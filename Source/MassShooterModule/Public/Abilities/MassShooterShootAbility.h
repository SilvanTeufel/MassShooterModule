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
	/**
	 * Wie lange nach einem Schuss die Angriffsanimation gezeigt wird (Sekunden).
	 *
	 * Wofuer: die Schussanimation des WeaponModule haengt NICHT an einer Montage - in
	 * BP_ShootAbility_AH sind WindupMontage und FireMontage leer. Sie kommt aus dem Zustand
	 * UnitData::Attack, den der AnimBP ueber CharAnimState liest. Der MassShooter steht aber
	 * dauerhaft auf UnitData::Idle, weil er seine Bewegung selbst fuehrt.
	 *
	 * Gesetzt wird deshalb nur der ANZEIGE-Zustand des AnimInstance
	 * (UUnitBaseAnimInstance::SetAnimStateOverride), nicht der echte Zustand der Einheit: ein
	 * echter Wechsel wuerde die Mass-Zustandsprozessoren mitziehen und die Figur beim Feuern
	 * anhalten.
	 *
	 * Der Wert sollte etwas ueber dem Feuertakt liegen, damit die Animation zwischen zwei
	 * Schuessen nicht zurueckspringt.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Shoot", meta = (ClampMin = "0.0"))
	float AttackAnimHoldSeconds = 0.35f;

	/**
	 * Anteil des Feuertakts, den der Attack-Zustand gehalten wird (0..1).
	 *
	 * Die Schussanimation lebt vom WECHSEL Attack -> zurueck -> Attack: der Zustandsautomat
	 * startet Fire_A_Slow bei jedem Eintritt neu. Haelt Attack laenger an als der Feuertakt,
	 * wird er nachgesetzt bevor er auslaeuft - der Zustand bleibt dann dauerhaft Attack und die
	 * Animation spielt genau EINMAL. Genau das war der erste Versuch: feste 0.35 s gegen
	 * Feuerraten von 0.25-0.3 s.
	 *
	 * AttackAnimHoldSeconds wirkt weiterhin als Obergrenze.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Shoot", meta = (ClampMin = "0.05", ClampMax = "0.95"))
	float AttackAnimDutyCycle = 0.5f;

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

	/**
	 * Setzt oder entfernt FMassStateContinuousAttackTag auf der Mass-Entitaet der Einheit.
	 *
	 * Das ist der Weg, den auch RTSUnitTemplate geht - nur an einer anderen Stelle. Dort setzt
	 * AExtendedControllerBase::BatchSetRotateToMouseTagLocally (bzw. UGameplayAbilityBase bei
	 * bIsContinuousAbility) diesen Tag, und daran haengen ZWEI Dinge:
	 *   - UUnitClientTagSyncProcessor::ComputeState liefert dann zyklisch ContinousAttack und Aim
	 *     im Takt von AAbilityUnit::ContinuousAttackDuration statt eines einmaligen Attack,
	 *   - UUnitAnimationProcessor rechnet PlayRate und AnimationPosition auf denselben Takt.
	 *
	 * Wir setzen hier NUR den Tag und lassen bIsContinuousAbility bewusst auf false: das Flag
	 * wuerde zusaetzlich den RTS-Feuerpfad in UGameplayAbilityBase aktivieren, der auf ein
	 * wahrgenommenes Ziel feuert statt auf das Zielkreuz. Gebraucht wird von den beiden Wirkungen
	 * also nur die Anzeige.
	 *
	 * Beim Setzen wird StateTimerClient genullt, sonst startet der Zyklus mitten in einer
	 * Schussphase - genauso macht es der Controller.
	 */
	void SetContinuousAttackTag(bool bAdd);

	FTimerHandle FireTimer;
};
