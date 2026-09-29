// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GAS/GAS.h"
#include "MassShooterCombatComponent.generated.h"

class AUnitBase;
class UMassShooterLoadoutComponent;
class UMassShooterHealthComponent;

/** Local cosmetic event: our shot connected. The HUD draws a hitmarker from this. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMassShooterOnHitConfirmed, AActor*, HitActor, bool, bLethal);

/**
 * Turns "the player is holding the trigger" into server-authoritative WeaponModule shots.
 *
 * WeaponModule already owns everything about a shot after the trigger: the ShootAbility spawns the
 * projectile, spends ammo, applies its own cooldown and routes damage. What it does NOT have is a
 * shooter's trigger: automatic fire at the weapon's cadence, an aim point taken from the camera
 * rather than a mouse-picked world target, ADS, and a spread cone. That is this component.
 *
 * Authority model: the owning client decides WHERE it is aiming (only it knows its camera) and
 * WHEN it wants to fire; the server decides whether the shot happens at all. Firing goes through
 * AGASUnit::ActivateAbilityByInputID on the server, so ammo, cooldown and GAS costs are all
 * enforced there — a client that spams the RPC simply gets refused by the ability.
 */
UCLASS(ClassGroup = (MassShooter), meta = (BlueprintSpawnableComponent))
class MASSSHOOTERMODULE_API UMassShooterCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMassShooterCombatComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- Input entry points (owning client) --------------------------------------------------

	/** Trigger down/up. While held, Tick fires at the equipped weapon's cadence. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Combat")
	void SetFiring(bool bNewFiring);

	/** Aim-down-sights on/off. Narrows the spread cone and slows the owner. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Combat")
	void SetAiming(bool bNewAiming);

	/** Explicit reload request (R). Auto-reload also runs when the magazine empties. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Combat")
	void RequestReload();

	/** Fire exactly one shot at an explicit world point, bypassing the trigger state. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Combat")
	void FireAt(FVector AimLocation);

	// ---- Queries (HUD) -----------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MassShooter|Combat")
	bool IsFiring() const { return bFiring; }

	UFUNCTION(BlueprintPure, Category = "MassShooter|Combat")
	bool IsAiming() const { return bAiming; }

	/**
	 * Current spread half-angle in degrees. Grows with each shot and while moving, shrinks while
	 * still. The HUD sizes the crosshair gap from this, so the reticle always tells the truth.
	 */
	UFUNCTION(BlueprintPure, Category = "MassShooter|Combat")
	float GetCurrentSpreadDegrees() const { return CurrentSpread; }

	/** Where the owner is currently aiming, in world space (camera trace, cached each tick). */
	UFUNCTION(BlueprintPure, Category = "MassShooter|Combat")
	FVector GetAimPoint() const { return CachedAimPoint; }

	/** The actor currently under the crosshair, or null. */
	UFUNCTION(BlueprintPure, Category = "MassShooter|Combat")
	AActor* GetAimTarget() const { return CachedAimTarget.Get(); }

	/**
	 * Setzt AUnitBase::CastTime auf die echte Dauer des gleich startenden Casts.
	 *
	 * Ohne das steht dort der Vorgabewert 5 s, waehrend ein Nachladen 1,4 bis 2,2 s dauert
	 * (DT_WeaponData_Soldier_AH). Die Cast-Leiste teilt UnitControlTimer durch CastTime - sie
	 * liefe also nur bis rund ein Drittel und verschwaende dann, weil die Faehigkeit laengst
	 * fertig ist. Gemessen am 27.09.2026.
	 *
	 * Weder ReloadAbility noch SwitchWeaponAbility setzen den Wert selbst; sie gehoeren dem
	 * WeaponModule und bleiben unangetastet, deshalb steht die Korrektur hier.
	 *
	 * @param Sekunden Dauer des Casts. Werte <= 0 werden ignoriert.
	 */
	void SetCastDuration(float Seconds);

	/**
	 * Waffenwechsel ueber die Faehigkeit statt ueber UWeaponComponent::Server_SwitchWeapon.
	 *
	 * Nur so gibt es Cast-Leiste, Wechsel-Montage und den Abklingring im HUD - der direkte Aufruf
	 * tauscht die Waffe stumm aus. USwitchWeaponAbility::PerformSwitchWeapon schaltet dabei immer
	 * auf die NAECHSTE Waffe; ein Zielindex laesst sich nicht uebergeben.
	 *
	 * @return true, wenn die Faehigkeit angestossen wurde.
	 */
	bool TryStartWeaponSwitchAbility();

	UPROPERTY(BlueprintAssignable, Category = "MassShooter|Combat")
	FMassShooterOnHitConfirmed OnHitConfirmed;

	// ---- Tuning ------------------------------------------------------------------------------

	/**
	 * Which ability slot of AGASUnit::DefaultAbilities is the shoot ability.
	 *
	 * ActivateAbilityByInputID resolves the slot positionally (AbilityOne -> index 0), so this
	 * must match the order the owning character grants them in. WeaponModule's own reference unit
	 * uses Shoot=0, Reload=1, SwitchWeapon=2, and its ShootAbility graph triggers a reload by
	 * activating slot 1 — so that order is load-bearing, not a convention.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat|Abilities")
	EGASAbilityInputID FireAbilitySlot = EGASAbilityInputID::AbilityOne;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat|Abilities")
	EGASAbilityInputID ReloadAbilitySlot = EGASAbilityInputID::AbilityTwo;

	/** Slot der Waffenwechsel-Faehigkeit - dieselbe Reihenfolge wie oben, also Index 2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat|Abilities")
	EGASAbilityInputID SwitchWeaponAbilitySlot = EGASAbilityInputID::AbilityThree;

	/** Spread half-angle (deg) when standing still, not aiming, having not fired for a while. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat|Spread")
	float BaseSpreadDegrees = 1.2f;

	/** Multiplier applied to the whole spread while aiming down sights. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat|Spread")
	float AimSpreadScale = 0.25f;

	/** Extra spread (deg) added per shot fired. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat|Spread")
	float SpreadPerShot = 0.7f;

	/** Extra spread (deg) at full sprint speed, scaled linearly by current speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat|Spread")
	float SpreadWhileMovingDegrees = 2.5f;

	/** Ceiling for accumulated spread. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat|Spread")
	float MaxSpreadDegrees = 8.f;

	/** Degrees of accumulated spread recovered per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat|Spread")
	float SpreadRecoveryPerSecond = 4.f;

	/** Upward view kick (deg) per shot. 0 disables view recoil entirely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat|Recoil")
	float RecoilPitchPerShot = 0.22f;

	/** Random horizontal view kick (deg) per shot, +/-. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat|Recoil")
	float RecoilYawPerShot = 0.1f;

	/**
	 * Ceiling on how far recoil may climb during one burst. Without it, an automatic weapon walks
	 * the camera at the fire rate until the player is looking straight up.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat|Recoil")
	float MaxRecoilPitchDegrees = 6.f;

	/** Degrees per second the accumulated recoil is given back once firing stops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat|Recoil")
	float RecoilRecoveryPerSecond = 14.f;

	/** Quiet time after the last shot before recovery starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat|Recoil")
	float RecoilRecoveryDelay = 0.12f;

	/** Reload automatically when the magazine runs dry. WeaponModule's ShootAbility does not. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Combat")
	bool bAutoReload = true;

protected:
	/** Client -> server: fire one shot at this world point. */
	UFUNCTION(Server, Reliable)
	void Server_Fire(FVector_NetQuantize AimLocation);

	UFUNCTION(Server, Reliable)
	void Server_Reload();

	/** Client -> server: the trigger came up. Stops a continuous ShootAbility. */
	UFUNCTION(Server, Reliable)
	void Server_StopFire();

	/** Server: replicated so other clients can play an aiming pose. */
	UFUNCTION(Server, Unreliable)
	void Server_SetAiming(bool bNewAiming);

	/** Server-side shot execution, shared by the RPC and by AI/debug callers. */
	void ExecuteFire(const FVector& AimLocation);

	/** Traces from the local camera and refreshes CachedAimPoint / CachedAimTarget. */
	void UpdateAimPoint();

	/** Applies the spread cone to a camera-to-aim direction and returns the perturbed world point. */
	FVector ApplySpread(const FVector& Origin, const FVector& AimPoint) const;

	/** Local view kick. Owning client only — the server never rotates a client's camera. */
	void ApplyRecoil();

	/** Eases the accumulated recoil back once the player stops firing. */
	void RecoverRecoil(float DeltaTime);

	/** One shot if the weapon is ready. Shared by the press edge and the automatic-fire Tick. */
	bool TryFireOnce();

	/** Throttled "why did no shot happen" log, active under Shooter.Debug.DrawAim. */
	void ReportFireBlocked(const TCHAR* Reason);

	UPROPERTY(Transient)
	TObjectPtr<AUnitBase> OwnerUnit;

	UPROPERTY(Transient)
	TObjectPtr<UMassShooterLoadoutComponent> Loadout;

	UPROPERTY(Transient)
	TObjectPtr<UMassShooterHealthComponent> Health;

	/** Replicated to everyone so remote clients can drive an ADS animation. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Combat")
	bool bAiming = false;

	bool bFiring = false;

	/** Seconds until the next shot may be requested (owning client cadence gate). */
	float FireCooldownRemaining = 0.f;

	/** Accumulated spread, degrees. */
	float CurrentSpread = 0.f;

	/** View pitch (deg) currently owed back to the player by recoil recovery. */
	float AccumulatedRecoilPitch = 0.f;

	/** Countdown before recovery may start. Refreshed by every shot. */
	float RecoilRecoveryHoldRemaining = 0.f;

	FVector CachedAimPoint = FVector::ZeroVector;

	/** Where the aim trace started (the camera). Kept for the Shooter.Debug.DrawAim overlay. */
	FVector CachedAimOrigin = FVector::ZeroVector;

	TWeakObjectPtr<AActor> CachedAimTarget;

	/** Throttle for the empty-magazine check. */
	float AutoReloadAccumulator = 0.f;

	/** Edge state for the Shooter.Test.HoldFire trigger injection. */
	bool bTestTriggerHeld = false;

	/** Throttle state for ReportFireBlocked. */
	const TCHAR* LastFireBlockReason = nullptr;
	float LastFireBlockLogTime = -100.f;

public:
	/**
	 * True while the trigger is being held by the test cvar rather than by hardware.
	 *
	 * The controller's trigger safety net reconciles against the physical key, which would
	 * immediately cancel an injected hold — there is no key to be down. It asks this first.
	 */
	static bool IsTestTriggerActive();
};
