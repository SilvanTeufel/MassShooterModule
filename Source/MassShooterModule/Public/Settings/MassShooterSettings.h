// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MassShooterSettings.generated.h"

/**
 * Project Settings -> Plugins -> Mass Shooter Module.
 *
 * Only values a project would want to change globally live here; per-match rules (score limit,
 * time limit, wave sizes) belong on the game mode so a level can override them.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Mass Shooter Module"))
class MASSSHOOTERMODULE_API UMassShooterSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMassShooterSettings();

	static const UMassShooterSettings* Get();

	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }

	/**
	 * Team id assigned to the first human team. RTSUnitTemplate treats team 0 as "no team" in
	 * several places, so shooter teams start at 1.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Teams", meta = (ClampMin = "1"))
	int32 FirstPlayerTeamId = 1;

	/** How many human teams the auto-balancer distributes joining players across. */
	UPROPERTY(Config, EditAnywhere, Category = "Teams", meta = (ClampMin = "1", ClampMax = "8"))
	int32 NumPlayerTeams = 2;

	/** Team id used by AI bots that fight every human team. */
	UPROPERTY(Config, EditAnywhere, Category = "Teams")
	int32 HostileBotTeamId = 9;

	/**
	 * Max distance of the aim trace that turns "where the crosshair points" into a world point for
	 * the WeaponModule shoot ability.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Combat", meta = (ClampMin = "1000.0"))
	float MaxAimTraceDistance = 100000.f;

	/** Seconds a corpse stays before the player is put back at a spawn point. */
	UPROPERTY(Config, EditAnywhere, Category = "Combat", meta = (ClampMin = "0.0"))
	float DefaultRespawnDelay = 5.f;

	/** Seconds of damage immunity after respawning. */
	UPROPERTY(Config, EditAnywhere, Category = "Combat", meta = (ClampMin = "0.0"))
	float SpawnProtectionSeconds = 2.f;

	/** Draw the built-in Canvas HUD. Turn off if a project ships its own UMG HUD. */
	UPROPERTY(Config, EditAnywhere, Category = "HUD")
	bool bDrawDefaultHUD = true;
};
