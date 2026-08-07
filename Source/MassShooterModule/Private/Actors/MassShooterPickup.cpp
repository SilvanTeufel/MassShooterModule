// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Actors/MassShooterPickup.h"
#include "Characters/MassShooterCharacter.h"
#include "Components/MassShooterLoadoutComponent.h"
#include "Components/MassShooterHealthComponent.h"
#include "MassShooterLog.h"

#include "GAS/AttributeSetBase.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

AMassShooterPickup::AMassShooterPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	SetRootComponent(Trigger);
	Trigger->SetSphereRadius(90.f);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionObjectType(ECC_WorldDynamic);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Trigger);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AMassShooterPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMassShooterPickup, bAvailable);
}

void AMassShooterPickup::BeginPlay()
{
	Super::BeginPlay();

	// Overlap is bound on every machine but only acted on by the server; binding on clients too
	// keeps the delegate signature honest and costs nothing.
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AMassShooterPickup::OnOverlap);
}

void AMassShooterPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Mesh && bAvailable && SpinRate != 0.f)
	{
		Mesh->AddRelativeRotation(FRotator(0.f, SpinRate * DeltaSeconds, 0.f));
	}
}

void AMassShooterPickup::OnRep_Available()
{
	if (Mesh)
	{
		Mesh->SetVisibility(bAvailable, true);
	}
}

void AMassShooterPickup::OnOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (!HasAuthority() || !bAvailable)
	{
		return;
	}

	AMassShooterCharacter* Taker = Cast<AMassShooterCharacter>(OtherActor);
	if (!Taker || Taker->IsDeadShooter())
	{
		return;
	}

	// Refuse rather than consume when the player cannot use it — walking over a health pack at
	// full health should not waste it.
	if (!TryApply(Taker))
	{
		return;
	}

	bAvailable = false;
	OnRep_Available();

	if (RespawnSeconds > 0.f)
	{
		GetWorldTimerManager().SetTimer(RespawnTimer, this, &AMassShooterPickup::Respawn, RespawnSeconds, false);
	}
	else
	{
		Destroy();
	}
}

bool AMassShooterPickup::TryApply(AMassShooterCharacter* Taker)
{
	switch (Kind)
	{
	case EMassShooterPickupKind::Ammo:
	{
		UMassShooterLoadoutComponent* Loadout = Taker->GetLoadout();
		if (!Loadout)
		{
			return false;
		}

		const bool bAmmoTaken = Loadout->ReplenishCurrentAmmo();
		const bool bGrenadesTaken = GrenadeAmount > 0 && Loadout->GetGrenades() < Loadout->MaxGrenades;

		if (bGrenadesTaken)
		{
			Loadout->AddGrenades(GrenadeAmount);
		}
		return bAmmoTaken || bGrenadesTaken;
	}

	case EMassShooterPickupKind::Health:
	{
		UAttributeSetBase* Attributes = Taker->Attributes;
		if (!Attributes || Attributes->GetHealth() >= Attributes->GetMaxHealth())
		{
			return false;
		}
		Attributes->SetAttributeHealth(FMath::Min(Attributes->GetMaxHealth(), Attributes->GetHealth() + Amount));
		return true;
	}

	case EMassShooterPickupKind::Shield:
	{
		UAttributeSetBase* Attributes = Taker->Attributes;
		if (!Attributes || Attributes->GetShield() >= Attributes->GetMaxShield())
		{
			return false;
		}
		Attributes->SetAttributeShield(FMath::Min(Attributes->GetMaxShield(), Attributes->GetShield() + Amount));
		return true;
	}
	}

	return false;
}

void AMassShooterPickup::Respawn()
{
	if (!HasAuthority())
	{
		return;
	}

	bAvailable = true;
	OnRep_Available();
}
