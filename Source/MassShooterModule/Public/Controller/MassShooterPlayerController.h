// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controller/PlayerController/CustomControllerBase.h"
#include "MassShooterPlayerController.generated.h"

class UInputConfig;
class UInputMappingContext;
class AMassShooterCharacter;
class AMassShooterHUD;
struct FInputActionValue;
struct FGameplayTag;
enum class ETriggerEvent : uint8;

/**
 * The shooter player controller.
 *
 * Derives ACustomControllerBase — the Mass-aware order layer — rather than
 * ACameraControllerBase, whose extra level is the RTS camera state machine, fog of war and
 * minimap: pure ballast for a first-person game. Staying on this branch keeps the whole
 * RTSUnitTemplate controller API (ability RPCs, unit registry access, team plumbing) available
 * to anyone extending this class.
 *
 * IMPORTANT — this class does NOT chain Super::BeginPlay or Super::Tick.
 *
 * AControllerBase::Tick runs GetHitResultUnderCursor() every frame with no IsLocalController()
 * gate, plus a Dijkstra pass over enemy units. That is the right thing for a top-down RTS and
 * completely wrong here: a shooter has no cursor, and on a dedicated server it would be one
 * pointless world trace per connected player per frame. So both functions jump straight to
 * APlayerController with a qualified call, skipping four RTS levels. Every RPC, formation helper
 * and prediction utility on those levels stays fully usable — only the per-frame and startup
 * bodies are skipped.
 *
 * The price is coupling to base-class internals: if AControllerBase::BeginPlay ever grows
 * something we need, nothing here will say so. That is the deliberate cost of not being allowed
 * to modify the plugin, and the one thing it did that we DO want (the alliance mask) is done
 * explicitly below.
 */
UCLASS(Blueprintable)
class MASSSHOOTERMODULE_API AMassShooterPlayerController : public ACustomControllerBase
{
	GENERATED_BODY()

public:
	AMassShooterPlayerController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;

	/** RTSUnitTemplate's tag -> UInputAction data asset. Read-only reuse; no changes to it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Input")
	TObjectPtr<UInputConfig> InputConfig;

	/** Keyboard/mouse and gamepad share one context — Enhanced Input resolves both. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Input")
	TObjectPtr<UInputMappingContext> ShooterMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Input")
	int32 MappingPriority = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Input")
	float LookSensitivity = 1.f;

	/** True while the scoreboard key is held. Read by the HUD. */
	UFUNCTION(BlueprintPure, Category = "MassShooter|UI")
	bool IsScoreboardHeld() const { return bScoreboardHeld; }

	/** The possessed shooter pawn, resolved every frame (see ResolveShooterCharacter). */
	UFUNCTION(BlueprintPure, Category = "MassShooter")
	AMassShooterCharacter* GetShooterCharacter() const { return ShooterCharacter; }

	/**
	 * Ask the server to respawn now instead of waiting out the death timer.
	 *
	 * A request, not a command: the game mode refuses it until the minimum delay has elapsed, so
	 * a client spamming the key gains nothing.
	 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "MassShooter|Respawn")
	void ServerRequestRespawn();

	// ---- Console commands --------------------------------------------------------------------
	//
	// A shooter's core loop needs a human holding a mouse, which makes it exactly the thing an
	// automated or headless run cannot exercise. These give the fire path an input-free entry
	// point so "does pulling the trigger actually credit a kill" is answerable without a person.

	/** `ShooterFireAtNearest` — fire one round at the closest hostile unit. */
	UFUNCTION(Exec)
	void ShooterFireAtNearest();

	/** `ShooterHoldFire 1|0` — hold or release the trigger, exactly as the mouse button does. */
	UFUNCTION(Exec)
	void ShooterHoldFire(int32 bHold);

	/** `ShooterDamageSelf <Amount>` — take damage through the real GameplayEffect path. */
	UFUNCTION(Exec)
	void ShooterDamageSelf(float Amount);

	/**
	 * Fallback bindings for projects that have no UInputConfig asset yet.
	 *
	 * Enhanced Input needs an InputAction asset per action, which only exists as content. Rather
	 * than shipping a controller that silently does nothing until someone authors those assets,
	 * this binds the same handlers through the legacy axis/action path so WASD, mouse look and
	 * fire work out of the box. Turn it off once a real InputConfig is assigned.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|Input")
	bool bUseLegacyInputFallback = true;

	// ---- WeaponModule UI ---------------------------------------------------------------------

	/**
	 * WeaponModule's weapon panel (current weapon, ammo, magazines, level, talents).
	 *
	 * Defaults to WeaponModule's own BP_WeaponSelectionHUDWidget, so the weapon UI is present with
	 * no setup; clear it to suppress the panel, or point it at your own subclass.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|UI")
	TSubclassOf<class UWeaponSelectionHUDWidget> WeaponSelectionWidgetClass;

	/** Drives WeaponModule's talent/store server RPCs on behalf of the widget. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter|UI")
	TObjectPtr<class UWeaponHUDComponent> WeaponHUD;

	UFUNCTION(BlueprintPure, Category = "MassShooter|UI")
	UWeaponSelectionHUDWidget* GetWeaponSelectionWidget() const { return WeaponSelectionWidget; }

protected:
	// ---- Enhanced Input handlers -------------------------------------------------------------
	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_JumpStarted(const FInputActionValue& Value);
	void Input_JumpCompleted(const FInputActionValue& Value);
	void Input_SprintStarted(const FInputActionValue& Value);
	void Input_SprintCompleted(const FInputActionValue& Value);
	void Input_CrouchToggle(const FInputActionValue& Value);
	void Input_FireStarted(const FInputActionValue& Value);
	void Input_FireCompleted(const FInputActionValue& Value);
	void Input_AimStarted(const FInputActionValue& Value);
	void Input_AimCompleted(const FInputActionValue& Value);
	void Input_Reload(const FInputActionValue& Value);
	void Input_NextWeapon(const FInputActionValue& Value);
	void Input_PrevWeapon(const FInputActionValue& Value);
	void Input_Grenade(const FInputActionValue& Value);
	void Input_Dash(const FInputActionValue& Value);
	void Input_ToggleView(const FInputActionValue& Value);
	void Input_ScoreboardStarted(const FInputActionValue& Value);
	void Input_ScoreboardCompleted(const FInputActionValue& Value);
	void Input_Respawn(const FInputActionValue& Value);

	// ---- Legacy fallback handlers ------------------------------------------------------------
	void Legacy_MoveForward(float Value);
	void Legacy_MoveRight(float Value);
	void Legacy_Turn(float Value);
	void Legacy_LookUp(float Value);
	void Legacy_FirePressed();
	void Legacy_FireReleased();
	void Legacy_AimPressed();
	void Legacy_AimReleased();
	void Legacy_SprintPressed();
	void Legacy_SprintReleased();
	void Legacy_JumpPressed();
	void Legacy_JumpReleased();
	void Legacy_CrouchToggle();
	void Legacy_Reload();
	void Legacy_NextWeapon();
	void Legacy_PrevWeapon();
	void Legacy_Grenade();
	void Legacy_Dash();
	void Legacy_ToggleView();
	void Legacy_ScoreboardPressed();
	void Legacy_ScoreboardReleased();
	void Legacy_Respawn();

	/** Number-row ability bar. Slot N is DefaultAbilities[N] — the same index the HUD labels. */
	void ActivateAbilitySlot(int32 Slot);
	void Legacy_Ability1();
	void Legacy_Ability2();
	void Legacy_Ability3();
	void Legacy_Ability4();
	void Legacy_Ability5();
	void Legacy_Ability6();

	/** Applies a 2D move vector to the pawn (shared by both input paths). */
	void ApplyMoveInput(float Forward, float Right);

	/** Keeps ShooterCharacter current. OnPossess is authority-only, so this runs every frame. */
	void ResolveShooterCharacter();

	/** Creates WeaponModule's weapon panel and hands it to the WeaponHUD component. */
	void SetupWeaponModuleHUD();

	UPROPERTY(Transient)
	TObjectPtr<AMassShooterCharacter> ShooterCharacter;

	/** Throttle for the Shooter.Debug.DrawAim aim-vs-ReplicatedMouseLocation audit line. */
	float LastAimAuditTime = -100.f;

	/** Reports the weapon panel's runtime state. See Shooter.Debug.LogHud. */
	void AuditWeaponHUD();

	float LastHudAuditTime = -100.f;

	UPROPERTY(Transient)
	TObjectPtr<class UWeaponSelectionHUDWidget> WeaponSelectionWidget;

	bool bScoreboardHeld = false;

private:
	void BindTaggedAction(class UEnhancedInputComponent* EIC, const FGameplayTag& Tag,
		ETriggerEvent Event, void (AMassShooterPlayerController::*Func)(const FInputActionValue&));

	/** Registers the legacy axis/action mappings so the fallback path has something to bind to. */
	void RegisterLegacyInputMappings();
};
