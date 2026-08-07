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

	/** Ranged unit class. Any AUnitBase works; AMassShooterBot adds scoring + attribution. */
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

	/** Previous audit sample per bot, so "did it actually move" is a measurement not a guess. */
	TMap<TWeakObjectPtr<AUnitBase>, FVector> LastAuditedLocations;

	/** Resolves (and, on first use, creates) the waypoint bots patrol toward. */
	class AWaypoint* GetOrCreateAdvanceWaypoint();

	UPROPERTY(Transient)
	TObjectPtr<class AWaypoint> AdvanceWaypoint;

	/** Running totals, so MeleeShare produces a stable ratio instead of a random clump. */
	int32 SpawnCounter = 0;
	int32 MeleeSpawnCounter = 0;

	FTimerHandle TopUpTimer;
};
