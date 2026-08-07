// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Actors/MassShooterBotSpawner.h"
#include "Characters/MassShooterBot.h"
#include "MassShooterLog.h"

#include "Characters/Unit/UnitBase.h"
#include "GameModes/RTSGameModeBase.h"
#include "Actors/Waypoint.h"
#include "GameFramework/PlayerStart.h"

#include "Kismet/GameplayStatics.h"
#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

namespace
{
	int32 GLogBots = 0;
	FAutoConsoleVariableRef CVarLogBots(
		TEXT("Shooter.Debug.LogBots"), GLogBots,
		TEXT("1 = log each spawned bot's state, position and distance moved every 2 s."), ECVF_Cheat);
}

AMassShooterBotSpawner::AMassShooterBotSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // server-only director; the bots themselves replicate

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

#if WITH_EDITORONLY_DATA
	if (UBillboardComponent* Billboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("Billboard")))
	{
		Billboard->SetupAttachment(SceneRoot);
		Billboard->bIsEditorOnly = true;
	}
#endif

	BotClass = AMassShooterBot::StaticClass();
}

void AMassShooterBotSpawner::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	if (MaxAlive > 0)
	{
		TopUp();
		GetWorldTimerManager().SetTimer(TopUpTimer, this, &AMassShooterBotSpawner::TopUp,
			FMath::Max(1.f, RespawnInterval), /*bLoop*/ true);
	}

	GetWorldTimerManager().SetTimer(AuditTimer, this, &AMassShooterBotSpawner::AuditBots,
		2.f, /*bLoop*/ true);
}

AWaypoint* AMassShooterBotSpawner::GetOrCreateAdvanceWaypoint()
{
	if (AdvanceWaypoint)
	{
		return AdvanceWaypoint;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// An explicitly placed target wins; the waypoint is then just a handle onto it.
	FVector Destination = AdvanceTarget ? AdvanceTarget->GetActorLocation() : FVector::ZeroVector;

	if (!AdvanceTarget)
	{
		// Default: the middle of where the players start. That is the one point in any level of
		// this shape that reliably means "toward the fight", and it needs no level authoring.
		TArray<AActor*> Starts;
		UGameplayStatics::GetAllActorsOfClass(World, APlayerStart::StaticClass(), Starts);

		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
		for (const AActor* Start : Starts)
		{
			if (Start)
			{
				Sum += Start->GetActorLocation();
				++Count;
			}
		}

		if (Count == 0)
		{
			// No player starts at all: fall back to this spawner, which reproduces the old
			// wander-at-home behaviour rather than sending bots to the world origin.
			UE_LOG(LogMassShooter, Warning,
				TEXT("%s: no player starts found, bots will patrol their spawn point instead of advancing."),
				*GetName());
			Destination = GetActorLocation();
		}
		else
		{
			Destination = Sum / (float)Count;
		}
	}

	if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
	{
		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(Destination, Projected, FVector(3000.f, 3000.f, 1000.f)))
		{
			Destination = Projected.Location;
		}
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AdvanceWaypoint = World->SpawnActor<AWaypoint>(AWaypoint::StaticClass(), Destination, FRotator::ZeroRotator, Params);

	if (AdvanceWaypoint)
	{
		// UMassActorBindingComponent seeds FMassPatrolFragment::RandomPatrolRadius from the mean of
		// this offset, and that radius is what PatrolRandom scatters its destinations within.
		AdvanceWaypoint->PatrolCloseOffset = FVector2D(AdvanceWanderRadius, AdvanceWanderRadius);
		AdvanceWaypoint->PatrolCloseToWaypoint = true;
		AdvanceWaypoint->TeamId = BotTeamId;

		UE_LOG(LogMassShooter, Log, TEXT("%s: advance waypoint at %s (wander %.0f)."),
			*GetName(), *Destination.ToCompactString(), AdvanceWanderRadius);
	}

	return AdvanceWaypoint;
}

void AMassShooterBotSpawner::AuditBots()
{
	if (GLogBots == 0)
	{
		return;
	}

	// "Running on the spot" has two very different causes and this line separates them: a unit
	// whose position does not change while it is in a moving state is stalled in navigation, while
	// one whose position DOES change is moving and the problem is the visual sync instead.
	int32 Reported = 0;
	for (const TWeakObjectPtr<AUnitBase>& Ptr : Spawned)
	{
		AUnitBase* Unit = Ptr.Get();
		if (!Unit || Reported >= 6)
		{
			continue;
		}

		const FVector Now = Unit->GetActorLocation();
		const FVector Mass = Unit->GetMassActorLocation();
		FVector& Previous = LastAuditedLocations.FindOrAdd(Ptr, Now);
		const float Moved = FVector::Dist2D(Now, Previous);
		Previous = Now;

		UE_LOG(LogMassShooter, Log,
			TEXT("BOT %s state=%d actor=%s massXY=(%.0f,%.0f) movedIn2s=%.0f speed=%.0f target=%s"),
			*Unit->GetName(), (int32)Unit->GetUnitState(), *Now.ToCompactString(),
			Mass.X, Mass.Y, Moved, Unit->GetVelocity().Size2D(),
			*GetNameSafe(Unit->UnitToChase));
		++Reported;
	}
}

int32 AMassShooterBotSpawner::GetAliveCount()
{
	Spawned.RemoveAll([](const TWeakObjectPtr<AUnitBase>& Ptr)
	{
		const AUnitBase* Unit = Ptr.Get();
		return !Unit || Unit->GetUnitState() == UnitData::Dead;
	});

	return Spawned.Num();
}

void AMassShooterBotSpawner::TopUp()
{
	if (!HasAuthority())
	{
		return;
	}

	// Guard the loop: a spawn that keeps failing (no class, no game mode) must not spin.
	int32 Guard = 0;
	while (GetAliveCount() < MaxAlive && Guard++ < 64)
	{
		if (!SpawnOne())
		{
			break;
		}
	}
}

int32 AMassShooterBotSpawner::SpawnWave(int32 Count, float HealthMultiplier, float DamageMultiplier)
{
	if (!HasAuthority())
	{
		return 0;
	}

	int32 Spawnedcount = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (SpawnOne(HealthMultiplier, DamageMultiplier))
		{
			++Spawnedcount;
		}
		else
		{
			break;
		}
	}
	return Spawnedcount;
}

AUnitBase* AMassShooterBotSpawner::SpawnOne(float HealthMultiplier, float DamageMultiplier)
{
	if (!HasAuthority() || !BotClass)
	{
		return nullptr;
	}

	// Melee-heavy mix, counted rather than rolled: at MeleeShare 0.8 a random draw still produces
	// runs of four ranged bots often enough to matter, and a wave of six is far too small to
	// average that out. Picking melee whenever the running ratio has fallen below the target keeps
	// every wave close to the intended mix.
	const bool bWantMelee = MeleeBotClass
		&& (SpawnCounter == 0 ? MeleeShare > 0.f
			: (float)MeleeSpawnCounter / (float)SpawnCounter < MeleeShare);

	const TSubclassOf<AUnitBase> ChosenClass = bWantMelee ? MeleeBotClass : BotClass;
	++SpawnCounter;
	if (bWantMelee)
	{
		++MeleeSpawnCounter;
	}

	UWorld* World = GetWorld();
	ARTSGameModeBase* GameMode = World ? Cast<ARTSGameModeBase>(World->GetAuthGameMode()) : nullptr;
	if (!GameMode)
	{
		// AddUnitIndexAndAssignToAllUnitsArray lives on the RTS game mode and is what registers the
		// unit for replication. Spawning without it would produce a unit clients never link to.
		UE_LOG(LogMassShooter, Warning,
			TEXT("%s cannot spawn: the game mode does not derive ARTSGameModeBase."), *GetName());
		return nullptr;
	}

	const FVector Base = GetActorLocation();
	FVector Location(
		Base.X + FMath::FRandRange(-SpawnRadius, SpawnRadius),
		Base.Y + FMath::FRandRange(-SpawnRadius, SpawnRadius),
		Base.Z + SpawnZOffset);

	// Snap onto the navmesh when there is one, so a bot never starts life unable to path.
	if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
	{
		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(Location, Projected, FVector(SpawnRadius, SpawnRadius, 500.f)))
		{
			Location = Projected.Location + FVector(0.f, 0.f, SpawnZOffset);
		}
	}

	FTransform SpawnTransform;
	SpawnTransform.SetLocation(Location);
	SpawnTransform.SetRotation(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Quaternion());

	AUnitBase* Unit = Cast<AUnitBase>(UGameplayStatics::BeginDeferredActorSpawnFromClass(
		this, ChosenClass, SpawnTransform, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn));
	if (!Unit)
	{
		return nullptr;
	}

	// Team and state MUST be set before FinishSpawning: the unit's BeginPlay builds its Mass
	// entity and reads both, so setting them afterwards leaves the entity on the wrong team.
	Unit->TeamId = BotTeamId;
	Unit->SetMeshRotationServer();
	Unit->UnitState = InitialState;
	Unit->UnitStatePlaceholder = InitialState;

	// Before FinishSpawning, like team and state: UUnitStateProcessor reads NextWaypoint when it
	// seeds the patrol fragment, and falls back to the unit's own spawn point when there is none —
	// which is the difference between advancing on the players and milling about at the spawner.
	if (AWaypoint* Waypoint = GetOrCreateAdvanceWaypoint())
	{
		Unit->NextWaypoint = Waypoint;
	}

	UGameplayStatics::FinishSpawningActor(Unit, SpawnTransform);

	// Attributes FIRST, scaling second.
	//
	// The reverse order looks right and is not: before InitializeAttributes the attribute set is
	// still all zeros, so scaling multiplied zero by the wave factor and wrote AttackDamage = 0.
	// The bots then fired projectiles carrying zero damage, and because
	// AUnitBase::HandleProjectileImpact treats any DamageOverride >= 0 as authoritative, every hit
	// landed for exactly nothing. Measured: 1788 registered hits on the player, health never moved.
	Unit->InitializeAttributes();

	if (AMassShooterBot* Bot = Cast<AMassShooterBot>(Unit))
	{
		// Shooter-appropriate damage first, then the wave multiplier on top of it.
		Bot->ApplyAttackDamageOverride();
		Bot->ApplyWaveScaling(HealthMultiplier, DamageMultiplier);
	}
	GameMode->AddUnitIndexAndAssignToAllUnitsArray(Unit); // replicated UnitIndex + registry entry
	Unit->ScheduleDelayedNavigationUpdate();

	Spawned.Add(Unit);
	return Unit;
}
