// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassShooterFragments.generated.h"

/**
 * Marks a Mass entity whose transform is owned by an actor's CharacterMovementComponent instead
 * of by the RTS locomotion processors.
 *
 * Added (and FUnitMassTag removed) in FMassShooterModule::OnMassArchetypeBuilding for shooter
 * player pawns. UMassShooterPawnSyncProcessor keys off this tag alone, so it never touches a
 * normal RTS unit.
 */
USTRUCT()
struct MASSSHOOTERMODULE_API FMassShooterPawnTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * Small per-pawn state the sync processor keeps between frames.
 *
 * Only the last pushed location is needed: the push is skipped while the actor has not moved,
 * which keeps a standing player out of the write path entirely.
 */
USTRUCT()
struct MASSSHOOTERMODULE_API FMassShooterPawnFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Last location written into the entity's FTransformFragment. */
	UPROPERTY()
	FVector LastPushedLocation = FVector(FLT_MAX);

	/** Last yaw written, in degrees. */
	UPROPERTY()
	float LastPushedYaw = FLT_MAX;
};
