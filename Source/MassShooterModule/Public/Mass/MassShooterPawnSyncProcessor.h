// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "MassShooterPawnSyncProcessor.generated.h"

/**
 * Pushes a shooter player pawn's ACTOR transform into its Mass entity.
 *
 * RTSUnitTemplate syncs the other direction for units (fragment -> actor, via
 * UActorTransformSyncProcessor) and only ever writes FTransformFragment from an actor for
 * AEffectArea. A shooter player is moved by its CharacterMovementComponent, and its entity has
 * had FUnitMassTag removed, so nothing in the base plugin would ever update its entity
 * transform — bots would forever perceive it standing at its spawn point.
 *
 * This processor closes that gap and nothing else. Everything the bots need beyond position
 * (TeamId, Health, MaxHealth, alliance mask) is already synced from the actor by
 * UUnitActorToFragmentSyncProcessor, which does NOT require FUnitMassTag.
 *
 * It also maintains two things on the player's entity that the base plugin stops maintaining once
 * FUnitMassTag is gone: the ground height the muzzle position is derived from, and the absence of
 * the RTS rotate-to-mouse tag. Both are documented at their sites in Execute.
 *
 * Runs on server, client and standalone: a client's own bot-perception queries and its HUD
 * ranging read the same fragments.
 */
UCLASS()
class MASSSHOOTERMODULE_API UMassShooterPawnSyncProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassShooterPawnSyncProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;

	/** Shooter pawns that have picked up FMassRotateToMouseTag from an ability activation. */
	FMassEntityQuery RotateToMouseQuery;
};
