// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassShooterTypes.generated.h"

/** Which rule set the match runs. Selected on the game mode (and overridable per level). */
UENUM(BlueprintType)
enum class EMassShooterMatchMode : uint8
{
	/** First team to reach ScoreLimit kills wins. Bots count for whoever shot them. */
	TeamDeathmatch UMETA(DisplayName = "Team Deathmatch"),

	/** Holding capture points ticks score over time. Kills are worth a small amount. */
	Domination UMETA(DisplayName = "Domination"),

	/** All players share one team and survive escalating bot waves. */
	Survival UMETA(DisplayName = "Survival")
};

/** Coarse match lifecycle. Replicated on the game state so every client draws the same banner. */
UENUM(BlueprintType)
enum class EMassShooterMatchPhase : uint8
{
	/** Players may spawn and move, scoring is disabled, waiting for MinPlayers / WarmupSeconds. */
	Warmup UMETA(DisplayName = "Warmup"),

	/** The real match. */
	InProgress UMETA(DisplayName = "In Progress"),

	/** Scoring frozen, winner decided, waiting for PostMatchSeconds before restart/travel. */
	PostMatch UMETA(DisplayName = "Post Match")
};

/** Capture-point ownership state, replicated to drive the HUD objective bar. */
UENUM(BlueprintType)
enum class EMassShooterCaptureState : uint8
{
	Neutral    UMETA(DisplayName = "Neutral"),
	Contested  UMETA(DisplayName = "Contested"),
	Capturing  UMETA(DisplayName = "Capturing"),
	Owned      UMETA(DisplayName = "Owned")
};

/** What a pickup restores when a player walks over it. */
UENUM(BlueprintType)
enum class EMassShooterPickupKind : uint8
{
	Ammo   UMETA(DisplayName = "Ammo"),
	Health UMETA(DisplayName = "Health"),
	Shield UMETA(DisplayName = "Shield")
};

/**
 * One line of the kill feed. Kept as plain strings + a team id rather than actor pointers so it
 * survives the killer/victim being destroyed and costs nothing to replicate.
 */
USTRUCT(BlueprintType)
struct MASSSHOOTERMODULE_API FMassShooterKillFeedEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MassShooter")
	FString KillerName;

	UPROPERTY(BlueprintReadOnly, Category = "MassShooter")
	FString VictimName;

	UPROPERTY(BlueprintReadOnly, Category = "MassShooter")
	FString WeaponName;

	/** Team of the killer, so the feed can tint friendly vs. enemy kills. */
	UPROPERTY(BlueprintReadOnly, Category = "MassShooter")
	int32 KillerTeamId = 0;

	/** True when the victim was an AI bot rather than a player. */
	UPROPERTY(BlueprintReadOnly, Category = "MassShooter")
	bool bVictimWasBot = false;

	/** Server world time the kill happened; the HUD fades the entry out relative to this. */
	UPROPERTY(BlueprintReadOnly, Category = "MassShooter")
	float ServerTime = 0.f;
};

/** Replicated per-team score row. Teams are sparse (a level may use 1, 2 or 4), hence an array. */
USTRUCT(BlueprintType)
struct MASSSHOOTERMODULE_API FMassShooterTeamScore
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MassShooter")
	int32 TeamId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MassShooter")
	int32 Score = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MassShooter")
	int32 Kills = 0;

	/** How many capture points this team currently holds (Domination). */
	UPROPERTY(BlueprintReadOnly, Category = "MassShooter")
	int32 HeldPoints = 0;
};
