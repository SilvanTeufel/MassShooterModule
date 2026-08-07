// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MassShooterHealthComponent.generated.h"

class AUnitBase;

/** Broadcast on the server the moment a unit carrying this component reaches 0 health. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMassShooterOnDeath, AActor*, Victim, AActor*, Killer);

/** Broadcast on every machine whenever the observed health value changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMassShooterOnHealthChanged, float, NewHealth, float, MaxHealth);

/**
 * Life, death and kill attribution for one AUnitBase — player or bot.
 *
 * Why polling and not a GAS delegate: RTSUnitTemplate's UAttributeSetBase::PostGameplayEffectExecute
 * writes the result with Health.SetCurrentValue() directly (AttributeSetBase.cpp:237), which
 * bypasses the AbilitySystemComponent, so no attribute-value-change delegate fires on the server.
 * Reading the value is therefore the only signal that does not require modifying the plugin.
 *
 * Kill attribution is exact, not guessed: RTSUnitTemplate routes every projectile hit through
 * AUnitBase::HandleProjectileImpact(Shooter, ...), whose _Implementation UHT declares virtual.
 * AMassShooterCharacter and AMassShooterBot override it, call NotifyDamageFrom() and then chain to
 * Super, so the base plugin's damage behaviour is completely unchanged.
 */
UCLASS(ClassGroup = (MassShooter), meta = (BlueprintSpawnableComponent))
class MASSSHOOTERMODULE_API UMassShooterHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMassShooterHealthComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: called by the owner's HandleProjectileImpact override. Stamps who is hurting us. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Health")
	void NotifyDamageFrom(AActor* Instigator);

	/** Fires once per death, on the server only. */
	UPROPERTY(BlueprintAssignable, Category = "MassShooter|Health")
	FMassShooterOnDeath OnDeath;

	UPROPERTY(BlueprintAssignable, Category = "MassShooter|Health")
	FMassShooterOnHealthChanged OnHealthChanged;

	/**
	 * Health/MaxHealth this unit is initialised to.
	 *
	 * Non-zero is REQUIRED, not cosmetic: RTSUnitTemplate only broadcasts presence for units with
	 * Health > 0, and a unit sitting at 0 is treated as a corpse by the RTS death pipeline — a
	 * player left at 0 gets despawned. See ApplyDefaultStats for why it is re-applied.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Health")
	float DefaultMaxHealth = 150.f;

	/**
	 * Whether this component writes the stat block at all.
	 *
	 * Off for a unit whose stats already come from an RTSUnitTemplate attribute DataTable — that
	 * init is the designer's intent and this would fight it. The death detection and kill
	 * attribution keep working either way; only the writing is skipped.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Health")
	bool bOverrideStatsOnStart = true;

	/** Regenerating overshield, absorbed before health (RTSUnitTemplate applies Shield first). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Health")
	float DefaultMaxShield = 50.f;

	/** Shield points regenerated per second once RegenDelay has elapsed since the last hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Health")
	float ShieldRegenPerSecond = 12.f;

	/** Seconds without taking damage before the shield starts refilling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Health")
	float ShieldRegenDelay = 4.f;

	/** Mana pool, spent by grenade/dash abilities. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Health")
	float DefaultMaxMana = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Health")
	float ManaRegenPerSecond = 8.f;

	/** How long an attribution stamp stays valid. A slow projectile still credits its shooter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Health")
	float KillCreditWindow = 10.f;

	/** True while Shooter.Debug.LogHits is on. Lets other classes log around the damage path. */
	static bool IsHitLoggingEnabled();

	/** Server: (re)apply the default stat block. Safe to call repeatedly. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Health")
	void ApplyDefaultStats();

	/** Server: restore to full and clear the death latch. Used by respawn. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Health")
	void ResetForRespawn();

	/** Server: open a damage-immunity window (seconds). */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Health")
	void GrantSpawnProtection(float Seconds);

	UFUNCTION(BlueprintPure, Category = "MassShooter|Health")
	bool IsSpawnProtected() const;

	UFUNCTION(BlueprintPure, Category = "MassShooter|Health")
	bool IsDeadShooter() const { return bDeadLatched; }

	UFUNCTION(BlueprintPure, Category = "MassShooter|Health")
	float GetHealthValue() const { return ObservedHealth; }

	UFUNCTION(BlueprintPure, Category = "MassShooter|Health")
	float GetMaxHealthValue() const { return ObservedMaxHealth; }

	UFUNCTION(BlueprintPure, Category = "MassShooter|Health")
	float GetShieldValue() const { return ObservedShield; }

	UFUNCTION(BlueprintPure, Category = "MassShooter|Health")
	float GetMaxShieldValue() const { return ObservedMaxShield; }

	UFUNCTION(BlueprintPure, Category = "MassShooter|Health")
	AActor* GetLastDamageInstigator() const { return LastDamageInstigator.Get(); }

	/** Seconds since this unit last took a projectile hit (large when never hit). */
	UFUNCTION(BlueprintPure, Category = "MassShooter|Health")
	float GetTimeSinceLastDamage() const;

protected:
	/** Cached owner, resolved in BeginPlay. */
	UPROPERTY(Transient)
	TObjectPtr<AUnitBase> OwnerUnit;

	/**
	 * Replicated so a client HUD can draw its own bars without reaching into GAS (attributes are
	 * replicated too, but Minimal replication mode means a non-owning client may not see them).
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Health")
	float ObservedHealth = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Health")
	float ObservedMaxHealth = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Health")
	float ObservedShield = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Health")
	float ObservedMaxShield = 0.f;

	/** One-shot latch so OnDeath fires exactly once per life. */
	bool bDeadLatched = false;

	TWeakObjectPtr<AActor> LastDamageInstigator;
	float LastDamageTime = -1000.f;
	float SpawnProtectionEndTime = -1000.f;

	/**
	 * Counts down the startup window in which the default stat block is re-applied.
	 *
	 * A single write does not stick: RTSUnitTemplate's own attribute init runs later and derives
	 * MaxHealth from BaseHealth (ALevelUnit), which is 0 for a unit with no attribute data table,
	 * clobbering an early write back to 0. So we also set BaseHealth, and re-apply for a bounded
	 * few seconds to win regardless of when that init lands.
	 */
	int32 StatInitTicksLeft = 0;
	float StatInitAccumulator = 0.f;

	/** Throttle for the health poll — a shooter does not need this at full tick rate. */
	float PollAccumulator = 0.f;

	/** Client-side edge detector for OnHealthChanged (the value arrives by replication). */
	float LastBroadcastHealth = -1.f;
};
