// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "MassShooterPlayerState.generated.h"

/**
 * Per-player match record, replicated to everyone.
 *
 * RTSUnitTemplate has no PlayerState — it keeps per-player data on the controller
 * (SelectableTeamId, AlliedTeamsMask), which only ever reaches its owner. That is fine for an RTS
 * where you never need to know another player's stats. A shooter has a scoreboard, nameplates and
 * a kill feed, all of which are about players you do not own, so this class exists.
 */
UCLASS()
class MASSSHOOTERMODULE_API AMassShooterPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AMassShooterPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Carries the match record across a seamless travel / map change. */
	virtual void CopyProperties(APlayerState* PlayerState) override;

	/** Which team this player fights for. Mirrored onto the controller and the pawn. */
	/**
	 * True for a player state that belongs to an RTS AI commander, not a person.
	 *
	 * ARTSGameModeBase creates a full APlayerController for every PlayerStart marked bIsAi, and the
	 * engine gives each one a PlayerState of the game mode's PlayerStateClass - so on Level_14 the
	 * four Xeno AIs and the allied Singularian AI each add a row to GameState::PlayerArray. Left
	 * unmarked they showed up as five empty scoreboard lines on the human's screen and counted
	 * toward team balancing.
	 *
	 * The flag lives HERE and replicates on purpose: AControllerBase::bIsAi is not replicated, and
	 * the AI controllers do not exist on a client at all - so a client-side scoreboard cannot ask
	 * the controller. It can only ask the player state, which always replicates.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter")
	bool bIsAiPlayer = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter")
	int32 ShooterTeamId = 1;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter")
	int32 Kills = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter")
	int32 Deaths = 0;

	/** Bots killed. Tracked apart from player kills so the scoreboard can show both. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter")
	int32 BotKills = 0;

	/** Match score. Kills, objective ticks and captures all feed this. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter")
	int32 MatchScore = 0;

	/** Consecutive kills without dying. Reset on death. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter")
	int32 KillStreak = 0;

	/** Best streak this match. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter")
	int32 BestKillStreak = 0;

	/** Server world time this player may respawn at. The HUD counts down to it. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter")
	float RespawnAvailableTime = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter")
	bool bAwaitingRespawn = false;

	// ---- Server-side mutators ----------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "MassShooter")
	void AddKill(bool bVictimWasBot, int32 ScoreValue);

	UFUNCTION(BlueprintCallable, Category = "MassShooter")
	void AddDeath();

	UFUNCTION(BlueprintCallable, Category = "MassShooter")
	void AddScore(int32 Delta);

	UFUNCTION(BlueprintCallable, Category = "MassShooter")
	void SetShooterTeam(int32 NewTeamId);

	/** Server: mark this player dead and stamp when they may come back. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter")
	void BeginRespawnWait(float AvailableAtServerTime);

	UFUNCTION(BlueprintCallable, Category = "MassShooter")
	void EndRespawnWait();

	/** Kill/death ratio for the scoreboard; deaths of 0 return the kill count. */
	UFUNCTION(BlueprintPure, Category = "MassShooter")
	float GetKDRatio() const;
};
