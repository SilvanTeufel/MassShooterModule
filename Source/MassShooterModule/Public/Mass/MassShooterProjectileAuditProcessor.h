// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "MassShooterProjectileAuditProcessor.generated.h"

/**
 * Diagnostic only: reports what live Mass projectiles are actually doing.
 *
 * Every earlier attempt to explain "projectiles will not fly up or down" measured an INPUT — the
 * aim point, the value the ability reads, the direction implied by muzzle and target. All of those
 * came out correct while the complaint persisted, which means the answer is downstream of them.
 * This reads the projectile's own FMassProjectileFragment: the direction it is travelling and the
 * height it is at, sampled as it flies.
 *
 * Costs nothing unless Shooter.Debug.LogProjectiles is on.
 */
UCLASS()
class MASSSHOOTERMODULE_API UMassShooterProjectileAuditProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassShooterProjectileAuditProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;

	float LastLogTime = -100.f;
};
