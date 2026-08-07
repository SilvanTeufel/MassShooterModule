// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GAS/GameplayAbilityBase.h"
#include "MassShooterDashAbility.generated.h"

/**
 * A short burst of speed in the direction the player is moving (or looking, when standing still).
 *
 * Implemented as a launch rather than a teleport so it goes through the CharacterMovement
 * component: collision, ledges and the client's own prediction all keep working, and there is no
 * new movement path to keep in sync. The launch is multicast because the owning client predicts
 * its own movement — a server-only launch would be corrected away within a frame.
 */
UCLASS()
class MASSSHOOTERMODULE_API UMassShooterDashAbility : public UGameplayAbilityBase
{
	GENERATED_BODY()

public:
	UMassShooterDashAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	/** Horizontal launch speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Dash")
	float DashSpeed = 1500.f;

	/** Upward component, so a dash clears small steps instead of stubbing on them. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Dash")
	float DashLift = 260.f;
};
