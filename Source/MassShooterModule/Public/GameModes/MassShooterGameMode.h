// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameModes/ResourceGameMode.h"
#include "Core/MassShooterTypes.h"
#include "MassShooterGameMode.generated.h"

class AMassShooterCharacter;
class AMassShooterBot;
class AMassShooterBotSpawner;
class AMassShooterPlayerState;
class AMassShooterGameState;
class AMassShooterPlayerStart;
class AUnitBase;
class UMassShooterHealthComponent;

/**
 * The match director.
 *
 * Derives AResourceGameMode -> ARTSGameModeBase, because that is where the unit registry lives:
 * AddUnitIndexAndAssignToAllUnitsArray assigns the replicated UnitIndex every client's Mass link
 * resolves through, and NavInitialisation is what makes spawned units pathable. A shooter mode
 * that did not inherit it could not spawn a single RTS bot correctly.
 *
 * What it deliberately does NOT inherit is the RTS win condition:
 * ARTSGameModeBase::CheckWinLoseCondition ends the match when a team's units are destroyed, which
 * in a shooter would end the round every time a bot wave died. It is overridden to a no-op and
 * replaced by score/time rules below.
 */
UCLASS(Blueprintable)
class MASSSHOOTERMODULE_API AMassShooterGameMode : public AResourceGameMode
{
	GENERATED_BODY()

public:
	AMassShooterGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/**
	 * Neutralised on purpose — see the class comment. The RTS rule ("your units are gone, you
	 * lose") is the wrong question in a mode where units respawn by design.
	 */
	virtual void CheckWinLoseCondition(AUnitBase* DestroyedUnit = nullptr) override;

	// ---- Rules -------------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Rules")
	EMassShooterMatchMode MatchMode = EMassShooterMatchMode::TeamDeathmatch;

	/** Team score that ends the match. 0 = time limit only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Rules")
	int32 ScoreLimit = 50;

	/** Match length in seconds. 0 = score limit only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Rules")
	float MatchSeconds = 600.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Rules")
	float WarmupSeconds = 10.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Rules")
	float PostMatchSeconds = 15.f;

	/** Score awarded for killing an enemy player. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Rules")
	int32 ScorePerPlayerKill = 2;

	/** Score awarded for killing a bot (also used as the fallback when a bot has no ScoreValue). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Rules")
	int32 ScorePerBotKill = 1;

	/** Score deducted for a team kill. Negative values are the point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Rules")
	int32 ScorePerTeamKill = -2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Rules")
	float RespawnDelay = 5.f;

	/** Restart the match automatically when PostMatch ends, instead of just sitting there. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Rules")
	bool bRestartAfterPostMatch = true;

	// ---- Waves -------------------------------------------------------------------------------

	/** Send bot waves. Always on in Survival; optional flavour in the PvP modes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Waves")
	bool bSpawnBotWaves = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Waves")
	int32 BotsInFirstWave = 6;

	/** Extra bots added to each subsequent wave. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Waves")
	int32 BotsAddedPerWave = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Waves")
	int32 MaxBotsAlive = 60;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Waves")
	float SecondsBetweenWaves = 25.f;

	/** Bot health/damage multiplier compounded per wave (1.08 = +8% each wave). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Waves")
	float WaveScalingPerWave = 1.08f;

	// ---- API ---------------------------------------------------------------------------------

	/** Server: called by a health component when any registered unit dies. */
	UFUNCTION()
	void HandleUnitDeath(AActor* Victim, AActor* Killer);

	/** Server: respawn this controller's pawn now if its delay has elapsed. Returns success. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Match")
	bool TryRespawnPlayer(AController* Controller);

	/** Server: send the next wave immediately. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Waves")
	void SpawnNextWave();

	/** Server: move to a phase and stamp its end time. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Match")
	void SetMatchPhase(EMassShooterMatchPhase NewPhase);

	/** Server: reset scores, respawn everyone, go back to Warmup. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Match")
	void RestartMatch();

	/**
	 * The team a newly joined player is put on: the smallest human team.
	 *
	 * Joining is passed in and excluded from the count. By PostLogin its PlayerState is already in
	 * PlayerArray carrying the default team, so counting it makes the very first player look like
	 * team 1 is occupied and puts them on team 2 — with team 1's spawn points then unusable.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Teams")
	int32 PickTeamForNewPlayer(AController* Joining) const;

protected:
	/** Subscribes to a unit's health component so its death reaches HandleUnitDeath. */
	void RegisterUnitForDeathTracking(AActor* Unit);

	/** Applies scoring, the kill feed and the respawn timer for one death. */
	void ScoreKill(AActor* Victim, AActor* Killer);

	/** Finds the team of any actor we might score for/against. */
	static int32 GetActorTeamId(const AActor* Actor);

	/** Score/time check. Ends the match when either limit is hit. */
	void EvaluateEndConditions();

	/** Collects the level's bot spawners once. */
	void CacheSpawners();

	/** Refreshes AMassShooterGameState::BotsAlive. */
	void RefreshBotCount();

	/** Server: respawn every player at a start point (used on match restart). */
	void RespawnAllPlayers();

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMassShooterBotSpawner>> Spawners;

	/** Units already subscribed, so a re-registration does not double-count a kill. */
	UPROPERTY(Transient)
	TSet<TObjectPtr<AActor>> TrackedUnits;

	float WaveTimer = 0.f;
	float BotScanTimer = 0.f;
	float DeathScanTimer = 0.f;
};
