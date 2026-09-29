// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MassShooterHudWidget.generated.h"

class AMassShooterCharacter;
class UMassShooterAbilitySlotWidget;
class UPanelWidget;
class UProgressBar;
class UTextBlock;
class UWidget;

/**
 * The designed half of the shooter HUD: vitals, ammo and the ability bar.
 *
 * AMassShooterHUD draws the whole HUD on the Canvas, which is right for a plugin that must look
 * complete with no content: bars and text are exactly what Canvas is good at. What Canvas cannot do
 * is carry a project's material language - a panel material, a button frame, a font. So the three
 * pieces a player looks at constantly move here, into UMG, where AstraHelix's MI_SciFi_Panel and
 * MI_SciFi_Btn_* apply; the crosshair, kill feed, banner and scoreboard stay on the Canvas, where
 * their cost is a few draw calls and their look does not matter as much.
 *
 * Every binding is OPTIONAL. A Blueprint that only wants a health bar binds HealthBar and leaves
 * the rest out; nothing here asserts on a missing widget. That keeps this class usable as the base
 * for several differently-scoped HUDs instead of one mandatory layout.
 *
 * Reads, never writes: the widget polls the pawn's components each tick and never touches gameplay
 * state, so it stays safe to run on a simulated proxy or not at all.
 */
UCLASS(Abstract)
class MASSSHOOTERMODULE_API UMassShooterHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** The pawn whose state is shown. Resolved from the owning controller when unset. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|HUD")
	void SetShooterPawn(AMassShooterCharacter* InPawn);

protected:
	// ---- Vitals -------------------------------------------------------------------------------

	/** 0..1 of current health. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UTextBlock> HealthText;

	/** Hidden entirely when the pawn has no shield, rather than shown empty. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UProgressBar> ShieldBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UTextBlock> ShieldText;

	/** Shown only during spawn protection. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UWidget> SpawnProtectedBox;

	// ---- Ammo ---------------------------------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UTextBlock> AmmoText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UTextBlock> WeaponNameText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UTextBlock> GrenadeText;

	/** Tint applied to AmmoText once the magazine is empty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|HUD")
	FLinearColor EmptyAmmoColor = FLinearColor(1.f, 0.30f, 0.25f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|HUD")
	FLinearColor NormalAmmoColor = FLinearColor(0.95f, 0.85f, 0.55f, 1.f);

	// ---- Cast-Leiste --------------------------------------------------------------------------
	//
	// Warum hier und nicht wie im RTS ueber der Einheit: UUnitTimerWidget haengt an
	// APerformanceUnit::TimerWidgetComp, also an einem Weltwidget ueber dem Kopf. Im Topdown sieht
	// man das, im Ego-Blick nicht - AMassShooterCharacter startet mit bStartFirstPerson. Die
	// Leiste muss deshalb auf den Bildschirm.
	//
	// Gefuellt wird sie aus derselben Quelle wie die RTS-Leiste: UnitControlTimer geteilt durch
	// CastTime, sichtbar nur im Zustand Casting und nur zwischen 0 und 1 (siehe
	// UUnitTimerWidget::TimerTick). Damit zeigen beide dasselbe an, ohne dass es zwei Wahrheiten
	// gibt.

	/** Klammer um Leiste und Beschriftung - wird als Ganzes ein- und ausgeblendet. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UWidget> CastBox;

	/** 0..1 des laufenden Casts. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UProgressBar> CastBar;

	/** Name der castenden Faehigkeit, z.B. "Reload". */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UTextBlock> CastText;

	/** Fuellfarbe der Cast-Leiste. Vorgabe wie UUnitTimerWidget::CastingColor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|HUD")
	FLinearColor CastColor = FLinearColor(0.20f, 0.85f, 0.95f, 1.f);

	// ---- Abilities ----------------------------------------------------------------------------

	/** Slots are created into this container, one per entry of the pawn's DefaultAbilities. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UPanelWidget> AbilityContainer;

	/** Slot widget class. Without it the ability bar simply stays empty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|HUD")
	TSubclassOf<UMassShooterAbilitySlotWidget> AbilitySlotClass;

	/**
	 * Horizontal gap to the left and right of each ability slot frame, in slate units.
	 *
	 * This spaces the frames apart without touching their shape: the slot widget's own SizeBox
	 * fixes it at a square size, so padding here can never squash it into a rectangle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|HUD",
		meta = (ClampMin = "0.0", ClampMax = "40.0"))
	float AbilitySlotPadding = 6.f;

	/** Blueprint hook for per-frame polish the bindings cannot express. */
	UFUNCTION(BlueprintImplementableEvent, Category = "MassShooter|HUD")
	void OnHudRefreshed(AMassShooterCharacter* Pawn);

private:
	void RefreshVitals(AMassShooterCharacter* Pawn);
	void RefreshAmmo(AMassShooterCharacter* Pawn);
	void RefreshAbilities(AMassShooterCharacter* Pawn);
	void RefreshCast(AMassShooterCharacter* Pawn);

	/** Cached pawn; re-resolved whenever it dies or the player respawns into a new one. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AMassShooterCharacter> ShooterPawn;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMassShooterAbilitySlotWidget>> AbilitySlots;
};
