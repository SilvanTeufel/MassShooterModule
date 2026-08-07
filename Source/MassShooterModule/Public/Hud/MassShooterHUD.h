// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Hud/HUDBase.h"
#include "MassShooterHUD.generated.h"

class AMassShooterCharacter;
class AMassShooterGameState;
class AMassShooterPlayerController;

/**
 * The shooter HUD, drawn entirely on the Canvas.
 *
 * Canvas rather than UMG on purpose: this module has to be playable the moment it is enabled,
 * with no widget assets to author and nothing to wire up in a Blueprint. A dynamic crosshair,
 * bars, a kill feed and a scoreboard are all shapes and text, which is exactly what Canvas is
 * good at. Projects that want a designed UI turn bDrawDefaultHUD off in the plugin settings and
 * subclass or replace this.
 *
 * Derives RTSUnitTemplate's AHUDBase, but does NOT draw any of its RTS presentation.
 *
 * The inheritance is not for the drawing — it is for AHUDBase::SelectedUnits. WeaponModule's
 * UWeaponSelectionHUDWidget casts the player's HUD to AHUDBase and mirrors that array into its
 * per-weapon panels; a plain AHUD makes that cast fail and the weapon/ammo/level UI silently never
 * updates. So this class keeps the local pawn in SelectedUnits (RefreshWeaponSelection) and
 * overrides DrawHUD to draw the shooter HUD *instead of* the RTS selection boxes, indicators and
 * minimap — none of which apply to a first-person game.
 *
 * bDrawRTSHUD re-enables the inherited drawing for anyone who does want it.
 */
UCLASS()
class MASSSHOOTERMODULE_API AMassShooterHUD : public AHUDBase
{
	GENERATED_BODY()

public:
	AMassShooterHUD();

	virtual void DrawHUD() override;

	/**
	 * Also run AHUDBase's RTS drawing (selection rectangle, unit indicators, health bars).
	 * Off by default: in a first-person game it draws selection boxes around everything and costs
	 * a per-frame pass over every registered unit.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|HUD")
	bool bDrawRTSHUD = false;

	/** Show the hitmarker for HitMarkerDuration seconds. Called locally when a shot connects. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|HUD")
	void ShowHitMarker(bool bLethal);

	/** Flash a directional damage indicator from a world position. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|HUD")
	void ShowDamageDirection(const FVector& FromWorldLocation);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|HUD")
	float HitMarkerDuration = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|HUD")
	float DamageIndicatorDuration = 1.2f;

	/** How long a kill-feed line stays before it fades out. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|HUD")
	float KillFeedEntryLifetime = 6.f;

	// ---- Ability bar -------------------------------------------------------------------------

	/** Draw the ability bar (icon, key hint and cooldown per granted ability). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|HUD|Abilities")
	bool bDrawAbilityBar = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|HUD|Abilities", meta = (ClampMin = "24"))
	float AbilitySlotSize = 64.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|HUD|Abilities")
	float AbilitySlotPadding = 8.f;

	/** Distance of the bar's bottom edge from the bottom of the screen. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "MassShooter|HUD|Abilities")
	float AbilityBarBottomMargin = 26.f;

protected:
	/**
	 * Keeps the locally controlled pawn in AHUDBase::SelectedUnits.
	 *
	 * In an RTS that array is filled by marquee-selecting units. A shooter has exactly one unit
	 * the player cares about — the one they are, permanently — so it is set here instead. This is
	 * what makes WeaponModule's weapon/ammo/magazine/level panels display the player's weapon.
	 */
	void RefreshWeaponSelection();

	// Each of these draws one region of the screen and is safe to call with a missing pawn/state.
	void DrawCrosshair(AMassShooterCharacter* Pawn);
	void DrawVitals(AMassShooterCharacter* Pawn);
	void DrawAmmo(AMassShooterCharacter* Pawn);
	void DrawMatchBanner(AMassShooterGameState* GS);
	void DrawObjectives(AMassShooterGameState* GS);
	void DrawKillFeed(AMassShooterGameState* GS);
	void DrawScoreboard(AMassShooterGameState* GS);
	void DrawRespawnPrompt(AMassShooterCharacter* Pawn);
	void DrawDamageIndicators();

	/**
	 * One slot per granted ability: its AbilityIcon, its KeyboardKey hint, and a dim overlay while
	 * it is on cooldown. Everything comes off UGameplayAbilityBase's own designer-facing fields, so
	 * an ability authored in Blueprint appears here with no extra wiring.
	 */
	void DrawAbilityBar(AMassShooterCharacter* Pawn);

	/** Filled bar with a border, in screen pixels. */
	void DrawBar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& FillColor);

	/** Text with a 1px offset shadow, so it stays readable over a bright sky. */
	void DrawShadowedText(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale = 1.f);

	/** Colour for a team id, matching the capture-point palette. */
	static FLinearColor GetTeamColor(int32 TeamId);

	float HitMarkerEndTime = -1.f;
	bool bHitMarkerLethal = false;

	struct FDamageDirection
	{
		FVector WorldLocation = FVector::ZeroVector;
		float EndTime = 0.f;
	};
	TArray<FDamageDirection> DamageDirections;

	/** Cached each DrawHUD so the draw helpers do not each re-resolve them. */
	UPROPERTY(Transient)
	TObjectPtr<AMassShooterPlayerController> ShooterPC;
};
