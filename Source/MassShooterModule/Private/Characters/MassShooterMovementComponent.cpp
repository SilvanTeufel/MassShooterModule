// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Characters/MassShooterMovementComponent.h"

UMassShooterMovementComponent::UMassShooterMovementComponent()
{
	// AUnitBase's constructor enables RVO for RTS crowd flow. A player should push through
	// friendlies under their own input, not be steered around them by an avoidance solver.
	bUseRVOAvoidance = false;
	SetAvoidanceEnabled(false);

	MaxWalkSpeed = BaseWalkSpeed;
	MaxWalkSpeedCrouched = 260.f;
	MaxAcceleration = 2400.f;
	BrakingDecelerationWalking = 2200.f;
	GroundFriction = 8.f;

	JumpZVelocity = 520.f;
	AirControl = 0.35f;
	BrakingDecelerationFalling = 200.f;

	NavAgentProps.bCanCrouch = true;
	NavAgentProps.bCanJump = true;

	// The RTS unit CDO leaves the pawn standing still (its position comes from Mass). A player
	// walks, so state the land mode explicitly rather than relying on the inherited default.
	DefaultLandMovementMode = MOVE_Walking;

	// Movement replication is the standard ACharacter path (ServerMove from the owner,
	// ReplicatedMovement to simulated proxies) and UCharacterMovementComponent already opts into
	// it. Calling SetIsReplicated here would ensure-fail — during construction the API to use is
	// SetIsReplicatedByDefault, and the base class has already done it. What DOES need undoing is
	// AUnitBase::BeginPlay's SetReplicateMovement(false), which AMassShooterCharacter::BeginPlay
	// reverses on the actor.
}

void UMassShooterMovementComponent::ApplyStanceSpeed(bool bSprinting, bool bAiming)
{
	// Aiming wins over sprinting: a player holding both should be in the accurate, slow stance.
	MaxWalkSpeed = bAiming ? AimWalkSpeed : (bSprinting ? SprintSpeed : BaseWalkSpeed);
}
