// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Components/MassShooterHealthComponent.h"
#include "MassShooterLog.h"
#include "Characters/Unit/UnitBase.h"
#include "GAS/AttributeSetBase.h"
#include "Net/UnrealNetwork.h"

UMassShooterHealthComponent::UMassShooterHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.f;
	SetIsReplicatedByDefault(true);
}

void UMassShooterHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UMassShooterHealthComponent, ObservedHealth);
	DOREPLIFETIME(UMassShooterHealthComponent, ObservedMaxHealth);
	DOREPLIFETIME(UMassShooterHealthComponent, ObservedShield);
	DOREPLIFETIME(UMassShooterHealthComponent, ObservedMaxShield);
}

void UMassShooterHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerUnit = Cast<AUnitBase>(GetOwner());
	if (!OwnerUnit)
	{
		UE_LOG(LogMassShooter, Error,
			TEXT("UMassShooterHealthComponent on %s, which is not an AUnitBase. Disabling."),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<null>"));
		SetComponentTickEnabled(false);
		return;
	}

	if (GetOwnerRole() == ROLE_Authority)
	{
		// ~6 s of re-application, matching how long RTSUnitTemplate's own attribute init can take
		// to land. See StatInitTicksLeft.
		StatInitTicksLeft = 12;
		StatInitAccumulator = 0.f;
		ApplyDefaultStats();
	}
}

void UMassShooterHealthComponent::ApplyDefaultStats()
{
	if (GetOwnerRole() != ROLE_Authority || !bOverrideStatsOnStart || !OwnerUnit || !OwnerUnit->Attributes)
	{
		return;
	}

	UAttributeSetBase* Attributes = OwnerUnit->Attributes;

	// BaseHealth is the load-bearing one: ALevelUnit's init recomputes MaxHealth from it, so
	// setting only Max/Health would be undone.
	Attributes->SetAttributeBaseHealth(DefaultMaxHealth);
	Attributes->SetAttributeMaxHealth(DefaultMaxHealth);
	Attributes->SetAttributeHealth(DefaultMaxHealth);

	// MaxShield/MaxMana are not derived from a base value, so a plain set holds. Set the max
	// before the current value — the setters clamp to it.
	Attributes->SetAttributeMaxShield(DefaultMaxShield);
	Attributes->SetAttributeShield(DefaultMaxShield);

	Attributes->SetAttributeMaxMana(DefaultMaxMana);
	Attributes->SetAttributeMana(DefaultMaxMana);
}

void UMassShooterHealthComponent::ResetForRespawn()
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	bDeadLatched = false;
	LastDamageInstigator.Reset();
	LastDamageTime = -1000.f;

	ApplyDefaultStats();
}

void UMassShooterHealthComponent::GrantSpawnProtection(float Seconds)
{
	if (const UWorld* World = GetWorld())
	{
		SpawnProtectionEndTime = World->GetTimeSeconds() + FMath::Max(0.f, Seconds);
	}
}

bool UMassShooterHealthComponent::IsSpawnProtected() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() < SpawnProtectionEndTime;
}

float UMassShooterHealthComponent::GetTimeSinceLastDamage() const
{
	const UWorld* World = GetWorld();
	return World ? (World->GetTimeSeconds() - LastDamageTime) : 1000.f;
}

namespace MassShooterHealthCVars
{
	// "Did my bullet actually hit?" is the first question every combat bug asks, and the answer
	// normally requires attaching a debugger to a running match. This makes it a log line.
	static int32 LogHits = 0;
	static FAutoConsoleVariableRef CVarLogHits(
		TEXT("Shooter.Debug.LogHits"), LogHits,
		TEXT("1 = log every registered projectile hit (victim, shooter, resulting health)."), ECVF_Cheat);
}

bool UMassShooterHealthComponent::IsHitLoggingEnabled()
{
	return MassShooterHealthCVars::LogHits != 0;
}

void UMassShooterHealthComponent::NotifyDamageFrom(AActor* DamageInstigator)
{
	if (GetOwnerRole() != ROLE_Authority || !DamageInstigator || DamageInstigator == GetOwner())
	{
		return;
	}

	LastDamageInstigator = DamageInstigator;
	LastDamageTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	if (MassShooterHealthCVars::LogHits && OwnerUnit && OwnerUnit->Attributes)
	{
		// Armor is logged because AUnitBase::HandleProjectileImpact subtracts it from the incoming
		// damage — an armour value at or above the shooter's attack damage silently nullifies every
		// hit, which looks exactly like "hits register but nothing happens".
		const UAttributeSetBase* A = OwnerUnit->Attributes;
		UE_LOG(LogMassShooter, Log,
			TEXT("HIT: %s <- %s | health %.1f/%.1f shield %.1f/%.1f armor %.1f magicres %.1f"),
			*GetOwner()->GetName(), *DamageInstigator->GetName(),
			A->GetHealth(), A->GetMaxHealth(), A->GetShield(), A->GetMaxShield(),
			A->GetArmor(), A->GetMagicResistance());
	}
}

void UMassShooterHealthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!OwnerUnit || !OwnerUnit->Attributes)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (GetOwnerRole() == ROLE_Authority)
	{
		// Bounded startup re-application (see StatInitTicksLeft).
		if (StatInitTicksLeft > 0)
		{
			StatInitAccumulator += DeltaTime;
			if (StatInitAccumulator >= 0.5f)
			{
				StatInitAccumulator = 0.f;
				--StatInitTicksLeft;
				ApplyDefaultStats();
			}
		}

		UAttributeSetBase* Attributes = OwnerUnit->Attributes;

		// Shield / mana regen. Health deliberately does NOT regenerate — a shooter's health bar is
		// the resource that makes trades matter; the shield is the part that comes back.
		if (!bDeadLatched && Attributes->GetMaxHealth() > 0.f)
		{
			if (ShieldRegenPerSecond > 0.f
				&& GetTimeSinceLastDamage() >= ShieldRegenDelay
				&& Attributes->GetShield() < Attributes->GetMaxShield())
			{
				Attributes->SetAttributeShield(
					FMath::Min(Attributes->GetMaxShield(), Attributes->GetShield() + ShieldRegenPerSecond * DeltaTime));
			}

			if (ManaRegenPerSecond > 0.f && Attributes->GetMana() < Attributes->GetMaxMana())
			{
				Attributes->SetAttributeMana(
					FMath::Min(Attributes->GetMaxMana(), Attributes->GetMana() + ManaRegenPerSecond * DeltaTime));
			}
		}

		// Spawn protection: top the health back up rather than trying to block the incoming effect
		// (we cannot intercept RTSUnitTemplate's damage path without modifying it, and would not
		// want to — this is equivalent from the player's side and costs one comparison).
		if (IsSpawnProtected() && !bDeadLatched && Attributes->GetMaxHealth() > 0.f
			&& Attributes->GetHealth() < Attributes->GetMaxHealth())
		{
			Attributes->SetAttributeHealth(Attributes->GetMaxHealth());
		}
	}

	// Poll at 20 Hz. Fast enough that a death is registered within one frame of a HUD update,
	// cheap enough to run on every bot in a wave.
	PollAccumulator += DeltaTime;
	if (PollAccumulator < 0.05f)
	{
		return;
	}
	PollAccumulator = 0.f;

	if (GetOwnerRole() == ROLE_Authority)
	{
		const UAttributeSetBase* Attributes = OwnerUnit->Attributes;

		const float NewHealth = Attributes->GetHealth();
		const float NewMaxHealth = Attributes->GetMaxHealth();

		if (!FMath::IsNearlyEqual(NewHealth, ObservedHealth) || !FMath::IsNearlyEqual(NewMaxHealth, ObservedMaxHealth))
		{
			ObservedHealth = NewHealth;
			ObservedMaxHealth = NewMaxHealth;
			OnHealthChanged.Broadcast(ObservedHealth, ObservedMaxHealth);
		}

		ObservedShield = Attributes->GetShield();
		ObservedMaxShield = Attributes->GetMaxShield();

		// MaxHealth > 0 gates out the startup window where the attributes are still all zero —
		// that is not a death.
		if (!bDeadLatched && ObservedMaxHealth > 0.f && ObservedHealth <= 0.f)
		{
			bDeadLatched = true;

			AActor* Killer = nullptr;
			if (GetTimeSinceLastDamage() <= KillCreditWindow)
			{
				Killer = LastDamageInstigator.Get();
			}

			UE_LOG(LogMassShooterMatch, Log, TEXT("%s died. Killer: %s"),
				*OwnerUnit->GetName(), Killer ? *Killer->GetName() : TEXT("<unknown>"));

			OnDeath.Broadcast(OwnerUnit, Killer);
		}
	}
	else if (!FMath::IsNearlyEqual(ObservedHealth, LastBroadcastHealth))
	{
		// Clients: the replicated Observed* values arrive on their own; fan the change out once per
		// actual change so widgets can react without polling themselves.
		LastBroadcastHealth = ObservedHealth;
		OnHealthChanged.Broadcast(ObservedHealth, ObservedMaxHealth);
	}
}
