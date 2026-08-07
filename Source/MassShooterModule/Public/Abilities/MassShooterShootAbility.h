// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/ShootAbility.h"
#include "MassShooterShootAbility.generated.h"

class AUnitBase;

/**
 * The shooter's own automatic fire, built on WeaponModule's UShootAbility.
 *
 * WeaponModule's own BP_ShootAbility cannot be used by a first-person shooter, and the reason is
 * structural rather than a bug: its graph builds the shot's target as
 * (aim.X, aim.Y, ProjectileSpawn->GetComponentLocation().Z) — it deliberately replaces the aim's
 * height with the muzzle's. For an RTS clicking a ground target that is exactly right. It is also
 * why shots here could never travel up or down, measured directly off the projectile fragment:
 * targetZ was always identical to startZ and the flight direction's pitch was always 0.
 *
 * A Blueprint graph cannot be overridden from a child, so this reimplements the firing loop in
 * C++ using UShootAbility's public API — GetShootInfo, ModifyAmmo, ApplyGlobalCooldown,
 * GetProjectileCount, GetEffectAreaInfo. Ammo, cooldown, talents, effect areas and the weapon
 * table therefore behave exactly as WeaponModule intends; the only thing that differs is that the
 * aim keeps all three of its components.
 *
 * WeaponModule is not modified. This is a subclass using published entry points.
 */
UCLASS()
class MASSSHOOTERMODULE_API UMassShooterShootAbility : public UShootAbility
{
	GENERATED_BODY()

public:
	UMassShooterShootAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	/**
	 * Extra elevation (uu) added to the aim point, applied at the aim's distance.
	 *
	 * 0 means shots go exactly where the crosshair is. Left exposed because a weapon that lobs
	 * wants a positive value here rather than a hand-edited aim.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Shoot")
	float AimZOffset = 0.f;

	/** Spread passed to the projectile spawn for multi-projectile weapons (shotguns). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Shoot")
	float MultiProjectileSpread = 120.f;

protected:
	/** One round. Called immediately on activation and then by the repeating timer. */
	void FireOneRound();

	/** Where this shot should go, in full 3D. Falls back through several sources. */
	bool ResolveAimPoint(FVector& OutAim) const;

	AUnitBase* GetShootingUnit() const;

	FTimerHandle FireTimer;
};
