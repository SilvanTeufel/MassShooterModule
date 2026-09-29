// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "UI/MassShooterHudWidget.h"

#include "UI/MassShooterAbilitySlotWidget.h"
#include "Characters/MassShooterCharacter.h"
#include "Components/MassShooterHealthComponent.h"
#include "Components/MassShooterLoadoutComponent.h"
#include "Controller/MassShooterPlayerController.h"

#include "Components/PanelWidget.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "GAS/GameplayAbilityBase.h"   // AbilityName fuer die Cast-Label
#include "Core/UnitData.h"             // UnitData::Casting

void UMassShooterHudWidget::SetShooterPawn(AMassShooterCharacter* InPawn)
{
	ShooterPawn = InPawn;
}

void UMassShooterHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Re-resolve rather than cache once: the player respawns into a NEW pawn, and a widget holding
	// the corpse would freeze at the health it had when it died.
	AMassShooterCharacter* Pawn = ShooterPawn.Get();
	if (!Pawn)
	{
		if (const AMassShooterPlayerController* PC = Cast<AMassShooterPlayerController>(GetOwningPlayer()))
		{
			Pawn = PC->GetShooterCharacter();
		}
		if (!Pawn)
		{
			Pawn = Cast<AMassShooterCharacter>(GetOwningPlayerPawn());
		}
		ShooterPawn = Pawn;
	}

	RefreshVitals(Pawn);
	RefreshAmmo(Pawn);
	RefreshAbilities(Pawn);
	RefreshCast(Pawn);

	OnHudRefreshed(Pawn);
}

void UMassShooterHudWidget::RefreshVitals(AMassShooterCharacter* Pawn)
{
	const UMassShooterHealthComponent* Health = Pawn ? Pawn->GetShooterHealth() : nullptr;

	if (SpawnProtectedBox)
	{
		SpawnProtectedBox->SetVisibility(Health && Health->IsSpawnProtected()
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (!Health)
	{
		return;
	}

	// Max of at least 1 so a pawn whose attributes have not initialised yet shows an empty bar
	// instead of dividing by zero.
	const float MaxHealth = FMath::Max(1.f, Health->GetMaxHealthValue());
	if (HealthBar)
	{
		HealthBar->SetPercent(FMath::Clamp(Health->GetHealthValue() / MaxHealth, 0.f, 1.f));
	}
	if (HealthText)
	{
		HealthText->SetText(FText::FromString(
			FString::Printf(TEXT("%.0f / %.0f"), Health->GetHealthValue(), MaxHealth)));
	}

	// A pawn with no shield hides the whole row rather than showing a permanently empty bar.
	const float MaxShield = Health->GetMaxShieldValue();
	const bool bHasShield = MaxShield > 0.f;
	if (ShieldBar)
	{
		ShieldBar->SetVisibility(bHasShield ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bHasShield)
		{
			ShieldBar->SetPercent(FMath::Clamp(Health->GetShieldValue() / MaxShield, 0.f, 1.f));
		}
	}
	if (ShieldText)
	{
		ShieldText->SetVisibility(bHasShield ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bHasShield)
		{
			ShieldText->SetText(FText::FromString(
				FString::Printf(TEXT("%.0f / %.0f"), Health->GetShieldValue(), MaxShield)));
		}
	}
}

void UMassShooterHudWidget::RefreshAmmo(AMassShooterCharacter* Pawn)
{
	const UMassShooterLoadoutComponent* Loadout = Pawn ? Pawn->GetLoadout() : nullptr;
	if (!Loadout)
	{
		return;
	}

	if (AmmoText)
	{
		const float Ammo = Loadout->GetCurrentAmmo();
		AmmoText->SetText(FText::FromString(
			FString::Printf(TEXT("%.0f / %.0f"), Ammo, Loadout->GetCurrentMagazineCount())));
		AmmoText->SetColorAndOpacity(FSlateColor(Ammo <= 0.f ? EmptyAmmoColor : NormalAmmoColor));
	}

	if (WeaponNameText)
	{
		WeaponNameText->SetText(FText::FromString(Loadout->GetCurrentWeaponName()));
	}

	if (GrenadeText)
	{
		GrenadeText->SetText(FText::FromString(
			FString::Printf(TEXT("Grenades  %d"), Loadout->GetGrenades())));
	}
}

void UMassShooterHudWidget::RefreshAbilities(AMassShooterCharacter* Pawn)
{
	if (!AbilityContainer || !AbilitySlotClass)
	{
		return;
	}

	const TArray<TSubclassOf<UGameplayAbilityBase>>& Abilities = Pawn ? Pawn->DefaultAbilities
		: TArray<TSubclassOf<UGameplayAbilityBase>>();

	// Grow the slot list to match, never rebuild it: recreating widgets every tick would restart
	// any Blueprint animation on them and churn the widget pool for no reason.
	while (AbilitySlots.Num() < Abilities.Num())
	{
		UMassShooterAbilitySlotWidget* NewSlot = CreateWidget<UMassShooterAbilitySlotWidget>(
			GetOwningPlayer(), AbilitySlotClass);
		if (!NewSlot)
		{
			break;
		}
		UPanelSlot* PanelSlot = AbilityContainer->AddChild(NewSlot);

		// The gap belongs on the container slot, not inside the slot widget: a widget cannot pad
		// itself outwards, and putting it here keeps the frame's own SizeBox square.
		if (UHorizontalBoxSlot* BoxSlot = Cast<UHorizontalBoxSlot>(PanelSlot))
		{
			BoxSlot->SetPadding(FMargin(AbilitySlotPadding, 0.f));
			BoxSlot->SetVerticalAlignment(VAlign_Bottom);
		}

		AbilitySlots.Add(NewSlot);
	}

	for (int32 Index = 0; Index < AbilitySlots.Num(); ++Index)
	{
		UMassShooterAbilitySlotWidget* SlotWidget = AbilitySlots[Index];
		if (!SlotWidget)
		{
			continue;
		}

		if (!Abilities.IsValidIndex(Index))
		{
			// Keep the widget, hide it - the pawn may swap to a loadout with more abilities again.
			SlotWidget->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}

		SlotWidget->SetVisibility(ESlateVisibility::HitTestInvisible);

		// IsAbilityOnCooldownByClass is RTSUnitTemplate's own query and works for Blueprint
		// abilities without needing their spec handle.
		const bool bOnCooldown = Pawn && Pawn->IsAbilityOnCooldownByClass(Abilities[Index]);
		SlotWidget->SetAbilitySlot(Index, Abilities[Index], bOnCooldown);
	}
}

void UMassShooterHudWidget::RefreshCast(AMassShooterCharacter* Pawn)
{
	// Dieselbe Rechnung wie UUnitTimerWidget::TimerTick im Zweig UnitData::Casting: Anteil aus
	// UnitControlTimer und CastTime, und sichtbar nur, solange der Cast wirklich laeuft.
	//
	// Der Zustand ist der Traeger, nicht die Ability: Reload und Waffenwechsel setzen ueber
	// bUseCastingFallbackProcessor den Casting-Tag, und daran haengt sowohl die Leiste als auch
	// die Cast-Animation. Eine Ability ohne dieses Flag (Schiessen, Granate, Dash) taucht hier
	// deshalb absichtlich nicht auf.
	bool bShow = false;
	float Percent = 0.f;

	if (Pawn && Pawn->GetUnitState() == UnitData::Casting)
	{
		const float Duration = Pawn->CastTime;
		Percent = (Duration > KINDA_SMALL_NUMBER)
			? FMath::Clamp(Pawn->UnitControlTimer / Duration, 0.f, 1.f)
			: 1.f;
		bShow = (Percent > 0.f && Percent < 1.f);
	}

	if (CastBar)
	{
		CastBar->SetPercent(Percent);
		CastBar->SetFillColorAndOpacity(CastColor);
	}

	if (CastText && bShow)
	{
		// AbilityName steht auf der laufenden Instanz und traegt im Bestand ein angehaengtes
		// ":\n\n" (siehe UGameplayAbilityBase::AbilityName) - das waere im HUD eine leere
		// zweite Zeile, deshalb wird ab dem Doppelpunkt abgeschnitten.
		FString Label;
		if (const UGameplayAbilityBase* RunningAbility = Pawn->ActivatedAbilityInstance)
		{
			Label = RunningAbility->AbilityName;
			int32 ColonIndex = INDEX_NONE;
			if (Label.FindChar(TEXT(':'), ColonIndex))
			{
				Label = Label.Left(ColonIndex);
			}
			Label.TrimStartAndEndInline();
		}
		CastText->SetText(FText::FromString(Label));
	}

	const ESlateVisibility NewVisibility = bShow
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed;

	// CastBox ist die uebliche Klammer. Fehlt sie im Blueprint, werden Leiste und Text einzeln
	// geschaltet - sonst bliebe eine leere Leiste stehen, wenn nur CastBar gebunden ist.
	if (CastBox)
	{
		CastBox->SetVisibility(NewVisibility);
	}
	else
	{
		if (CastBar)
		{
			CastBar->SetVisibility(NewVisibility);
		}
		if (CastText)
		{
			CastText->SetVisibility(NewVisibility);
		}
	}
}
