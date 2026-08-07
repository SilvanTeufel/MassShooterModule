// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Abilities/MassShooterShootAbility.h"
#include "Characters/MassShooterCharacter.h"
#include "Components/MassShooterCombatComponent.h"
#include "MassShooterLog.h"

#include "Characters/Unit/UnitBase.h"
#include "Controller/PlayerController/ExtendedControllerBase.h"
#include "Components/WeaponComponent.h"
#include "Abilities/WeaponAttributeSet.h"

#include "Engine/World.h"
#include "TimerManager.h"

UMassShooterShootAbility::UMassShooterShootAbility()
{
	// Server-only, like every other RTSUnitTemplate ability: the projectile is authoritative and
	// replicates itself. InstancedPerActor so the repeating timer has somewhere to live.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	// NOT continuous: bIsContinuousAbility makes UGameplayAbilityBase tag the unit with
	// FMassStateContinuousAttackTag and hand firing to the RTS attack state, which aims at a
	// perceived target rather than at the crosshair.
	bIsContinuousAbility = false;
}

AUnitBase* UMassShooterShootAbility::GetShootingUnit() const
{
	return Cast<AUnitBase>(GetAvatarActorFromActorInfo());
}

bool UMassShooterShootAbility::ResolveAimPoint(FVector& OutAim) const
{
	const AUnitBase* Unit = GetShootingUnit();
	if (!Unit)
	{
		return false;
	}

	// Preferred source: the shooter's own camera trace. It is the only value that is correct at
	// the instant this round leaves, and it is full 3D.
	if (const AMassShooterCharacter* Shooter = Cast<AMassShooterCharacter>(Unit))
	{
		if (const UMassShooterCombatComponent* Combat = Shooter->GetCombat())
		{
			const FVector AimPoint = Combat->GetAimPoint();
			if (!AimPoint.IsNearlyZero())
			{
				OutAim = AimPoint;
				return true;
			}
		}
	}

	// Fallback for a non-shooter pawn granted this ability: the RTS aim value. Same source
	// UGameplayAbilityBase::GetTargetLocation uses.
	const FVector Replicated = GetTargetLocation();
	if (!Replicated.IsNearlyZero())
	{
		OutAim = Replicated;
		return true;
	}

	return false;
}

void UMassShooterShootAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// The first round leaves NOW, not one interval from now.
	//
	// WeaponModule's Blueprint arms its looping timer and waits for the first callback, so a pull
	// shorter than the weapon's interval was cancelled before anything was fired — with the table
	// default of FireRate 1.0 that meant a full second of nothing, and a tap produced no shot at
	// all.
	FireOneRound();

	TSubclassOf<AProjectile> ProjectileClass;
	FWeaponData WeaponData;
	float ExtraDamage = 0.f;
	int32 MaxPierced = 1;
	TSubclassOf<UGameplayEffect> Effect1, Effect2, Effect3;

	float Interval = 0.15f;
	if (GetShootInfo(ProjectileClass, WeaponData, ExtraDamage, MaxPierced, Effect1, Effect2, Effect3))
	{
		Interval = WeaponData.FireRate * FMath::Max(0.01f, WeaponData.FireRateMultiplier);
	}
	Interval = FMath::Max(0.02f, Interval);

	World->GetTimerManager().SetTimer(FireTimer, this, &UMassShooterShootAbility::FireOneRound,
		Interval, /*bLoop*/ true);
}

void UMassShooterShootAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireTimer);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UMassShooterShootAbility::FireOneRound()
{
	AUnitBase* Unit = GetShootingUnit();
	if (!Unit)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	TSubclassOf<AProjectile> ProjectileClass;
	FWeaponData WeaponData;
	float ExtraDamage = 0.f;
	int32 MaxPierced = 1;
	TSubclassOf<UGameplayEffect> Effect1, Effect2, Effect3;

	if (!GetShootInfo(ProjectileClass, WeaponData, ExtraDamage, MaxPierced, Effect1, Effect2, Effect3)
		|| !ProjectileClass)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	// Out of ammo ends the burst rather than firing blanks. The combat component's auto-reload
	// picks it up from there.
	const UWeaponAttributeSet* WeaponAttributes = GetWeaponAttributeSet();
	if (WeaponAttributes && WeaponAttributes->GetAmmo() <= 0.f)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
		return;
	}

	FVector Aim;
	if (!ResolveAimPoint(Aim))
	{
		return;
	}

	FEffectAreaInfo AreaInfo;
	GetEffectAreaInfo(0, AreaInfo);

	const int32 ProjectileCount = FMath::Max(1, GetProjectileCount());

	// The aim keeps its Z. That single difference from WeaponModule's Blueprint is the whole point
	// of this class: SpawnProjectileFromClassWithAim builds the flight direction as
	// (aim - muzzle) and preserves whatever height the caller passes in.
	Unit->SpawnProjectileFromClassWithAim(
		Aim,
		ProjectileClass,
		MaxPierced,
		ProjectileCount,
		ProjectileCount > 1 ? MultiProjectileSpread : 0.f,
		/*IsBouncingNext*/ false,
		/*IsBouncingBack*/ false,
		AimZOffset,
		/*Scale*/ 1.f,
		WeaponData.MuzzleSpawnOffset,
		ExtraDamage,
		Effect1, Effect2, Effect3,
		AreaInfo);

	ModifyAmmo(-1.f);
	ApplyGlobalCooldown();
}
