// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "MassShooterPlayerStart.generated.h"

/**
 * A team-owned spawn point.
 *
 * RTSUnitTemplate ships APlayerStartBase, which carries RTS per-player customisation (team id,
 * default waypoint, camera setup). A shooter spawn needs almost none of that but does need one
 * thing APlayerStartBase has no concept of: enemy proximity, so nobody spawns into a gunfight.
 * Deriving APlayerStart directly keeps this actor to exactly what it is.
 *
 * TeamId 0 means "any team may use this" — useful for free-for-all and Survival maps.
 */
UCLASS(Blueprintable)
class MASSSHOOTERMODULE_API AMassShooterPlayerStart : public APlayerStart
{
	GENERATED_BODY()

public:
	AMassShooterPlayerStart(const FObjectInitializer& ObjectInitializer);

	/** Team allowed to spawn here. 0 = any team. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawn")
	int32 TeamId = 0;

	/** Turn off to retire a spawn without deleting it (e.g. a captured zone). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawn")
	bool bEnabled = true;

	/**
	 * An enemy within this radius makes the spawn unsafe, and the selector prefers another one.
	 * If EVERY start is unsafe it still spawns at the least-bad option rather than refusing —
	 * a player waiting forever is worse than a contested spawn.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawn")
	float EnemyAvoidanceRadius = 1500.f;
};
