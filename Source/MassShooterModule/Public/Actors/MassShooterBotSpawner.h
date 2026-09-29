// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/UnitData.h"
#include "MassShooterBotSpawner.generated.h"

class AUnitBase;

/**
 * Spawns hostile bots, either as a steady population or in escalating waves.
 *
 * Uses RTSUnitTemplate's documented runtime-spawn sequence, which is deliberately not a single
 * call: deferred spawn -> set team/state -> FinishSpawning -> InitializeAttributes ->
 * AddUnitIndexAndAssignToAllUnitsArray -> ScheduleDelayedNavigationUpdate. Team and state must be
 * set BEFORE FinishSpawning because the unit's BeginPlay (which builds its Mass entity) reads
 * them, and the UnitIndex assignment is what puts the unit into the replicated registry every
 * client's Mass link resolves through. Skipping any step produces a unit that exists but is
 * invisible, unregistered, or stuck off the navmesh.
 *
 * Server-only: clients receive the bots through normal replication.
 */
UCLASS(Blueprintable)
class MASSSHOOTERMODULE_API AMassShooterBotSpawner : public AActor
{
	GENERATED_BODY()

public:
	AMassShooterBotSpawner();

	virtual void BeginPlay() override;

	/**
	 * Spawn table: rows of RTSUnitTemplate's FUnitSpawnParameter.
	 *
	 * When set, this is the authority on what spawns — the row supplies the unit class, the state
	 * to spawn in, and the WaypointTag naming the AWaypoint the bot patrols around. That is
	 * RTSUnitTemplate's own mechanism (ARTSGameModeBase::AssignWaypointToUnit matches a row's
	 * WaypointTag against AWaypoint::Tag), so waypoints are placed and named in the level exactly
	 * as they are for any other RTS unit rather than through something specific to this plugin.
	 *
	 * UnitCount is read as a RELATIVE WEIGHT, not a literal count: this spawner is driven by
	 * waves and population targets that decide how many bots to make, so the table only decides
	 * the mix. Melee 4 / ranged 1 gives the 80/20 split the example ships with.
	 *
	 * Leave unset to fall back to BotClass / MeleeBotClass / MeleeShare below.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner")
	TObjectPtr<UDataTable> SpawnTable;

	/** Ranged unit class. Used when SpawnTable is unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner")
	TSubclassOf<AUnitBase> BotClass;

	/** Melee unit class. Falls back to BotClass when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner")
	TSubclassOf<AUnitBase> MeleeBotClass;

	/**
	 * Share of spawns that use MeleeBotClass, 0..1.
	 *
	 * Melee-heavy on purpose: a crowd that has to close the distance is what gives a shooter its
	 * pacing, while a field of ranged units turns every engagement into unavoidable crossfire.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MeleeShare = 0.8f;

	/**
	 * Where bots head when they have not seen anyone yet.
	 *
	 * Without this they patrol around their own spawn point, which in a map this size means they
	 * never reach anybody: measured with bots idling 11700 uu from the player against a 7000 uu
	 * sight radius, standing still with no target for the whole match. Leave it unset and the
	 * spawner aims at the average of the level's player starts, so bots advance on the fight.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner|Advance")
	TObjectPtr<AActor> AdvanceTarget;

	/** Radius the patrol wanders within once it has arrived at the advance point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner|Advance")
	float AdvanceWanderRadius = 2500.f;

	/**
	 * Give spawned units that are not AMassShooterBot the shooter's health component.
	 *
	 * The spawn table takes any AUnitBase, which is the point: a project that already has RTS
	 * units (the Xeno faction, say) should be able to field them as the shooter's hostiles without
	 * reparenting a single Blueprint. What those units lack is UMassShooterHealthComponent, and
	 * that component is the entire death path of this module - the game mode subscribes to its
	 * OnDeath to run the kill feed and the scoreboard, and its per-second sweep picks up whatever
	 * carries one. Attaching it here is therefore the difference between a wave that scores and a
	 * wave that merely dies quietly.
	 *
	 * The attached component is set to observe only (bOverrideStatsOnStart = false), so the unit
	 * keeps the health its own attribute table gave it. Wave scaling is applied separately, on the
	 * attribute set, for exactly the same reason.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner")
	bool bAdaptForeignUnits = true;

	/**
	 * Sight radius forced onto adapted foreign units.
	 *
	 * This is the single reason a wave of RTS units looks like "nothing spawned". An RTS unit is
	 * tuned for a commander who walks it into contact, so it sees about 900 uu; measured on the
	 * Xenocrypta Skitterling. A shooter's hostiles spawn at the map edge and have to cross the
	 * arena on their own. At 900 uu they march to their advance point, arrive, drop into
	 * PatrolIdle with no target, and stand there forever while the player - 6100 uu away in his
	 * corner - never sees a single one.
	 *
	 * AMassShooterBot solves this for itself in BeginPlay (SightRadiusOverride). A foreign unit has
	 * no such override, so the spawner applies it, and it MUST happen before FinishSpawning: the
	 * binding component's SightRadius is copied into FMassCombatStatsFragment when the unit's
	 * BeginPlay builds the Mass entity, so a later write never reaches the entity.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner")
	float ForeignUnitSightRadius = 7000.f;

	/** Distance at which an adapted foreign unit drops an acquired target. Kept above the sight radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner")
	float ForeignUnitLoseSightRadius = 9000.f;

	/** Team the spawned bots fight for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner")
	int32 BotTeamId = 9;

	/** Radius around this actor bots appear in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner")
	float SpawnRadius = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner")
	float SpawnZOffset = 120.f;

	/**
	 * Initial RTS state. PatrolRandom makes bots wander toward the advance point instead of
	 * standing where they spawned; Idle leaves them motionless until something walks into their
	 * sight radius.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner")
	TEnumAsByte<UnitData::EState> InitialState = UnitData::PatrolRandom;

	/**
	 * Continuous mode: keep this many bots alive at all times, re-checked every RespawnInterval.
	 * Set 0 and drive SpawnWave() from the game mode for a wave-based match instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner|Continuous")
	int32 MaxAlive = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner|Continuous")
	float RespawnInterval = 6.f;

	/** Whether this spawner participates in game-mode-driven waves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Spawner|Waves")
	bool bUseForWaves = true;

	/** Server: spawn Count bots at once, scaled by the given multipliers. Returns how many landed. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Spawner")
	int32 SpawnWave(int32 Count, float HealthMultiplier, float DamageMultiplier);

	/** Server: spawn a single bot. Null when the class or game mode is missing. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Spawner")
	AUnitBase* SpawnOne(float HealthMultiplier = 1.f, float DamageMultiplier = 1.f);

	/** Bots from this spawner that are still alive. Also prunes the tracking list. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Spawner")
	int32 GetAliveCount();

protected:
	/** Continuous mode top-up. */
	void TopUp();

	/** Bots this spawner created. Weak, so a despawned unit simply drops out. */
	TArray<TWeakObjectPtr<AUnitBase>> Spawned;

	/** Diagnostic: samples each bot's state and distance moved. See Shooter.Debug.LogBots. */
	void AuditBots();

	FTimerHandle AuditTimer;

	/** Seconds between audit samples; see Shooter.Debug.LogBots. */
	float AuditInterval = 2.f;

	/** Previous audit sample per bot, so "did it actually move" is a measurement not a guess. */
	TMap<TWeakObjectPtr<AUnitBase>, FVector> LastAuditedLocations;

	/**
	 * Picks the next spawn-table row, weighted by UnitCount.
	 *
	 * Counted rather than rolled: at a 4:1 weighting a random draw still hands out runs of four
	 * ranged bots, and a wave of six is far too small to average that out.
	 */
	const struct FUnitSpawnParameter* PickSpawnRow();

	/**
	 * Makes a spawned non-AMassShooterBot unit participate in the shooter's match systems.
	 * See bAdaptForeignUnits.
	 */
	void AdaptForeignUnit(AUnitBase* Unit, float HealthMultiplier, float DamageMultiplier);

	/**
	 * Perception and detection for a foreign unit. Separate from AdaptForeignUnit because it has to
	 * run BEFORE FinishSpawning - see ForeignUnitSightRadius.
	 */
	void PrepareForeignUnitPerception(AUnitBase* Unit);

	/** Resolves (and, on first use, creates) the fallback waypoint used when no tag matches. */
	class AWaypoint* GetOrCreateAdvanceWaypoint();

	UPROPERTY(Transient)
	TObjectPtr<class AWaypoint> AdvanceWaypoint;

	/** Running totals, so MeleeShare produces a stable ratio instead of a random clump. */
	int32 SpawnCounter = 0;
	int32 MeleeSpawnCounter = 0;

	/** Per-row spawn tallies for the weighted pick, indexed the same as the cached row list. */
	TArray<int32> RowSpawnCounts;

	FTimerHandle TopUpTimer;
};
