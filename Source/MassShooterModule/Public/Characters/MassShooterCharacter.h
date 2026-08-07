// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Characters/Unit/UnitBase.h"
#include "MassShooterCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UWeaponComponent;
class UMassShooterCombatComponent;
class UMassShooterLoadoutComponent;
class UMassShooterHealthComponent;
class UMassShooterMovementComponent;

/**
 * The player pawn of a mass shooter.
 *
 * Derives AUnitBase — which is eleven levels of inheritance and looks excessive until you list
 * what it buys, all of it needed here:
 *   AGASUnit         -> replicated AbilitySystemComponent + AttributeSet (WeaponModule's shoot,
 *                       reload and switch abilities are GameplayAbilities; they need an ASC)
 *   ALevelUnit       -> UnitIndex, the stable replicated key the RTS registry is built on
 *   AMassUnitBase    -> UMassActorBindingComponent, i.e. a Mass entity
 *   APerformanceUnit -> IMassVisibilityInterface
 *   AUnitBase        -> TeamId, alliance mask, projectile spawning, the damage pipeline, and the
 *                       type every RTS bot's targeting is written against
 *
 * A plain ACharacter would mean the Mass bots literally cannot see the player (RTS perception is
 * a Mass query over entities), WeaponModule's abilities would have nothing to attach to, and the
 * whole damage path would have to be rebuilt. The cost is a handful of unused RTS fields per
 * instance, which is nothing next to that.
 *
 * MOVEMENT is the one place this pawn deliberately parts company with RTSUnitTemplate: it is
 * driven by UMassShooterMovementComponent (a real CharacterMovementComponent), not by the Mass
 * locomotion processors. See FMassShooterModule::OnMassArchetypeBuilding for how that separation
 * is made — and note that it costs the base plugin nothing, because opting out is done purely by
 * choosing which tags this pawn's archetype gets.
 */
UCLASS(Blueprintable)
class MASSSHOOTERMODULE_API AMassShooterCharacter : public AUnitBase
{
	GENERATED_BODY()

public:
	AMassShooterCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * RTSUnitTemplate calls this on the VICTIM for every projectile hit, handing us the shooter.
	 * UHT declares the _Implementation virtual, so overriding it is a supported extension point,
	 * not a trick — and chaining to Super keeps the base damage behaviour byte-for-byte identical.
	 * This is what makes kill attribution exact instead of a heuristic.
	 */
	virtual void HandleProjectileImpact_Implementation(AActor* Shooter, const FVector& ImpactLocation,
		TSubclassOf<class AProjectile> ProjectileClass, float DamageOverride,
		TSubclassOf<class UGameplayEffect> ProjectileEffect,
		TSubclassOf<class UGameplayEffect> ProjectileEffect2,
		TSubclassOf<class UGameplayEffect> ProjectileEffect3) override;

	// ---- Components --------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MassShooter")
	UMassShooterCombatComponent* GetCombat() const { return Combat; }

	UFUNCTION(BlueprintPure, Category = "MassShooter")
	UMassShooterLoadoutComponent* GetLoadout() const { return Loadout; }

	UFUNCTION(BlueprintPure, Category = "MassShooter")
	UMassShooterHealthComponent* GetShooterHealth() const { return ShooterHealth; }

	UFUNCTION(BlueprintPure, Category = "MassShooter")
	UWeaponComponent* GetWeaponComponent() const { return WeaponComp; }

	UFUNCTION(BlueprintPure, Category = "MassShooter")
	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }

	UFUNCTION(BlueprintPure, Category = "MassShooter")
	UCameraComponent* GetThirdPersonCamera() const { return ThirdPersonCamera; }

	// ---- Stance ------------------------------------------------------------------------------

	/** Sprint on/off (owning client + server). Also widens the combat spread cone. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Stance")
	void SetSprinting(bool bNewSprinting);

	UFUNCTION(BlueprintPure, Category = "MassShooter|Stance")
	bool IsSprinting() const { return bSprinting; }

	/** Crouch toggle routed through ACharacter's own replicated crouch. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Stance")
	void SetCrouching(bool bNewCrouching);

	/** Switch between the first- and third-person camera. Purely local. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|View")
	void SetFirstPerson(bool bNewFirstPerson);

	UFUNCTION(BlueprintCallable, Category = "MassShooter|View")
	void ToggleViewMode();

	UFUNCTION(BlueprintPure, Category = "MassShooter|View")
	bool IsFirstPerson() const { return bFirstPerson; }

	/** Re-applies the movement speed for the current sprint/aim stance. */
	void RefreshStanceSpeed();

	// ---- Death / respawn ---------------------------------------------------------------------

	/**
	 * Server: put this pawn back in the fight at Where — full stats, fresh loadout, spawn
	 * protection, RTS unit state back to Idle. Used by the game mode instead of destroying and
	 * respawning the pawn, which would cost a Mass entity teardown and rebuild per death.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Respawn")
	void RespawnAt(const FTransform& Where);

	/** Server: enter the downed presentation (hidden, frozen, not targetable). */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Respawn")
	void EnterDeadState();

	UFUNCTION(BlueprintPure, Category = "MassShooter|Respawn")
	bool IsDeadShooter() const { return bShooterDead; }

	/** Team this pawn fights for. Server: also refreshes the RTS alliance mask. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Team")
	void SetShooterTeam(int32 NewTeamId);

	// ---- Combat feedback ---------------------------------------------------------------------
	//
	// Both of these are driven from the VICTIM's HandleProjectileImpact override, which is the one
	// place in the whole pipeline that knows a specific shooter hit a specific target. The victim
	// tells its own client where the shot came from; it tells the shooter's client that the shot
	// landed. Unreliable on purpose — a dropped hitmarker is a missed cosmetic, not a missed hit,
	// and during sustained fire these would otherwise flood the reliable buffer.

	/** Owning client: our shot connected. Drives the HUD hitmarker. */
	UFUNCTION(Client, Unreliable)
	void Client_NotifyHitConfirmed(bool bLethal);

	/** Owning client: we were hit from this world position. Drives the directional damage arc. */
	UFUNCTION(Client, Unreliable)
	void Client_NotifyDamageFrom(FVector_NetQuantize SourceLocation);

	/**
	 * Server: route one landed hit to both clients' HUDs. Called by a victim (player or bot) from
	 * its HandleProjectileImpact override, so the logic lives in exactly one place.
	 */
	static void ReportHitToHUDs(AActor* Victim, AActor* Shooter, bool bLethal);

	/**
	 * Turns a projectile's declared damage into the value the impact should actually apply.
	 *
	 * AUnitBase::HandleProjectileImpact treats ANY DamageOverride >= 0 as authoritative — only a
	 * negative value means "ask the projectile CDO". A projectile that arrives declaring exactly
	 * 0 therefore lands for nothing, silently.
	 *
	 * That is not hypothetical: in this template the hostiles end up firing a WeaponModule weapon
	 * projectile (BP_Projectile_3) whose damage reaches the victim as 0, so a player under
	 * sustained fire took 1788 registered hits without losing a single point of health. The fix
	 * belongs in WeaponModule's spawn path, which this module is not allowed to modify, so the
	 * compensation lives here instead: a zero-damage hit falls back to the shooter's AttackDamage,
	 * and failing that to the projectile's own CDO value.
	 *
	 * Set bTreatZeroDamageAsAttackDamage = false on a pawn that genuinely wants harmless hits.
	 */
	static float ResolveImpactDamage(const AActor* Shooter, float DamageOverride, bool bEnabled);

	/** See ResolveImpactDamage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat")
	bool bTreatZeroDamageAsAttackDamage = true;

	// ---- Ability bar -------------------------------------------------------------------------

	/**
	 * Activate ability-bar slot `Slot` (0-based; keys 1..6 map to 0..5).
	 *
	 * Slot N is DefaultAbilities[N], the same positional mapping AGASUnit::GetAbilityForInputID
	 * uses, so what the HUD labels "3" is what pressing 3 runs. Client entry point; hops to the
	 * server, which owns activation.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Abilities")
	void ActivateAbilitySlot(int32 Slot);

	/** How many ability-bar slots exist (== DefaultAbilities.Num()). */
	UFUNCTION(BlueprintPure, Category = "MassShooter|Abilities")
	int32 GetAbilitySlotCount() const { return DefaultAbilities.Num(); }

	/**
	 * Launch the pawn (dash, knockback, jump pad).
	 *
	 * Multicast rather than server-only: LaunchCharacter writes a pending velocity into the local
	 * movement component, and the owning client runs its own prediction — if only the server
	 * launched, the client would immediately correct the dash away.
	 */
	UFUNCTION(NetMulticast, Reliable, BlueprintCallable, Category = "MassShooter|Movement")
	void Multicast_Launch(FVector LaunchVelocity, bool bOverrideXY, bool bOverrideZ);

	// ---- Testing hooks -----------------------------------------------------------------------

	/** Fire at a world point with no input. Server-side; used by automated tests and AI. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Debug")
	void DebugFireAt(FVector AimLocation);

	/** Apply damage exactly as a real hit does (an instant EffectDamage GameplayEffect). */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Debug")
	void DebugApplyDamage(float Amount, AActor* FromInstigator);

protected:
	UFUNCTION(Server, Reliable)
	void Server_SetSprinting(bool bNewSprinting);

	UFUNCTION(Server, Reliable)
	void Server_ActivateAbilitySlot(int32 Slot, FVector_NetQuantize AimLocation);

	UFUNCTION()
	void OnRep_ShooterDead();

	/** Bound to the health component; runs on the server the moment health hits zero. */
	UFUNCTION()
	void HandleDeath(AActor* Victim, AActor* Killer);

	/** Show/hide the body for the death presentation, on every machine. */
	void SetBodyVisibleForShooter(bool bVisible);

	// ---- Subobjects --------------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> ThirdPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	/**
	 * Muzzle point tagged "ProjectileSpawn".
	 *
	 * AUnitBase::GetProjectileSpawnLocation looks for a component with exactly this tag and falls
	 * back to a generic offset otherwise; WeaponModule's BP_ShootAbility indexes
	 * GetComponentsByTag("ProjectileSpawn")[0] directly, so without one it hits an empty array.
	 * Providing it here is what makes a player able to use the stock WeaponModule abilities with
	 * no Blueprint changes at all.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> ProjectileSpawnPoint;

	/** WeaponModule weapon. Registers its own UWeaponAttributeSet onto our ASC in its BeginPlay. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter|Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWeaponComponent> WeaponComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter|Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMassShooterCombatComponent> Combat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter|Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMassShooterLoadoutComponent> Loadout;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter|Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMassShooterHealthComponent> ShooterHealth;

	/** Cached cast of GetCharacterMovement(). */
	UPROPERTY(Transient)
	TObjectPtr<UMassShooterMovementComponent> ShooterMovement;

	/** Camera height above the capsule centre in first person. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|View")
	float FirstPersonEyeHeight = 64.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|View")
	float ThirdPersonArmLength = 260.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|View")
	FVector ThirdPersonSocketOffset = FVector(0.f, 60.f, 60.f);

	/** Start in first person. Designers flip this on the Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|View")
	bool bStartFirstPerson = true;

	/**
	 * Replicated so remote clients hide the corpse and the owner stops feeding input. The RTS
	 * never sees this pawn at 0 HP for long (the health component tops it back up on respawn), so
	 * this flag — not the GAS attribute — is the single source of truth for "currently dead".
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ShooterDead, BlueprintReadOnly, Category = "MassShooter")
	bool bShooterDead = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Stance")
	bool bSprinting = false;

	bool bFirstPerson = true;

	/**
	 * One-shot latch for taking the pawn out of the RTS acquire -> chase -> attack loop.
	 *
	 * A player pawn is a valid enemy as far as the RTS AI is concerned, so without this the Mass
	 * state machine picks its own targets and attacks them — the pawn kills anything that walks
	 * into range with no input at all, which is not a shooter. (Observed directly: bots died to
	 * the standing, un-driven player pawn in a headless run.)
	 *
	 * Dropping the entity's detection tag is the clean lever, but the entity does not exist yet at
	 * BeginPlay — the RTS creates it a few seconds in — so Tick retries until it lands, then stops.
	 */
	bool bRtsAutoAttackSuppressed = false;
};
