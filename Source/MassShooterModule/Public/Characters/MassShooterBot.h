// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Characters/Unit/UnitBase.h"
#include "MassShooterBot.generated.h"

class UMassShooterHealthComponent;
class UWeaponComponent;

/**
 * An AI combatant — the "mass" in mass shooter.
 *
 * This is a normal RTSUnitTemplate unit and stays one: its movement, perception, targeting,
 * attacking and animation are entirely the base plugin's Mass pipeline, untouched. Hundreds of
 * these are the point of building on RTSUnitTemplate at all.
 *
 * All this subclass adds is what a shooter scoreboard needs and an RTS does not:
 *   - exact kill attribution, by overriding the virtual HandleProjectileImpact_Implementation
 *     that RTSUnitTemplate already calls on the victim with the shooter
 *   - a per-kill score value and a death broadcast the game mode listens to
 *   - an optional WeaponModule weapon, so bots can carry the same guns players do
 */
UCLASS(Blueprintable)
class MASSSHOOTERMODULE_API AMassShooterBot : public AUnitBase
{
	GENERATED_BODY()

public:
	AMassShooterBot(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;

	/** See AMassShooterCharacter's override — same supported extension point, same reason. */
	virtual void HandleProjectileImpact_Implementation(AActor* Shooter, const FVector& ImpactLocation,
		TSubclassOf<class AProjectile> ProjectileClass, float DamageOverride,
		TSubclassOf<class UGameplayEffect> ProjectileEffect,
		TSubclassOf<class UGameplayEffect> ProjectileEffect2,
		TSubclassOf<class UGameplayEffect> ProjectileEffect3) override;

	UFUNCTION(BlueprintPure, Category = "MassShooter")
	UMassShooterHealthComponent* GetShooterHealth() const { return ShooterHealth; }

	UFUNCTION(BlueprintPure, Category = "MassShooter")
	UWeaponComponent* GetWeaponComponent() const { return WeaponComp; }

	/** Match score awarded to whoever kills this bot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter")
	int32 ScoreValue = 1;

	/** See AMassShooterCharacter::ResolveImpactDamage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat")
	bool bTreatZeroDamageAsAttackDamage = true;

	/**
	 * Damage per hit, applied after the RTS attribute init.
	 *
	 * The RTS ranged archetype these bots inherit hits for 80, which against the player's 150
	 * health plus 50 shield is death in three hits — measured, and unplayable for a shooter where
	 * a dozen hostiles converge on you. 0 keeps whatever the attribute table provides.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat", meta = (ClampMin = "0.0"))
	float AttackDamageOverride = 18.f;

	/** Server: apply AttackDamageOverride. Called by the spawner after InitializeAttributes. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Combat")
	void ApplyAttackDamageOverride();

protected:
	/**
	 * Re-applies AttackDamageOverride over a bounded startup window.
	 *
	 * A single write does not hold: RTSUnitTemplate's attribute initialisation lands after the
	 * spawn call chain and derives AttackDamage from the attribute table, overwriting it —
	 * measured as damage settling back to 58 after a clean write of 18. Same shape as the health
	 * component's stat window, and it closes after a few seconds so genuine later changes stand.
	 */
	void TickAttackDamageWindow();

	FTimerHandle AttackDamageWindowTimer;
	int32 AttackDamageWindowTicksLeft = 0;

public:

	/** Display name used in the kill feed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter")
	FString BotDisplayName = TEXT("Hostile");

	/**
	 * How far this bot can spot a target.
	 *
	 * RTSUnitTemplate defaults to 2000 uu, which suits an RTS where the commander walks units into
	 * contact. Nobody walks a shooter's hostiles anywhere: they spawn at the map edge, and at 2000
	 * uu they simply stand there forever. Measured in a PIE session on the example arena — 60 bots
	 * alive, the nearest one 3145 uu from the player, none of them ever moving.
	 *
	 * A radius that covers the arena makes them acquire and chase, which is what turns them into
	 * an actual threat. Applied to the Mass binding component in BeginPlay (before the entity is
	 * built) so it holds regardless of what a Blueprint serialised earlier.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Perception")
	float SightRadiusOverride = 7000.f;

	/** Distance at which an acquired target is dropped again. Kept above SightRadiusOverride. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Perception")
	float LoseSightRadiusOverride = 9000.f;

	/**
	 * Difficulty multiplier applied to health when the spawner scales a wave up. Applied on top of
	 * the health component's DefaultMaxHealth, before the first stat write.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassShooter")
	void ApplyWaveScaling(float HealthMultiplier, float DamageMultiplier);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMassShooterHealthComponent> ShooterHealth;

	/**
	 * Optional WeaponModule weapon. Present so a bot can be armed identically to a player; it does
	 * nothing on its own, because a bot's shooting is driven by the RTS attack state machine.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWeaponComponent> WeaponComp;

	/** Muzzle anchor tagged "ProjectileSpawn" — same reason as on the player character. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> ProjectileSpawnPoint;
};
