// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Components/MassShooterLoadoutComponent.h"
#include "MassShooterLog.h"
#include "Components/WeaponComponent.h"
#include "Components/MassShooterCombatComponent.h"
#include "Characters/MassShooterCharacter.h"
#include "Abilities/WeaponAttributeSet.h"
#include "Characters/Unit/UnitBase.h"
#include "Net/UnrealNetwork.h"

UMassShooterLoadoutComponent::UMassShooterLoadoutComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMassShooterLoadoutComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMassShooterLoadoutComponent, Grenades);
}

void UMassShooterLoadoutComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerUnit = Cast<AUnitBase>(GetOwner());
	Weapon = GetOwner() ? GetOwner()->FindComponentByClass<UWeaponComponent>() : nullptr;

	if (!Weapon)
	{
		UE_LOG(LogMassShooter, Warning,
			TEXT("%s has a MassShooterLoadoutComponent but no WeaponModule UWeaponComponent — it will carry nothing."),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<null>"));
	}

	if (GetOwnerRole() == ROLE_Authority)
	{
		Grenades = FMath::Clamp(StartGrenades, 0, MaxGrenades);
	}
}

int32 UMassShooterLoadoutComponent::GetWeaponCount() const
{
	return Weapon ? Weapon->AvailableWeapons.Num() : 0;
}

int32 UMassShooterLoadoutComponent::GetCurrentWeaponIndex() const
{
	return Weapon ? Weapon->CurrentWeaponIndex : INDEX_NONE;
}

FString UMassShooterLoadoutComponent::GetCurrentWeaponName() const
{
	if (!Weapon || !Weapon->AvailableWeapons.IsValidIndex(Weapon->CurrentWeaponIndex))
	{
		return FString();
	}
	return Weapon->GetCurrentWeaponData().WeaponName;
}

float UMassShooterLoadoutComponent::GetCurrentAmmo() const
{
	// Prefer the live GAS attribute over the FWeaponData struct.
	//
	// WeaponModule spends ammo by modifying the UWeaponAttributeSet; the struct is only written
	// back on a weapon switch (SaveAttributesToWeapon). Reading the struct therefore shows a stale
	// count — measured in a PIE session: the HUD read "0 / 3 magazines" while the reload ability
	// was already reporting "No magazines left", because the attribute had reached 0 and the
	// struct still said 3.
	if (Weapon && Weapon->WeaponAttributes)
	{
		return Weapon->WeaponAttributes->GetAmmo();
	}

	if (!Weapon || !Weapon->AvailableWeapons.IsValidIndex(Weapon->CurrentWeaponIndex))
	{
		return 0.f;
	}
	return Weapon->GetCurrentWeaponData().Ammo;
}

float UMassShooterLoadoutComponent::GetCurrentMagazineSize() const
{
	if (!Weapon || !Weapon->AvailableWeapons.IsValidIndex(Weapon->CurrentWeaponIndex))
	{
		return 0.f;
	}
	const FWeaponData Data = Weapon->GetCurrentWeaponData();

	// MaxAmmoSpec is the talent-upgraded magazine size; it is 0 until a point is invested.
	return Data.MaxAmmoSpec > 0.f ? Data.MaxAmmoSpec : Data.MaxAmmo;
}

float UMassShooterLoadoutComponent::GetCurrentMagazineCount() const
{
	// Same divergence as GetCurrentAmmo: the attribute is authoritative during play. Reading the
	// struct here also made the auto-reload retry forever — it saw magazines that were not there
	// and re-activated the reload ability twice a second.
	if (Weapon && Weapon->WeaponAttributes)
	{
		return Weapon->WeaponAttributes->GetAmountMagazines();
	}

	if (!Weapon || !Weapon->AvailableWeapons.IsValidIndex(Weapon->CurrentWeaponIndex))
	{
		return 0.f;
	}
	return Weapon->GetCurrentWeaponData().AmountMagazines;
}

float UMassShooterLoadoutComponent::GetCurrentFireInterval() const
{
	if (!Weapon || !Weapon->AvailableWeapons.IsValidIndex(Weapon->CurrentWeaponIndex))
	{
		return 0.25f;
	}

	const FWeaponData Data = Weapon->GetCurrentWeaponData();

	// CooldownTime is the designer value; CooldownMultiplier is what talents shrink. Floor it so a
	// heavily upgraded weapon cannot ask the client to send a fire RPC every frame.
	const float Interval = Data.CooldownTime * (Data.CooldownMultiplier > 0.f ? Data.CooldownMultiplier : 1.f);
	return FMath::Max(0.05f, Interval);
}

void UMassShooterLoadoutComponent::SelectWeapon(int32 Index)
{
	if (!Weapon || !Weapon->AvailableWeapons.IsValidIndex(Index) || Index == Weapon->CurrentWeaponIndex)
	{
		return;
	}

	// WeaponModule's own server RPC — server-authoritative, so a client cannot equip a weapon it
	// does not carry.
	Weapon->Server_SwitchWeapon(Index);
}

void UMassShooterLoadoutComponent::CycleWeapon(int32 Delta)
{
	const int32 Count = GetWeaponCount();
	if (Count <= 1 || Delta == 0)
	{
		return;
	}

	const int32 Current = FMath::Max(0, GetCurrentWeaponIndex());
	// Double modulo so a negative Delta wraps instead of producing a negative index.
	const int32 Next = ((Current + Delta) % Count + Count) % Count;

	// Vorwaerts geht ueber die Faehigkeit, nicht ueber den direkten Tausch: nur so laufen
	// Cast-Leiste und Wechsel-Montage, genau wie beim WeaponModule-Charakter.
	//
	// Warum nur vorwaerts: USwitchWeaponAbility::PerformSwitchWeapon schaltet fest auf
	// (CurrentWeaponIndex + 1) und nimmt kein Ziel entgegen. Rueckwaerts bliebe nur, den Index
	// vorher zu verbiegen - das waere ein kurzzeitig falscher, replizierter Zustand. Deshalb
	// tauscht das Mausrad nach unten weiterhin stumm; um auch dort einen Cast zu bekommen,
	// muesste die Faehigkeit im WeaponModule einen Zielindex lernen.
	if (Next == (Current + 1) % Count)
	{
		if (const AMassShooterCharacter* Shooter = Cast<AMassShooterCharacter>(GetOwner()))
		{
			if (UMassShooterCombatComponent* Combat = Shooter->GetCombat())
			{
				if (Combat->TryStartWeaponSwitchAbility())
				{
					return;
				}
			}
		}
	}

	SelectWeapon(Next);
}

bool UMassShooterLoadoutComponent::ReplenishCurrentAmmo()
{
	if (GetOwnerRole() != ROLE_Authority || !Weapon)
	{
		return false;
	}

	const int32 Index = Weapon->CurrentWeaponIndex;
	if (!Weapon->AvailableWeapons.IsValidIndex(Index))
	{
		return false;
	}

	FWeaponData& Data = Weapon->AvailableWeapons[Index];
	const float FullMag = Data.MaxAmmoSpec > 0.f ? Data.MaxAmmoSpec : Data.MaxAmmo;
	const float FullMags = Data.MaxMagazinesSpec > 0.f ? Data.MaxMagazinesSpec : Data.MaxMagazines;

	if (FMath::IsNearlyEqual(Data.Ammo, FullMag) && FMath::IsNearlyEqual(Data.AmountMagazines, FullMags))
	{
		return false;
	}

	Data.Ammo = FullMag;
	Data.AmountMagazines = FullMags;

	// Push the data back into the GAS attributes the shoot/reload abilities actually read. This is
	// WeaponModule's own public sync entry point — the same one its reload path uses.
	Weapon->SyncAttributesFromWeapon(Index);
	return true;
}

void UMassShooterLoadoutComponent::ReplenishAllAmmo()
{
	if (GetOwnerRole() != ROLE_Authority || !Weapon)
	{
		return;
	}

	for (int32 Index = 0; Index < Weapon->AvailableWeapons.Num(); ++Index)
	{
		FWeaponData& Data = Weapon->AvailableWeapons[Index];
		Data.Ammo = Data.MaxAmmoSpec > 0.f ? Data.MaxAmmoSpec : Data.MaxAmmo;
		Data.AmountMagazines = Data.MaxMagazinesSpec > 0.f ? Data.MaxMagazinesSpec : Data.MaxMagazines;
	}

	// Only the equipped weapon's values live in the attribute set, so sync that one.
	if (Weapon->AvailableWeapons.IsValidIndex(Weapon->CurrentWeaponIndex))
	{
		Weapon->SyncAttributesFromWeapon(Weapon->CurrentWeaponIndex);
	}
}

bool UMassShooterLoadoutComponent::ConsumeGrenade()
{
	if (GetOwnerRole() != ROLE_Authority || Grenades <= 0)
	{
		return false;
	}

	--Grenades;
	return true;
}

void UMassShooterLoadoutComponent::AddGrenades(int32 Count)
{
	if (GetOwnerRole() != ROLE_Authority || Count <= 0)
	{
		return;
	}

	Grenades = FMath::Clamp(Grenades + Count, 0, MaxGrenades);
}

void UMassShooterLoadoutComponent::ResetLoadout()
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	ReplenishAllAmmo();
	Grenades = FMath::Clamp(StartGrenades, 0, MaxGrenades);
	SelectWeapon(0);
}
