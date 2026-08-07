// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameStates/ResourceGameState.h"
#include "Core/MassShooterTypes.h"
#include "MassShooterGameState.generated.h"

/**
 * Everything every client needs to draw the same match.
 *
 * Derives AResourceGameState (RTSUnitTemplate's own game state) rather than AGameStateBase so a
 * project can keep using the RTS resource/team plumbing alongside the shooter rules — several of
 * RTSUnitTemplate's systems cast the game state to this type, and inheriting costs nothing.
 */
UCLASS()
class MASSSHOOTERMODULE_API AMassShooterGameState : public AResourceGameState
{
	GENERATED_BODY()

public:
	AMassShooterGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Match")
	EMassShooterMatchPhase MatchPhase = EMassShooterMatchPhase::Warmup;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Match")
	EMassShooterMatchMode MatchMode = EMassShooterMatchMode::TeamDeathmatch;

	/** Server world time the current phase ends. Clients count down against server-synced time. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Match")
	float PhaseEndTime = 0.f;

	/** Score needed to win. 0 = no score limit (time only). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Match")
	int32 ScoreLimit = 50;

	/** Team that won, once MatchPhase is PostMatch. -1 while undecided or on a draw. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Match")
	int32 WinningTeamId = -1;

	/** Current wave number in Survival (and in any mode that spawns waves). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Match")
	int32 CurrentWave = 0;

	/** Live bot count, so the HUD can show "12 hostiles remaining". */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Match")
	int32 BotsAlive = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Match")
	TArray<FMassShooterTeamScore> TeamScores;

	/** Newest first, capped at MaxKillFeedEntries. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Match")
	TArray<FMassShooterKillFeedEntry> KillFeed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MassShooter|Match")
	int32 MaxKillFeedEntries = 6;

	// ---- Server-side mutators ----------------------------------------------------------------

	/** Adds Delta to a team's score, creating the row if this team has not scored yet. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Match")
	void AddTeamScore(int32 TeamId, int32 Delta, bool bCountAsKill);

	/** Overwrites how many capture points a team holds (Domination HUD). */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Match")
	void SetTeamHeldPoints(int32 TeamId, int32 HeldPoints);

	UFUNCTION(BlueprintCallable, Category = "MassShooter|Match")
	void PushKillFeed(const FMassShooterKillFeedEntry& Entry);

	/** Ensures a score row exists for this team (so it appears on the scoreboard at 0). */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Match")
	void RegisterTeam(int32 TeamId);

	// ---- Queries -----------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MassShooter|Match")
	int32 GetTeamScore(int32 TeamId) const;

	/** Highest-scoring team, or -1 when nobody has scored or the lead is tied. */
	UFUNCTION(BlueprintPure, Category = "MassShooter|Match")
	int32 GetLeadingTeam() const;

	/** Seconds left in the current phase, floored at 0. */
	UFUNCTION(BlueprintPure, Category = "MassShooter|Match")
	float GetPhaseTimeRemaining() const;
};
