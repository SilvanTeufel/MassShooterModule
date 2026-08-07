// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MassShooterLoadoutComponent.generated.h"

class UWeaponComponent;
class AUnitBase;

/**
 * The shooter's weapon wheel and throwables.
 *
 * WeaponModule's UWeaponComponent already owns the weapon list, ammo and per-weapon talents, and
 * exposes Server_SwitchWeapon. This component is the thin layer above it that a shooter needs and
 * an RTS unit does not: cycling with the mouse wheel / number keys, an ammo-reserve top-up on
 * resupply, and a grenade charge count. It never duplicates weapon state — every query forwards
 * to the UWeaponComponent so there is exactly one source of truth.
 */
UCLASS(ClassGroup = (MassShooter), meta = (BlueprintSpawnableComponent))
class MASSSHOOTERMODULE_API UMassShooterLoadoutComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMassShooterLoadoutComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The WeaponModule component this loadout drives (found on the owner at BeginPlay). */
	UFUNCTION(BlueprintPure, Category = "MassShooter|Loadout")
	UWeaponComponent* GetWeapon() const { return Weapon; }

	/** Number of weapons the owner currently carries. */
	UFUNCTION(BlueprintPure, Category = "MassShooter|Loadout")
	int32 GetWeaponCount() const;

	UFUNCTION(BlueprintPure, Category = "MassShooter|Loadout")
	int32 GetCurrentWeaponIndex() const;

	UFUNCTION(BlueprintPure, Category = "MassShooter|Loadout")
	FString GetCurrentWeaponName() const;

	UFUNCTION(BlueprintPure, Category = "MassShooter|Loadout")
	float GetCurrentAmmo() const;

	UFUNCTION(BlueprintPure, Category = "MassShooter|Loadout")
	float GetCurrentMagazineSize() const;

	UFUNCTION(BlueprintPure, Category = "MassShooter|Loadout")
	float GetCurrentMagazineCount() const;

	/** Seconds between shots for the equipped weapon, after its multipliers. */
	UFUNCTION(BlueprintPure, Category = "MassShooter|Loadout")
	float GetCurrentFireInterval() const;

	/** Client entry: select an absolute slot. Hops to the server via WeaponModule. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Loadout")
	void SelectWeapon(int32 Index);

	/** Client entry: cycle by +1 / -1, wrapping. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Loadout")
	void CycleWeapon(int32 Delta);

	/** Server: refill the magazine reserve of every carried weapon (ammo pickups, resupply). */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Loadout")
	void ReplenishAllAmmo();

	/** Server: refill just the equipped weapon. Returns false when it was already full. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Loadout")
	bool ReplenishCurrentAmmo();

	/** Grenades currently held. */
	UFUNCTION(BlueprintPure, Category = "MassShooter|Loadout")
	int32 GetGrenades() const { return Grenades; }

	/** Server: consume one grenade. False when empty. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Loadout")
	bool ConsumeGrenade();

	/** Server: hand out grenades (pickup, respawn). Clamped to MaxGrenades. */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Loadout")
	void AddGrenades(int32 Count);

	/** Server: restore the spawn loadout (full ammo everywhere, grenades back to StartGrenades). */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Loadout")
	void ResetLoadout();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Loadout", meta = (ClampMin = "0"))
	int32 StartGrenades = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Loadout", meta = (ClampMin = "0"))
	int32 MaxGrenades = 4;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UWeaponComponent> Weapon;

	UPROPERTY(Transient)
	TObjectPtr<AUnitBase> OwnerUnit;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Loadout")
	int32 Grenades = 0;
};
