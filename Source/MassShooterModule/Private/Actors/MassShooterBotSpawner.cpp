// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Actors/MassShooterBotSpawner.h"
#include "Characters/MassShooterBot.h"
#include "Components/MassShooterHealthComponent.h"
#include "Mass/MassActorBindingComponent.h"
#include "GAS/AttributeSetBase.h"
#include "MassShooterLog.h"

#include "Characters/Unit/UnitBase.h"
#include "GameModes/RTSGameModeBase.h"
#include "Actors/Waypoint.h"
#include "Core/UnitData.h"
#include "Engine/DataTable.h"
#include "GameFramework/PlayerStart.h"

#include "Kismet/GameplayStatics.h"
#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "MassEntityManager.h"
#include "MassNavigationFragments.h"
#include "Steering/MassSteeringFragments.h"
#include "MassMovementFragments.h"
#include "MassCommonFragments.h"
#include "Mass/UnitMassTag.h"
#include "Mass/UnitNavigationFragments.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"

namespace
{
	int32 GLogBots = 0;
	FAutoConsoleVariableRef CVarLogBots(
		TEXT("Shooter.Debug.LogBots"), GLogBots,
		TEXT("1 = log each spawned bot's state, position and distance moved every 2 s. ")
		TEXT("2 = same at 4 Hz, which is the rate needed to see a per-frame flag like ")
		TEXT("bIsPathfindingInProgress toggle; a 2 s sample aliases it away entirely."), ECVF_Cheat);
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

	// Read once here, not per call: the timer period is fixed when it is armed, and the fast rate
	// only matters for a deliberate diagnostic run started before PIE.
	AuditInterval = (GLogBots >= 2) ? 0.25f : 2.f;
	GetWorldTimerManager().SetTimer(AuditTimer, this, &AMassShooterBotSpawner::AuditBots,
		AuditInterval, /*bLoop*/ true);
}

const FUnitSpawnParameter* AMassShooterBotSpawner::PickSpawnRow()
{
	if (!SpawnTable)
	{
		return nullptr;
	}

	TArray<FUnitSpawnParameter*> Rows;
	SpawnTable->GetAllRows<FUnitSpawnParameter>(TEXT("MassShooterBotSpawner"), Rows);
	if (Rows.Num() == 0)
	{
		return nullptr;
	}

	RowSpawnCounts.SetNumZeroed(Rows.Num());

	// Whichever row is furthest behind its intended share gets the next spawn. With weights 4 and
	// 1 that produces melee, melee, melee, melee, ranged and repeats - a stable mix even in a
	// six-bot wave.
	float TotalWeight = 0.f;
	for (const FUnitSpawnParameter* Row : Rows)
	{
		TotalWeight += FMath::Max(0, Row->UnitCount);
	}
	if (TotalWeight <= 0.f)
	{
		return Rows[0];
	}

	int32 BestIndex = 0;
	float BestDeficit = -MAX_flt;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const float Share = FMath::Max(0, Rows[Index]->UnitCount) / TotalWeight;
		if (Share <= 0.f)
		{
			continue;
		}

		// How many spawns this row is owed relative to what it has had.
		const float Deficit = Share * (SpawnCounter + 1) - RowSpawnCounts[Index];
		if (Deficit > BestDeficit)
		{
			BestDeficit = Deficit;
			BestIndex = Index;
		}
	}

	++RowSpawnCounts[BestIndex];
	return Rows[BestIndex];
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

		// Chase / Run "moving but going nowhere" is not answerable from the actor alone: what
		// decides whether a unit walks is the Mass move target and the nav path behind it. Read
		// them here rather than inferring from position deltas.
		FString MassDiag = TEXT("(no entity)");
		const FMassEntityManager* EntityManager = nullptr;
		FMassEntityHandle EntityHandle;
		if (Unit->GetMassEntityData(EntityManager, EntityHandle) && EntityManager)
		{
			const FMassMoveTargetFragment* Move = EntityManager->GetFragmentDataPtr<FMassMoveTargetFragment>(EntityHandle);
			const FMassAITargetFragment* Target = EntityManager->GetFragmentDataPtr<FMassAITargetFragment>(EntityHandle);
			const FMassAIStateFragment* State = EntityManager->GetFragmentDataPtr<FMassAIStateFragment>(EntityHandle);
			const FMassVelocityFragment* Velocity = EntityManager->GetFragmentDataPtr<FMassVelocityFragment>(EntityHandle);
			const FUnitNavigationPathFragment* Path = EntityManager->GetFragmentDataPtr<FUnitNavigationPathFragment>(EntityHandle);
			const FMassCombatStatsFragment* Stats = EntityManager->GetFragmentDataPtr<FMassCombatStatsFragment>(EntityHandle);

			// The target's REAL position next to the position the unit believes it has: a chase
			// that stalls because LastKnownLocation went stale looks identical to one that stalls
			// in navigation until these two are printed side by side.
			FVector TrueTargetLoc = FVector::ZeroVector;
			if (Target && EntityManager->IsEntityValid(Target->TargetEntity))
			{
				if (const FTransformFragment* TT = EntityManager->GetFragmentDataPtr<FTransformFragment>(Target->TargetEntity))
				{
					TrueTargetLoc = TT->GetTransform().GetLocation();
				}
			}

			// UUnitMovementProcessor is what turns a move target into velocity, and its query
			// EXCLUDES a set of tags (StopMovement, Frozen, IsAttacked, StopWhileAiming,
			// RunAnimation, EffectArea). A unit carrying any of them has a path, a destination and
			// a desired speed and still never moves - indistinguishable from a navigation failure
			// unless the tags are printed.
			// The two flags UUnitMovementProcessor checks before anything else, and the distance to
			// the path point it steers at: those three separate "forbidden to move", "no path" and
			// "steering at a point it is already standing on", which all look the same from outside.
			float DistToPathPt = -1.f;
			if (Path && Path->HasValidPath())
			{
				const TArray<FNavPathPoint>& Pts = Path->CurrentPath->GetPathPoints();
				if (Pts.IsValidIndex(Path->CurrentPathPointIndex))
				{
					DistToPathPt = FVector::Dist2D(Now, Pts[Path->CurrentPathPointIndex].Location);
				}
			}

			// Reproduce the exact projection UUnitMovementProcessor does before every path request:
			// it lowers the query point by half the capsule half-height and uses a 100/100/500
			// extent, and when THAT fails it silently resets the path, zeroes the steering and
			// tries again next tick - no log, no cooldown. A unit stuck that way reads as
			// "path=NO searching=0 steer=0", which is what the stalled bots show.
			// FUnitNavigationPathFragment::HasValidPath only tests the shared pointer. The mover
			// tests FNavigationPath::IsValid(), which additionally requires the path to be
			// UP TO DATE - and a path whose navmesh tiles were rebuilt underneath it is not. That
			// case resets the path and zeroes the steering every tick, so the unit holds a path,
			// holds a full-speed move order, and never takes a step.
			int32 PathUpToDate = -1;
			int32 PathObjValid = -1;
			if (Path && Path->HasValidPath())
			{
				PathUpToDate = Path->CurrentPath->IsUpToDate() ? 1 : 0;
				PathObjValid = Path->CurrentPath->IsValid() ? 1 : 0;
			}

			int32 StartProjOk = -1;
			float StartProjOffset = -1.f;
			if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld()))
			{
				FVector Probe = Unit->GetMassActorLocation();
				if (const UCapsuleComponent* Capsule = Unit->GetCapsuleComponent())
				{
					Probe.Z -= Capsule->GetScaledCapsuleHalfHeight() / 2.f;
				}
				FNavLocation Projected;
				StartProjOk = NavSys->ProjectPointToNavigation(Probe, Projected, FVector(100.f, 100.f, 500.f)) ? 1 : 0;
				if (StartProjOk == 1)
				{
					StartProjOffset = FVector::Dist(Probe, Projected.Location);
				}
			}

			const FMassSteeringFragment* Steer = EntityManager->GetFragmentDataPtr<FMassSteeringFragment>(EntityHandle);
			const FString TagDiag = FString::Printf(
				TEXT("pathValid=%d upToDate=%d startProj=%d projOff=%.0f canMove=%d init=%d distPathPt=%.0f unitTag=%d stopMove=%d frozen=%d isAttacked=%d stopAim=%d runAnim=%d effArea=%d rotMouse=%d chase=%d run=%d atk=%d pause=%d"),
				PathObjValid, PathUpToDate, StartProjOk, StartProjOffset,
				State ? (int32)State->CanMove : -1, State ? (int32)State->IsInitialized : -1, DistToPathPt,
				DoesEntityHaveTag(*EntityManager, EntityHandle, FUnitMassTag::StaticStruct()) ? 1 : 0,
				DoesEntityHaveTag(*EntityManager, EntityHandle, FMassStateStopMovementTag::StaticStruct()) ? 1 : 0,
				DoesEntityHaveTag(*EntityManager, EntityHandle, FMassStateFrozenTag::StaticStruct()) ? 1 : 0,
				DoesEntityHaveTag(*EntityManager, EntityHandle, FMassStateIsAttackedTag::StaticStruct()) ? 1 : 0,
				DoesEntityHaveTag(*EntityManager, EntityHandle, FMassStopWhileAimingTag::StaticStruct()) ? 1 : 0,
				DoesEntityHaveTag(*EntityManager, EntityHandle, FRunAnimationTag::StaticStruct()) ? 1 : 0,
				DoesEntityHaveTag(*EntityManager, EntityHandle, FMassIsEffectAreaTag::StaticStruct()) ? 1 : 0,
				DoesEntityHaveTag(*EntityManager, EntityHandle, FMassRotateToMouseTag::StaticStruct()) ? 1 : 0,
				DoesEntityHaveTag(*EntityManager, EntityHandle, FMassStateChaseTag::StaticStruct()) ? 1 : 0,
				DoesEntityHaveTag(*EntityManager, EntityHandle, FMassStateRunTag::StaticStruct()) ? 1 : 0,
				DoesEntityHaveTag(*EntityManager, EntityHandle, FMassStateAttackTag::StaticStruct()) ? 1 : 0,
				DoesEntityHaveTag(*EntityManager, EntityHandle, FMassStatePauseTag::StaticStruct()) ? 1 : 0);

			MassDiag = FString::Printf(
				TEXT("mtCenter=(%.0f,%.0f) mtDist=%.0f mtSlack=%.0f mtSpeed=%.0f mtAction=%d massVel=%.0f ")
				TEXT("lastKnown=(%.0f,%.0f) trueTgt=(%.0f,%.0f) distTrue=%.0f range=%.0f runSpeed=%.0f ")
				TEXT("noProgress=%.1f path=%s pts=%d idx=%d pathTgt=(%.0f,%.0f) searching=%d steer=%.0f %s"),
				Move ? Move->Center.X : 0.f, Move ? Move->Center.Y : 0.f,
				Move ? Move->DistanceToGoal : -1.f, Move ? Move->SlackRadius : -1.f,
				Move ? Move->DesiredSpeed.Get() : -1.f, Move ? (int32)Move->GetCurrentAction() : -1,
				Velocity ? Velocity->Value.Size2D() : -1.f,
				Target ? Target->LastKnownLocation.X : 0.f, Target ? Target->LastKnownLocation.Y : 0.f,
				TrueTargetLoc.X, TrueTargetLoc.Y,
				FVector::Dist2D(Now, TrueTargetLoc),
				Stats ? Stats->AttackRange : -1.f, Stats ? Stats->RunSpeed : -1.f,
				State ? State->NoProgressTimer : -1.f,
				Path ? (Path->HasValidPath() ? TEXT("yes") : TEXT("NO")) : TEXT("-"),
				(Path && Path->HasValidPath()) ? Path->CurrentPath->GetPathPoints().Num() : -1,
				Path ? Path->CurrentPathPointIndex : -1,
				Path ? Path->PathTargetLocation.X : 0.f, Path ? Path->PathTargetLocation.Y : 0.f,
				Path ? (int32)Path->bIsPathfindingInProgress : -1,
				Steer ? Steer->DesiredVelocity.Size2D() : -1.f, *TagDiag);
		}

		UE_LOG(LogMassShooter, Log,
			TEXT("BOT %s state=%d actor=%s massXY=(%.0f,%.0f) movedIn2s=%.0f speed=%.0f target=%s %s"),
			*Unit->GetName(), (int32)Unit->GetUnitState(), *Now.ToCompactString(),
			Mass.X, Mass.Y, Moved, Unit->GetVelocity().Size2D(),
			*GetNameSafe(Unit->UnitToChase), *MassDiag);

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
	if (!HasAuthority())
	{
		return nullptr;
	}

	// The spawn table wins when there is one. Everything the row decides - class, state, waypoint
	// tag - is read from it; the properties below are the no-table fallback.
	const FUnitSpawnParameter* Row = PickSpawnRow();

	TSubclassOf<AUnitBase> ChosenClass = Row ? Row->UnitBaseClass : nullptr;

	if (!Row)
	{
		// Melee-heavy mix, counted rather than rolled: at MeleeShare 0.8 a random draw still
		// produces runs of four ranged bots often enough to matter, and a wave of six is far too
		// small to average that out. Picking melee whenever the running ratio has fallen below the
		// target keeps every wave close to the intended mix.
		const bool bWantMelee = MeleeBotClass
			&& (SpawnCounter == 0 ? MeleeShare > 0.f
				: (float)MeleeSpawnCounter / (float)SpawnCounter < MeleeShare);

		ChosenClass = bWantMelee ? MeleeBotClass : BotClass;
		if (bWantMelee)
		{
			++MeleeSpawnCounter;
		}
	}

	++SpawnCounter;

	if (!ChosenClass)
	{
		UE_LOG(LogMassShooter, Warning,
			TEXT("%s: nothing to spawn - %s."), *GetName(),
			Row ? TEXT("the chosen spawn-table row has no UnitBaseClass")
				: TEXT("no SpawnTable and no BotClass"));
		return nullptr;
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
	// TeamId before FinishSpawning is what lets AUnitBase::BeginPlay read the right alliance mask
	// out of UPlayerTeamSubsystem by itself - no stamping needed here.
	Unit->TeamId = BotTeamId;
	Unit->SetMeshRotationServer();

	// Perception BEFORE FinishSpawning, not after: the unit's BeginPlay builds the Mass entity and
	// copies the binding component's sight values into it. See ForeignUnitSightRadius.
	if (bAdaptForeignUnits && !Unit->IsA<AMassShooterBot>())
	{
		PrepareForeignUnitPerception(Unit);
	}

	// UnitData::None is the row's "not specified" value, so a row that leaves State blank inherits
	// the spawner's InitialState rather than spawning the unit into state None.
	const TEnumAsByte<UnitData::EState> RowState =
		(Row && Row->State != UnitData::None) ? Row->State : InitialState;
	const TEnumAsByte<UnitData::EState> RowPlaceholder =
		(Row && Row->StatePlaceholder != UnitData::None) ? Row->StatePlaceholder : RowState;

	Unit->UnitState = RowState;
	Unit->UnitStatePlaceholder = RowPlaceholder;

	// The waypoint is assigned BEFORE FinishSpawning, like team and state: UUnitStateProcessor
	// reads NextWaypoint when it seeds the patrol fragment, and PatrolRandom falls back to the
	// unit's own spawn point when there is none - the difference between advancing on the players
	// and milling about at the spawner.
	//
	// The row's WaypointTag is matched against AWaypoint::Tag by RTSUnitTemplate's own
	// ARTSGameModeBase::AssignWaypointToUnit, so waypoints are authored in the level the same way
	// they are for any other RTS unit.
	bool bHasWaypoint = false;
	if (Row && !Row->WaypointTag.IsEmpty())
	{
		GameMode->AssignWaypointToUnit(Unit, Row->WaypointTag);
		bHasWaypoint = Unit->NextWaypoint != nullptr;

		if (!bHasWaypoint)
		{
			// Named but absent is worth saying out loud: silently falling back would leave a
			// misspelled tag looking like it worked while every bot patrolled the wrong place.
			UE_LOG(LogMassShooter, Warning,
				TEXT("%s: no AWaypoint in the level has Tag \"%s\" - falling back to the generated advance point."),
				*GetName(), *Row->WaypointTag);
		}
	}

	if (!bHasWaypoint)
	{
		if (AWaypoint* Fallback = GetOrCreateAdvanceWaypoint())
		{
			Unit->NextWaypoint = Fallback;
		}
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
	else if (bAdaptForeignUnits)
	{
		AdaptForeignUnit(Unit, HealthMultiplier, DamageMultiplier);
	}
	GameMode->AddUnitIndexAndAssignToAllUnitsArray(Unit); // replicated UnitIndex + registry entry
	Unit->ScheduleDelayedNavigationUpdate();

	Spawned.Add(Unit);
	return Unit;
}

void AMassShooterBotSpawner::AdaptForeignUnit(AUnitBase* Unit, float HealthMultiplier, float DamageMultiplier)
{
	if (!Unit || !HasAuthority())
	{
		return;
	}

	// Observe, do not overwrite. The unit's own attribute table decided how much health a
	// Xenocrypta Skitterling has, and this module has no business replacing that with the player
	// pawn's 150. bOverrideStatsOnStart = false leaves ApplyDefaultStats a no-op, so the component
	// contributes only what is actually missing: the death signal.
	if (!Unit->FindComponentByClass<UMassShooterHealthComponent>())
	{
		UMassShooterHealthComponent* Health = NewObject<UMassShooterHealthComponent>(Unit);
		Health->bOverrideStatsOnStart = false;
		Health->RegisterComponent();
		Unit->AddInstanceComponent(Health);
	}

	// Wave scaling, straight on the attribute set. AMassShooterBot routes this through its own
	// health component's DefaultMaxHealth, which a foreign unit does not have.
	UAttributeSetBase* Attributes = Unit->Attributes;
	if (!Attributes)
	{
		return;
	}

	if (HealthMultiplier > 0.f && !FMath::IsNearlyEqual(HealthMultiplier, 1.f))
	{
		// BaseHealth is load-bearing: ALevelUnit's init recomputes MaxHealth from it, so scaling
		// only Max/Health would be undone the next time that init runs.
		const float BaseHealth = Attributes->GetBaseHealth() > 0.f
			? Attributes->GetBaseHealth()
			: Attributes->GetMaxHealth();

		if (BaseHealth > 0.f)
		{
			Attributes->SetAttributeBaseHealth(BaseHealth * HealthMultiplier);
			Attributes->SetAttributeMaxHealth(BaseHealth * HealthMultiplier);
			Attributes->SetAttributeHealth(BaseHealth * HealthMultiplier);
		}
	}

	if (DamageMultiplier > 0.f && !FMath::IsNearlyEqual(DamageMultiplier, 1.f))
	{
		const float BaseDamage = Attributes->GetBaseAttackDamage() > 0.f
			? Attributes->GetBaseAttackDamage()
			: Attributes->GetAttackDamage();

		// Refuse to scale an uninitialised stat block - multiplying zero writes zero, and a unit
		// with AttackDamage 0 lands every hit for exactly nothing.
		if (BaseDamage > 0.f)
		{
			Attributes->SetAttributeBaseAttackDamage(BaseDamage * DamageMultiplier);
			Attributes->SetAttributeAttackDamage(BaseDamage * DamageMultiplier);
		}
	}
}

void AMassShooterBotSpawner::PrepareForeignUnitPerception(AUnitBase* Unit)
{
	if (!Unit || ForeignUnitSightRadius <= 0.f)
	{
		return;
	}

	if (UMassActorBindingComponent* Binding = Unit->MassActorBindingComponent)
	{
		Binding->SightRadius = ForeignUnitSightRadius;
		Binding->LoseSightRadius = FMath::Max(ForeignUnitLoseSightRadius, ForeignUnitSightRadius * 1.2f);
	}

	// A unit that never runs detection never acquires a target and therefore never fights. RTS
	// units usually have this on, but a hostile that is supposed to hunt the player must not
	// depend on that.
	Unit->ToggleUnitDetection = true;
}
