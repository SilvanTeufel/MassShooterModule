// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Components/MassShooterCombatComponent.h"
#include "Components/MassShooterLoadoutComponent.h"
#include "Components/MassShooterHealthComponent.h"
#include "Settings/MassShooterSettings.h"
#include "MassShooterLog.h"

#include "Characters/Unit/UnitBase.h"
#include "Controller/PlayerController/ExtendedControllerBase.h"
#include "Components/WeaponComponent.h"
#include "GAS/GameplayAbilityBase.h"
#include "Mass/UnitMassTag.h"
#include "AbilitySystemComponent.h"
#include "MassEntityManager.h"

#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "Net/UnrealNetwork.h"

namespace MassShooterTestCVars
{
	// Holding the trigger is the one thing an automated run cannot do, which makes the core loop
	// of a shooter the hardest part to prove. Gated on IsLocallyControlled below, so a process-wide
	// cvar naturally drives only each machine's own pawn — exactly like a real mouse button, and
	// therefore exercising the same client -> server RPC path rather than bypassing it.
	static int32 HoldFire = 0;
	static FAutoConsoleVariableRef CVarHoldFire(
		TEXT("Shooter.Test.HoldFire"), HoldFire,
		TEXT("1 = hold the fire trigger on the locally controlled shooter pawn."), ECVF_Cheat);

	// "Do my shots go where I am aiming" is not answerable from a log line — it is a geometry
	// question, so it gets drawn. Green = camera to aim point, yellow = muzzle to the point the
	// shot is actually sent at (spread included). If those diverge, the divergence is visible.
	static int32 DrawAim = 0;
	static FAutoConsoleVariableRef CVarDrawAim(
		TEXT("Shooter.Debug.DrawAim"), DrawAim,
		TEXT("1 = draw the aim trace and the actual shot direction."), ECVF_Cheat);

	// "Can I shoot upward" is a question about the CAMERA, and an automated run has no mouse to
	// tilt it with. Without this the headless pass could only ever prove the level case, which is
	// precisely the case that already worked.
	static float AimPitch = 999.f;
	static FAutoConsoleVariableRef CVarAimPitch(
		TEXT("Shooter.Test.AimPitch"), AimPitch,
		TEXT("Force the local pawn's view pitch to this many degrees (positive = up). 999 = off."), ECVF_Cheat);
}

bool UMassShooterCombatComponent::IsTestTriggerActive()
{
	return MassShooterTestCVars::HoldFire != 0;
}

UMassShooterCombatComponent::UMassShooterCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UMassShooterCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMassShooterCombatComponent, bAiming);
}

void UMassShooterCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerUnit = Cast<AUnitBase>(GetOwner());
	if (!OwnerUnit)
	{
		UE_LOG(LogMassShooter, Error,
			TEXT("UMassShooterCombatComponent on %s, which is not an AUnitBase. Disabling."),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<null>"));
		SetComponentTickEnabled(false);
		return;
	}

	Loadout = GetOwner()->FindComponentByClass<UMassShooterLoadoutComponent>();
	Health = GetOwner()->FindComponentByClass<UMassShooterHealthComponent>();

	CurrentSpread = BaseSpreadDegrees;
}

void UMassShooterCombatComponent::SetFiring(bool bNewFiring)
{
	const bool bWasFiring = bFiring;
	bFiring = bNewFiring;

	// Releasing and re-pressing should not be punished by a leftover cooldown from a burst that
	// already ended, but it must not become a rate-of-fire exploit either — so only clear the
	// gate when it has nearly expired anyway.
	if (bFiring && FireCooldownRemaining < 0.02f)
	{
		FireCooldownRemaining = 0.f;
	}

	// Shoot on the PRESS EDGE, not on the next Tick.
	//
	// Firing only from Tick meant every shot waited for the next frame in which the trigger was
	// still held — so a short tap released before that frame produced nothing at all, and even a
	// held trigger felt like it had a delay before the first round. A trigger pull must produce a
	// round immediately when the weapon is ready; Tick then keeps automatic fire going.
	if (!bWasFiring && bFiring && FireCooldownRemaining <= 0.f && OwnerUnit && OwnerUnit->IsLocallyControlled())
	{
		if (!Health || !Health->IsDeadShooter())
		{
			UpdateAimPoint();   // the cached point may be a frame old, or unset on the very first shot
			TryFireOnce();
		}
	}

	// Tell the server the trigger came up. RTSUnitTemplate abilities can be CONTINUOUS
	// (UGameplayAbilityBase::bIsContinuousAbility): those keep running until the input-released
	// notification arrives, so without this a released trigger leaves the ability firing on its
	// own. Only sent on the falling edge, so it costs nothing while simply not shooting.
	if (bWasFiring && !bFiring)
	{
		if (GetOwnerRole() == ROLE_Authority)
		{
			Server_StopFire_Implementation();
		}
		else
		{
			Server_StopFire();
		}
	}
}

void UMassShooterCombatComponent::Server_StopFire_Implementation()
{
	if (!OwnerUnit)
	{
		return;
	}

	// 1) The polite path. Only does anything when the running instance's AbilityInputID happens to
	//    equal the slot we activated with (AGASUnit::NotifyInputReleased compares them) AND the
	//    ability's Blueprint implements OnInputReleased. Both are properties of a content asset we
	//    do not own, so this cannot be the only mechanism.
	OwnerUnit->NotifyInputReleased(FireAbilitySlot);

	const int32 SlotIndex = static_cast<int32>(FireAbilitySlot) - static_cast<int32>(EGASAbilityInputID::AbilityOne);
	const TSubclassOf<UGameplayAbilityBase> ShootClass =
		OwnerUnit->DefaultAbilities.IsValidIndex(SlotIndex) ? OwnerUnit->DefaultAbilities[SlotIndex] : nullptr;

	// 2) The certain path: cancel any live instance of the shoot ability. A CONTINUOUS ability
	//    (UGameplayAbilityBase::bIsContinuousAbility) keeps producing projectiles until it is told
	//    to stop, which is why a single click could empty the magazine.
	if (ShootClass)
	{
		if (UAbilitySystemComponent* ASC = OwnerUnit->GetAbilitySystemComponent())
		{
			// Handles are collected first: cancelling mutates the activatable list.
			TArray<FGameplayAbilitySpecHandle> ToCancel;
			for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
			{
				if (Spec.IsActive() && Spec.Ability && Spec.Ability->GetClass() == ShootClass)
				{
					ToCancel.Add(Spec.Handle);
				}
			}
			for (const FGameplayAbilitySpecHandle& Handle : ToCancel)
			{
				ASC->CancelAbilityHandle(Handle);
			}
		}
	}

	// 2b) Drop anything that queued up behind a busy weapon. Without this a burst of requests made
	//     during a reload still fires after the player has let go of the trigger.
	{
		const TArray<FQueuedAbility>& Queued = OwnerUnit->GetQueuedAbilities();
		for (int32 Index = Queued.Num() - 1; Index >= 0; --Index)
		{
			OwnerUnit->DequeueAbility(Index);
		}
	}

	// 3) Belt and braces: a continuous ability marks the entity with the RTS continuous-attack tag
	//    (GameplayAbilityBase.cpp:200), and that tag is what the Mass state machine keeps firing
	//    from. Cancelling the ability should clear it; removing it explicitly means a cancel that
	//    lands a frame late cannot leave the stream running.
	FMassEntityManager* EntityManager = nullptr;
	FMassEntityHandle Entity;
	if (OwnerUnit->GetMassEntityData(EntityManager, Entity) && EntityManager && Entity.IsSet()
		&& EntityManager->IsEntityValid(Entity))
	{
		EntityManager->Defer().RemoveTag<FMassStateContinuousAttackTag>(Entity);
	}
}

void UMassShooterCombatComponent::SetAiming(bool bNewAiming)
{
	if (bAiming == bNewAiming)
	{
		return;
	}

	bAiming = bNewAiming;

	// Predict locally (the crosshair must react on the same frame as the button) and tell the
	// server, which owns the replicated value everybody else animates from.
	if (GetOwnerRole() != ROLE_Authority)
	{
		Server_SetAiming(bNewAiming);
	}
}

void UMassShooterCombatComponent::Server_SetAiming_Implementation(bool bNewAiming)
{
	bAiming = bNewAiming;
}

void UMassShooterCombatComponent::RequestReload()
{
	if (GetOwnerRole() == ROLE_Authority)
	{
		Server_Reload_Implementation();
	}
	else
	{
		Server_Reload();
	}
}

void UMassShooterCombatComponent::Server_Reload_Implementation()
{
	if (!OwnerUnit || (Health && Health->IsDeadShooter()))
	{
		return;
	}

	// The reload ability is granted in DefaultAbilities and resolved positionally; GAS refuses a
	// second activation while one is already running, so this is safe to call repeatedly.
	OwnerUnit->ActivateAbilityByInputID(ReloadAbilitySlot, OwnerUnit->DefaultAbilities,
		FHitResult(), Cast<APlayerController>(OwnerUnit->GetController()));
}

void UMassShooterCombatComponent::FireAt(FVector AimLocation)
{
	if (GetOwnerRole() == ROLE_Authority)
	{
		ExecuteFire(AimLocation);
	}
	else
	{
		Server_Fire(AimLocation);
	}
}

void UMassShooterCombatComponent::Server_Fire_Implementation(FVector_NetQuantize AimLocation)
{
	ExecuteFire(AimLocation);
}

void UMassShooterCombatComponent::ExecuteFire(const FVector& AimLocation)
{
	if (!OwnerUnit || (Health && Health->IsDeadShooter()))
	{
		return;
	}

	// Never ask while another ability is running.
	//
	// AGASUnit::ActivateAbilityByInputID does not refuse a busy request — it QUEUES it (up to
	// MaxAbilityQueueSize = 6) and drains the queue later. For a held trigger that turns steady
	// fire into bursts and dead gaps: while a reload is active every request piles up, then all of
	// them fire at once. Skipping is correct here because the trigger is still held; the next tick
	// asks again as soon as the weapon is free.
	if (OwnerUnit->IsAnyAbilityActive())
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(OwnerUnit->GetController());

	// THE aim handoff. UGameplayAbilityBase::GetTargetLocation — which is what the shoot ability
	// actually aims with — returns AExtendedControllerBase::ReplicatedMouseLocation, NOT the hit
	// result and NOT FMassAITargetFragment::AbilityTargetLocation.
	//
	// RTSUnitTemplate keeps that value current from the cursor trace in AControllerBase::Tick, and
	// this module deliberately skips that Tick (a shooter has no cursor, and it is a world trace
	// per player per frame). So the value never moved and every shot flew at wherever it happened
	// to be — the reported "projectiles do not go where I aim".
	//
	// Written here, immediately before activation, so the ability reads THIS shot's aim rather
	// than a throttled or one-frame-stale one.
	if (AExtendedControllerBase* ExtPC = Cast<AExtendedControllerBase>(PC))
	{
		ExtPC->ReplicatedMouseLocation = AimLocation;

		// Logged HERE, after the write and immediately before activation, so it shows the value
		// the ability will actually read. Logging it on the client side before the RPC showed the
		// previous frame's value and made a working shot look broken.
		if (MassShooterTestCVars::DrawAim != 0)
		{
			// The muzzle is logged next to the aim because RTSUnitTemplate builds the flight
			// direction as (aim - muzzle): a wrong muzzle HEIGHT tilts every shot even when the aim
			// is perfect. GetProjectileSpawnLocation derives its Z solely from the Mass
			// characteristics fragment's LastGroundLocation, so this line also reads out whether
			// that value is tracking the pawn (see UMassShooterPawnSyncProcessor).
			const FVector Muzzle = OwnerUnit->GetProjectileSpawnLocation();
			const FVector Dir = (AimLocation - Muzzle).GetSafeNormal();
			UE_LOG(LogMassShooter, Log,
				TEXT("FIRE: abilityAim=%s muzzle=%s actorZ=%.0f resultPitch=%.1f deg"),
				*ExtPC->ReplicatedMouseLocation.ToCompactString(), *Muzzle.ToCompactString(),
				OwnerUnit->GetActorLocation().Z, Dir.Rotation().Pitch);
		}
	}

	// The hit result is still supplied: AGASUnit::FireMouseHitAbility writes it into
	// FMassAITargetFragment::AbilityTargetLocation (used by rotate-to-target and by abilities that
	// read the fragment) and drives OnAbilityMouseHit. It must be a valid BLOCKING hit or that
	// whole path is skipped.
	FHitResult AimHit;
	AimHit.bBlockingHit = true;
	AimHit.Location = AimLocation;
	AimHit.ImpactPoint = AimLocation;

	OwnerUnit->ActivateAbilityByInputID(FireAbilitySlot, OwnerUnit->DefaultAbilities, AimHit, PC);
}

void UMassShooterCombatComponent::UpdateAimPoint()
{
	const APlayerController* PC = OwnerUnit ? Cast<APlayerController>(OwnerUnit->GetController()) : nullptr;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector Start;
	FVector Direction;

	if (PC && PC->PlayerCameraManager)
	{
		Start = PC->PlayerCameraManager->GetCameraLocation();
		Direction = PC->PlayerCameraManager->GetCameraRotation().Vector();
	}
	else if (OwnerUnit)
	{
		// No camera (dedicated-server copy of the pawn, or a bot): aim straight ahead. The value
		// is only used locally for the crosshair, so this is a display fallback, not gameplay.
		Start = OwnerUnit->GetActorLocation() + FVector(0.f, 0.f, 50.f);
		Direction = OwnerUnit->GetActorForwardVector();
	}
	else
	{
		return;
	}

	const float MaxDistance = UMassShooterSettings::Get()->MaxAimTraceDistance;
	const FVector End = Start + Direction * MaxDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MassShooterAim), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(OwnerUnit);

	FHitResult Hit;
	CachedAimOrigin = Start;

	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		CachedAimPoint = Hit.ImpactPoint;
		CachedAimTarget = Hit.GetActor();
	}
	else
	{
		// Nothing hit: aim at the far end of the trace so distant shots still travel outward
		// instead of collapsing onto the muzzle.
		CachedAimPoint = End;
		CachedAimTarget = nullptr;
	}
}

FVector UMassShooterCombatComponent::ApplySpread(const FVector& Origin, const FVector& AimPoint) const
{
	const FVector ToAim = AimPoint - Origin;
	const float Distance = ToAim.Size();
	if (Distance <= KINDA_SMALL_NUMBER)
	{
		return AimPoint;
	}

	const float HalfAngle = CurrentSpread * (bAiming ? AimSpreadScale : 1.f);
	if (HalfAngle <= KINDA_SMALL_NUMBER)
	{
		return AimPoint;
	}

	const FVector Perturbed = FMath::VRandCone(ToAim / Distance, FMath::DegreesToRadians(HalfAngle));
	return Origin + Perturbed * Distance;
}

void UMassShooterCombatComponent::ApplyRecoil()
{
	APlayerController* PC = OwnerUnit ? Cast<APlayerController>(OwnerUnit->GetController()) : nullptr;
	if (!PC || !PC->IsLocalController() || RecoilPitchPerShot <= 0.f)
	{
		return;
	}

	// Bounded, and remembered so it can be given back.
	//
	// The first version just pushed the view up on every shot and never returned it, so holding
	// the trigger walked the camera at the fire rate until you were staring at the sky — the
	// reported "camera keeps jerking upward". Real recoil climbs while firing and settles back
	// when you stop, and it never exceeds a known ceiling.
	const float Kick = FMath::Min(RecoilPitchPerShot * (bAiming ? 0.6f : 1.f),
		FMath::Max(0.f, MaxRecoilPitchDegrees - AccumulatedRecoilPitch));
	if (Kick <= 0.f)
	{
		return;
	}

	AccumulatedRecoilPitch += Kick;

	// AddPitchInput is inverted relative to world pitch, so a negative value kicks the view UP.
	PC->AddPitchInput(-Kick);
	PC->AddYawInput(FMath::FRandRange(-RecoilYawPerShot, RecoilYawPerShot));

	// Recovery is suspended until the player stops shooting for RecoilRecoveryDelay.
	RecoilRecoveryHoldRemaining = RecoilRecoveryDelay;
}

void UMassShooterCombatComponent::RecoverRecoil(float DeltaTime)
{
	if (AccumulatedRecoilPitch <= 0.f)
	{
		return;
	}

	if (RecoilRecoveryHoldRemaining > 0.f)
	{
		RecoilRecoveryHoldRemaining -= DeltaTime;
		return;
	}

	APlayerController* PC = OwnerUnit ? Cast<APlayerController>(OwnerUnit->GetController()) : nullptr;
	if (!PC || !PC->IsLocalController())
	{
		AccumulatedRecoilPitch = 0.f;
		return;
	}

	// Give back only what we took, never more — so a player who deliberately aimed up during the
	// burst keeps that aim instead of being dragged back down past it.
	const float GiveBack = FMath::Min(AccumulatedRecoilPitch, RecoilRecoveryPerSecond * DeltaTime);
	AccumulatedRecoilPitch -= GiveBack;
	PC->AddPitchInput(GiveBack);
}

void UMassShooterCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!OwnerUnit)
	{
		return;
	}

	const bool bLocallyControlled = OwnerUnit->IsLocallyControlled();

	// ---- Server-only housekeeping ------------------------------------------------------------
	if (GetOwnerRole() == ROLE_Authority && bAutoReload && Loadout && (!Health || !Health->IsDeadShooter()))
	{
		AutoReloadAccumulator += DeltaTime;
		if (AutoReloadAccumulator >= 0.5f)
		{
			AutoReloadAccumulator = 0.f;

			// WeaponModule's ShootAbility does not reload itself when the magazine empties, so an
			// unattended weapon would sit at 0 rounds with magazines to spare.
			if (Loadout->GetCurrentAmmo() <= 0.f && Loadout->GetCurrentMagazineCount() > 0.f)
			{
				Server_Reload_Implementation();
			}
		}
	}

	if (!bLocallyControlled)
	{
		return;
	}

	// Test trigger. Routed through SetFiring rather than short-circuiting the fire check, so it
	// exercises the REAL press and release path — including Server_StopFire. The previous version
	// only forced shots out and could not have caught a broken release, which is precisely the bug
	// it was meant to test.
	const bool bTestHold = MassShooterTestCVars::HoldFire != 0;
	if (bTestHold != bTestTriggerHeld)
	{
		bTestTriggerHeld = bTestHold;
		SetFiring(bTestHold);
	}

	// Forced view pitch for automated runs. Applied before the aim trace so the very next shot
	// uses it.
	if (MassShooterTestCVars::AimPitch < 900.f)
	{
		if (APlayerController* TestPC = Cast<APlayerController>(OwnerUnit->GetController()))
		{
			FRotator Forced = TestPC->GetControlRotation();
			Forced.Pitch = MassShooterTestCVars::AimPitch;
			TestPC->SetControlRotation(Forced);
		}
	}

	// ---- Owning client: aim, spread, trigger -------------------------------------------------
	UpdateAimPoint();

	// Spread recovers toward its floor, which itself rises with movement speed. Recovery is
	// suspended while the trigger is down so held fire genuinely walks off target.
	const float SpeedFraction = OwnerUnit->GetVelocity().Size2D() > 1.f
		? FMath::Clamp(OwnerUnit->GetVelocity().Size2D() / 800.f, 0.f, 1.f)
		: 0.f;
	const float SpreadFloor = BaseSpreadDegrees + SpeedFraction * SpreadWhileMovingDegrees;

	if (CurrentSpread > SpreadFloor)
	{
		CurrentSpread = FMath::Max(SpreadFloor, CurrentSpread - SpreadRecoveryPerSecond * DeltaTime);
	}
	else
	{
		CurrentSpread = SpreadFloor;
	}

	// Runs unconditionally — the whole point is that it happens after the trigger is released.
	RecoverRecoil(DeltaTime);

	if (FireCooldownRemaining > 0.f)
	{
		FireCooldownRemaining -= DeltaTime;
	}

	if (!bFiring || FireCooldownRemaining > 0.f)
	{
		// While the test trigger is held, "not firing" is itself a defect worth naming: it means
		// something dropped the trigger state behind the test's back (death, a respawn, the
		// controller's safety net) and nothing ever restored it.
		if (!bFiring && bTestTriggerHeld)
		{
			ReportFireBlocked(TEXT("trigger state lost"));
		}
		return;
	}

	if (Health && Health->IsDeadShooter())
	{
		ReportFireBlocked(TEXT("dead"));
		return;
	}

	// Sustained automatic fire. The first round of a pull already left on the press edge
	// (see SetFiring); this keeps the stream going. Ammo and weapon-busy checks live in
	// TryFireOnce so both entry points behave identically.
	TryFireOnce();
}

void UMassShooterCombatComponent::ReportFireBlocked(const TCHAR* Reason)
{
	// A held trigger that produces nothing is the single hardest thing to diagnose in this
	// component, because every refusal path is a silent early return. Throttled to once per second
	// per reason so a stuck state prints steadily without drowning the log.
	if (MassShooterTestCVars::DrawAim == 0)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	if (Reason == LastFireBlockReason && Now - LastFireBlockLogTime < 1.f)
	{
		return;
	}

	LastFireBlockReason = Reason;
	LastFireBlockLogTime = Now;

	UE_LOG(LogMassShooter, Warning, TEXT("FIRE BLOCKED: %s (ammo=%.0f mags=%.0f dead=%d firing=%d)"),
		Reason,
		Loadout ? Loadout->GetCurrentAmmo() : -1.f,
		Loadout ? Loadout->GetCurrentMagazineCount() : -1.f,
		Health ? (int32)Health->IsDeadShooter() : -1,
		(int32)bFiring);
}

bool UMassShooterCombatComponent::TryFireOnce()
{
	if (!OwnerUnit)
	{
		return false;
	}

	// Out of ammo: hold off briefly instead of sending a doomed request every frame. The
	// auto-reload will refill.
	if (Loadout && Loadout->GetCurrentAmmo() <= 0.f)
	{
		FireCooldownRemaining = 0.25f;
		ReportFireBlocked(TEXT("no ammo"));
		return false;
	}

	// An ability is already running. For the shoot ability that is the NORMAL state of a held
	// trigger, not an error: WeaponModule's ShootAbility arms a looping timer and keeps firing at
	// the weapon's rate until it is cancelled, so one activation covers the whole burst and this
	// component's job while the trigger is down is simply not to activate it twice.
	//
	// Retry soon rather than burning the full weapon interval, so the shot that follows a reload
	// leaves as soon as the weapon is free instead of a whole interval later.
	if (OwnerUnit->IsAnyAbilityActive())
	{
		FireCooldownRemaining = 0.03f;
		ReportFireBlocked(TEXT("ability already running (expected while holding fire)"));
		return false;
	}

	const FVector MuzzleOrigin = OwnerUnit->GetProjectileSpawnLocation();
	const FVector ShotPoint = ApplySpread(MuzzleOrigin, CachedAimPoint);

	if (MassShooterTestCVars::DrawAim != 0)
	{
		const UWorld* World = GetWorld();
		DrawDebugLine(World, CachedAimOrigin, CachedAimPoint, FColor::Green, false, 3.f, 0, 1.5f);
		DrawDebugLine(World, MuzzleOrigin, ShotPoint, FColor::Yellow, false, 3.f, 0, 2.f);
		DrawDebugSphere(World, CachedAimPoint, 24.f, 10, FColor::Green, false, 3.f);

		// Pitch is called out explicitly: a shot that is level when the crosshair is not tells you
		// the elevation was lost somewhere between here and the projectile.
		const FVector ShotDir = (ShotPoint - MuzzleOrigin).GetSafeNormal();
		UE_LOG(LogMassShooter, Log, TEXT("AIM: muzzle=%s aim=%s shot=%s shotPitch=%.1f deg target=%s"),
			*MuzzleOrigin.ToCompactString(), *CachedAimPoint.ToCompactString(),
			*ShotPoint.ToCompactString(), ShotDir.Rotation().Pitch, *GetNameSafe(CachedAimTarget.Get()));
	}

	FireAt(ShotPoint);

	CurrentSpread = FMath::Min(MaxSpreadDegrees, CurrentSpread + SpreadPerShot);
	FireCooldownRemaining = Loadout ? Loadout->GetCurrentFireInterval() : 0.25f;

	ApplyRecoil();
	return true;
}
