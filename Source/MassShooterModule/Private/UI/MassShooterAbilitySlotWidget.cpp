// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "UI/MassShooterAbilitySlotWidget.h"

#include "GAS/GameplayAbilityBase.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

void UMassShooterAbilitySlotWidget::SetAbilitySlot(int32 SlotIndex,
	TSubclassOf<UGameplayAbilityBase> AbilityClass, bool bOnCooldown)
{
	ShownAbility = AbilityClass;

	if (KeyText)
	{
		// Slots past 9 have no key, so they get no hint rather than a misleading one.
		KeyText->SetText(SlotIndex < 9 ? FText::AsNumber(SlotIndex + 1) : FText::GetEmpty());
	}

	const UGameplayAbilityBase* CDO = AbilityClass ? AbilityClass->GetDefaultObject<UGameplayAbilityBase>() : nullptr;
	UTexture2D* Icon = CDO ? CDO->AbilityIcon : nullptr;

	if (IconImage)
	{
		if (Icon)
		{
			IconImage->SetBrushFromTexture(Icon);
		}
		IconImage->SetVisibility(Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (NameText)
	{
		// Only as a stand-in for a missing icon: showing both at this size is unreadable.
		const bool bUseName = (CDO != nullptr) && (Icon == nullptr);
		NameText->SetText(bUseName ? FText::FromString(CDO->AbilityName.Left(3)) : FText::GetEmpty());
		NameText->SetVisibility(bUseName ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (CooldownOverlay)
	{
		CooldownOverlay->SetVisibility(bOnCooldown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	OnAbilitySlotChanged(SlotIndex, bOnCooldown);
}
