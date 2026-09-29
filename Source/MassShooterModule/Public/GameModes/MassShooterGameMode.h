// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameModes/UpgradeGameMode.h"
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
 * Derives AUpgradeGameMode -> AResourceGameMode -> ARTSGameModeBase, because that is where the
 * unit registry lives: AddUnitIndexAndAssignToAllUnitsArray assigns the replicated UnitIndex every
 * client's Mass link resolves through, and NavInitialisation is what makes spawned units pathable.
 * A shooter mode that did not inherit it could not spawn a single RTS bot correctly.
 *
 * AUpgradeGameMode specifically, and not its parent AResourceGameMode, for a reason that has
 * nothing to do with upgrades: ARLAgent::GatherGameState opens with
 * `Cast<AUpgradeGameMode>(GetAuthGameMode())` and RETURNS AN EMPTY FGameStateData if that cast
 * fails. AResourceGameMode is a parent of AUpgradeGameMode, not a child, so a shooter mode derived
 * from it made every RTS AI on the map read zero of every resource and zero units in every control
 * group - measured: the rule decider evaluated all 28 rows every tick and failed each one with
 * "Primary: 0.00 < Thr 40", while 56 correctly tagged worker units stood on the map. The AI looked
 * broken; it was simply being handed an empty world.
 *
 * What it deliberately does NOT inherit is the RTS win condition:
 * ARTSGameModeBase::CheckWinLoseCondition ends the match when a team's units are destroyed, which
 * in a shooter would end the round every time a bot wave died. It is overridden to a no-op and
 * replaced by score/time rules below.
 */
/**
 * One "these two teams do not shoot each other" declaration.
 *
 * RTSUnitTemplate decides hostility per unit pair as
 * `TeamId == OtherTeamId || bIsAllied` (UDetectionProcessor), so on a map that carries several
 * factions, EVERY differing TeamId is a war. That is not a bug to route around - it is how a map
 * with four Xeno nests, a Singularian base and a shooter wave ends up as a seven-way brawl. An
 * alliance is the supported way to say "different faction, same side".
 */
USTRUCT(BlueprintType)
struct FMassShooterTeamAlliance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Teams")
	int32 TeamA = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Teams")
	int32 TeamB = 0;
};

UCLASS(Blueprintable)
class MASSSHOOTERMODULE_API AMassShooterGameMode : public AUpgradeGameMode
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

	/**
	 * Floor for the "field is nearly clear, send the next wave early" shortcut.
	 *
	 * Without it that shortcut can fire on consecutive frames whenever a wave puts nothing on the
	 * field, because the condition it tests stays true. The wave counter - and the difficulty
	 * scaling derived from it - would then run away while the arena is empty.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Waves", meta = (ClampMin = "0.0"))
	float MinSecondsBetweenWaves = 5.f;

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

	/**
	 * Teams that must not fight each other, applied through RTSUnitTemplate's own
	 * UPlayerTeamSubsystem::SetTeamsAllied.
	 *
	 * Team ids must stay below 64: the mask is built as `1LL << TeamId` and ids at or above 64 are
	 * silently dropped, so an alliance naming one would look configured and do nothing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Teams")
	TArray<FMassShooterTeamAlliance> TeamAlliances;

	/**
	 * Server: declare every pair in TeamAlliances. Called once from BeginPlay.
	 *
	 * Once is enough in both directions: SetTeamsAllied writes the mask onto every unit that
	 * already exists, and a unit created afterwards reads its own mask out of the subsystem in
	 * AUnitBase::BeginPlay. The spawner therefore only has to set TeamId before FinishSpawning,
	 * which it does.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Teams")
	void ApplyTeamAlliances();

protected:
	/** Subscribes to a unit's health component so its death reaches HandleUnitDeath. */
	void RegisterUnitForDeathTracking(AActor* Unit);

	/** Applies scoring, the kill feed and the respawn timer for one death. */
	void ScoreKill(AActor* Victim, AActor* Killer);

	/** Finds the team of any actor we might score for/against. */
	static int32 GetActorTeamId(const AActor* Actor);

	/**
	 * Kill-feed name for a victim: the bot's authored name, else RTSUnitTemplate's editable
	 * AUnitBase::Name, else a generic label. Never the raw actor name.
	 */
	static FString ResolveVictimName(const AActor* Victim, const class AMassShooterBot* VictimBot);

	/** Score/time check. Ends the match when either limit is hit. */
	void EvaluateEndConditions();

	/** Collects the level's bot spawners once. */
	void CacheSpawners();

	/** Refreshes AMassShooterGameState::BotsAlive. */
	void RefreshBotCount();

	/** Marks player states owned by an RTS AI commander, so the scoreboard and team balance skip them. */
	void MarkAiPlayerStates();

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
