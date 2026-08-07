// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GAS/GameplayAbilityBase.h"
#include "MassShooterGrenadeAbility.generated.h"

class AEffectArea;

/**
 * Throw a grenade at the crosshair.
 *
 * Built entirely out of RTSUnitTemplate's own explosion machinery: after a fuse, it calls
 * AUnitBase::SpawnEffectArea at the aim point, which spawns a Mass-driven AEffectArea that damages
 * everything in radius through the normal damage pipeline. That is the same code an RTS artillery
 * ability uses, so grenades hurt bots, players and buildings consistently and need no new damage
 * path.
 *
 * The aim point comes from UGameplayAbilityBase::GetTargetLocation(), which reads the target
 * location the activation wrote from the hit result — so the throw lands where the player was
 * looking, not at their feet.
 */
UCLASS()
class MASSSHOOTERMODULE_API UMassShooterGrenadeAbility : public UGameplayAbilityBase
{
	GENERATED_BODY()

public:
	UMassShooterGrenadeAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	/** The explosion. Required — without it the ability throws nothing and ends immediately. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Grenade")
	TSubclassOf<AEffectArea> EffectAreaClass;

	/** Seconds between the throw and the detonation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Grenade")
	float FuseSeconds = 1.6f;

	/** Scale applied to the spawned effect area — its radius comes from the class, this tunes it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Grenade")
	FVector ExplosionScale = FVector(3.f, 3.f, 3.f);

	/** Furthest the grenade can be thrown; a longer aim is clamped to this along the aim direction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Grenade")
	float MaxThrowDistance = 2500.f;

	/** Consume a charge from the thrower's loadout component. Off = unlimited grenades. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Grenade")
	bool bConsumeGrenadeCharge = true;
};
