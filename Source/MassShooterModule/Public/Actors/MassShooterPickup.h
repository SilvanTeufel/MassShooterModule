// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/MassShooterTypes.h"
#include "MassShooterPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/**
 * A respawning floor pickup: ammo, health or shield.
 *
 * RTSUnitTemplate ships APickup and WeaponModule ships AWeaponPickup, both of which hand over an
 * item or a weapon. Neither restores the resources a shooter map is paced around, and neither
 * comes back on a timer — which is the whole point of a map pickup. So this is its own actor
 * rather than a subclass that would fight its parent's lifecycle.
 */
UCLASS(Blueprintable)
class MASSSHOOTERMODULE_API AMassShooterPickup : public AActor
{
	GENERATED_BODY()

public:
	AMassShooterPickup();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Pickup")
	EMassShooterPickupKind Kind = EMassShooterPickupKind::Ammo;

	/** Health/shield restored. Ignored for ammo, which always refills whole magazines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Pickup")
	float Amount = 50.f;

	/** Grenades handed over alongside an ammo pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Pickup")
	int32 GrenadeAmount = 1;

	/** Seconds before this pickup comes back. 0 = one-shot, destroyed on use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Pickup")
	float RespawnSeconds = 20.f;

	/** Degrees per second the mesh spins, purely so it reads as a pickup at a glance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Pickup")
	float SpinRate = 90.f;

	UFUNCTION(BlueprintPure, Category = "MassShooter|Pickup")
	bool IsAvailable() const { return bAvailable; }

protected:
	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnRep_Available();

	/** Server: hand the contents to Taker. False when they could not use it (already full). */
	bool TryApply(class AMassShooterCharacter* Taker);

	/** Server: put the pickup back. */
	void Respawn();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter|Pickup", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter|Pickup", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(ReplicatedUsing = OnRep_Available, BlueprintReadOnly, Category = "MassShooter|Pickup")
	bool bAvailable = true;

	FTimerHandle RespawnTimer;
};
