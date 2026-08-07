// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MassShooterMovementComponent.generated.h"

/**
 * The shooter player's mover.
 *
 * MMOModule deliberately went the other way — it nailed the CMC shut and drove the pawn from a
 * fixed-step Mass processor with hand-written prediction — because it had to hold 250 players in
 * one zone. A shooter match is tens of players, not hundreds, and it needs sub-frame-accurate
 * strafe/jump/crouch feel plus rollback that already handles ledges, slopes and stairs. That is
 * exactly what UCharacterMovementComponent is, so this module uses it as intended rather than
 * reimplementing it.
 *
 * AUnitBase's constructor turns RVO avoidance on (right for a crowd of RTS units, wrong for a
 * player who should be able to body-block), so that is switched back off here. Everything else
 * is shooter tuning: a real air-control value, no ground friction cliff on landing, and a
 * sprint/crouch speed pair the character drives through RequestedSpeedScale.
 */
UCLASS()
class MASSSHOOTERMODULE_API UMassShooterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UMassShooterMovementComponent();

	/** Walk speed used when neither sprinting nor crouching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Movement")
	float BaseWalkSpeed = 520.f;

	/** Walk speed while the sprint input is held and the player is moving forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Movement")
	float SprintSpeed = 820.f;

	/** Walk speed while aiming down sights. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Movement")
	float AimWalkSpeed = 300.f;

	/**
	 * Applies the right MaxWalkSpeed for the current stance. Called by the character whenever a
	 * stance input changes — not every tick, so a designer overriding MaxWalkSpeed elsewhere is
	 * not fought frame by frame.
	 */
	void ApplyStanceSpeed(bool bSprinting, bool bAiming);
};
