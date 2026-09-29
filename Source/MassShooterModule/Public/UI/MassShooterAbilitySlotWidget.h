// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MassShooterAbilitySlotWidget.generated.h"

class UGameplayAbilityBase;
class UImage;
class UTextBlock;

/**
 * One slot of the ability bar.
 *
 * Every visual is optional (BindWidgetOptional): a project that wants only an icon can leave the
 * key and name out and still use this class. That is the point of binding rather than building the
 * tree in C++ - the layout and the materials stay in the Blueprint where a designer can reach them,
 * and this class only supplies what the slot should currently say.
 */
UCLASS(Abstract)
class MASSSHOOTERMODULE_API UMassShooterAbilitySlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Point the slot at an ability. SlotIndex is what the player actually presses.
	 *
	 * The key hint is the slot number, NOT UGameplayAbilityBase::KeyboardKey: that field defaults
	 * to "X" and most abilities never change it, so using it produced a row of X's matching no key
	 * at all. Keys 1..9 map to slots 0..8.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|HUD")
	void SetAbilitySlot(int32 SlotIndex, TSubclassOf<UGameplayAbilityBase> AbilityClass, bool bOnCooldown);

	/** The ability currently shown, so the owning bar can skip redundant refreshes. */
	UFUNCTION(BlueprintPure, Category = "MassShooter|HUD")
	TSubclassOf<UGameplayAbilityBase> GetAbilityClass() const { return ShownAbility; }

protected:
	/** Icon from UGameplayAbilityBase::AbilityIcon. Hidden when the ability has none. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UImage> IconImage;

	/** Slot number, 1-based. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UTextBlock> KeyText;

	/** Falls back to the ability name when there is no icon, so an empty-looking slot still reads. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UTextBlock> NameText;

	/** Shown while the ability is on cooldown; typically a dark translucent overlay. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MassShooter|HUD")
	TObjectPtr<UImage> CooldownOverlay;

	/** Blueprint hook for anything the bindings above cannot express (flash, sound, animation). */
	UFUNCTION(BlueprintImplementableEvent, Category = "MassShooter|HUD")
	void OnAbilitySlotChanged(int32 SlotIndex, bool bOnCooldown);

private:
	UPROPERTY(Transient)
	TSubclassOf<UGameplayAbilityBase> ShownAbility;
};
