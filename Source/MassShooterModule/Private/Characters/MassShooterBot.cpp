// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Characters/MassShooterBot.h"
#include "Characters/MassShooterCharacter.h"
#include "Components/MassShooterHealthComponent.h"
#include "MassShooterLog.h"

#include "Mass/MassActorBindingComponent.h"
#include "GAS/AttributeSetBase.h"
#include "Components/WeaponComponent.h"
#include "Components/CapsuleComponent.h"

AMassShooterBot::AMassShooterBot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ShooterHealth = CreateDefaultSubobject<UMassShooterHealthComponent>(TEXT("ShooterHealth"));
	ShooterHealth->DefaultMaxHealth = 80.f;
	ShooterHealth->DefaultMaxShield = 0.f;
	ShooterHealth->ShieldRegenPerSecond = 0.f;

	WeaponComp = CreateDefaultSubobject<UWeaponComponent>(TEXT("WeaponComp"));

	ProjectileSpawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ProjectileSpawnPoint"));
	ProjectileSpawnPoint->SetupAttachment(GetCapsuleComponent());
	ProjectileSpawnPoint->SetRelativeLocation(FVector(50.f, 0.f, 30.f));
	ProjectileSpawnPoint->ComponentTags.Add(FName(TEXT("ProjectileSpawn")));

	// Ranged by default: a mass shooter's hostiles shoot back.
	UseProjectile = true;

	// AGASUnit ships this OFF; RTSUnitTemplate's own unit Blueprints switch it on. A bot that
	// never runs detection never acquires a target and therefore never fires a shot, so it has to
	// be on for a hostile that is supposed to fight.
	ToggleUnitDetection = true;
}

void AMassShooterBot::BeginPlay()
{
	Super::BeginPlay();

	// Bots move continuously too, and there are dozens of them. See
	// AMassShooterCharacter::DetachFromNavigation for what leaving them in the navigation octree
	// costs on a navmesh with RuntimeGeneration = Dynamic.
	AMassShooterCharacter::DetachFromNavigation(this);

	// Perception range MUST be widened before the entity is created: the binding component's
	// SightRadius/LoseSightRadius are copied into FMassCombatStatsFragment at build time, so a
	// later change would not reach the entity. See SightRadiusOverride for why the RTS default
	// leaves hostiles standing at their spawner.
	if (MassActorBindingComponent && SightRadiusOverride > 0.f)
	{
		MassActorBindingComponent->SightRadius = SightRadiusOverride;
		MassActorBindingComponent->LoseSightRadius =
			FMath::Max(LoseSightRadiusOverride, SightRadiusOverride * 1.2f);
	}

	// RTS units build their Mass entity from their Blueprint's BeginPlay via SetupMassOnActor,
	// which has no C++ caller in the base plugin. Calling it here means a bot works as a bare C++
	// class too, and the call is idempotent, so a Blueprint that also calls it is harmless.
	if (MassActorBindingComponent)
	{
		MassActorBindingComponent->SetupMassOnActor();
	}
}

void AMassShooterBot::ApplyAttackDamageOverride()
{
	if (!HasAuthority() || !Attributes || AttackDamageOverride <= 0.f)
	{
		return;
	}

	// Base first: RTSUnitTemplate's attribute init derives AttackDamage from BaseAttackDamage, so
	// writing only the derived value gets recomputed away.
	Attributes->SetAttributeBaseAttackDamage(AttackDamageOverride);
	Attributes->SetAttributeAttackDamage(AttackDamageOverride);

	// Open the re-application window (see TickAttackDamageWindow). Re-armed on every explicit
	// call, so the spawner's post-init write also restarts it.
	AttackDamageWindowTicksLeft = 12;
	GetWorldTimerManager().SetTimer(AttackDamageWindowTimer, this,
		&AMassShooterBot::TickAttackDamageWindow, 0.5f, true, 0.5f);
}

void AMassShooterBot::TickAttackDamageWindow()
{
	if (HasAuthority() && Attributes && AttackDamageOverride > 0.f)
	{
		Attributes->SetAttributeBaseAttackDamage(AttackDamageOverride);
		Attributes->SetAttributeAttackDamage(AttackDamageOverride);
	}

	if (--AttackDamageWindowTicksLeft <= 0)
	{
		GetWorldTimerManager().ClearTimer(AttackDamageWindowTimer);
	}
}

void AMassShooterBot::ApplyWaveScaling(float HealthMultiplier, float DamageMultiplier)
{
	if (!HasAuthority())
	{
		return;
	}

	if (ShooterHealth && HealthMultiplier > 0.f)
	{
		ShooterHealth->DefaultMaxHealth *= HealthMultiplier;

		// Re-run the write so the new value lands even if the startup window has already closed.
		ShooterHealth->ApplyDefaultStats();
	}

	if (Attributes && DamageMultiplier > 0.f)
	{
		// BaseAttackDamage is what RTSUnitTemplate's init derives AttackDamage from, so scaling
		// only AttackDamage would be undone the next time that init runs.
		// Named BaseDamage, not Base: AWorkingUnitBase already has a `Base` member (the worker's
		// home building), and the shadow is an error under this project's warning settings.
		const float BaseDamage = Attributes->GetBaseAttackDamage() > 0.f
			? Attributes->GetBaseAttackDamage()
			: Attributes->GetAttackDamage();

		// Refuse to scale an uninitialised stat block. Multiplying zero writes zero, and a bot with
		// AttackDamage 0 fires projectiles carrying zero damage — which AUnitBase::HandleProjectileImpact
		// then applies faithfully as nothing. Measured before the spawner's ordering was fixed:
		// 1788 registered hits on the player, health never moved.
		if (BaseDamage > 0.f)
		{
			Attributes->SetAttributeBaseAttackDamage(BaseDamage * DamageMultiplier);
			Attributes->SetAttributeAttackDamage(BaseDamage * DamageMultiplier);
		}
		else
		{
			UE_LOG(LogMassShooter, Warning,
				TEXT("%s: ApplyWaveScaling ran before the attributes were initialised - damage left untouched."),
				*GetName());
		}
	}
}

void AMassShooterBot::HandleProjectileImpact_Implementation(AActor* Shooter, const FVector& ImpactLocation,
	TSubclassOf<AProjectile> ProjectileClass, float DamageOverride,
	TSubclassOf<UGameplayEffect> ProjectileEffect,
	TSubclassOf<UGameplayEffect> ProjectileEffect2,
	TSubclassOf<UGameplayEffect> ProjectileEffect3)
{
	if (ShooterHealth)
	{
		ShooterHealth->NotifyDamageFrom(Shooter);
	}

	// Same zero-damage compensation as the player pawn — see ResolveImpactDamage. Without it a
	// player's shot can land on a bot for nothing just as easily as the reverse.
	const float EffectiveDamage =
		AMassShooterCharacter::ResolveImpactDamage(Shooter, DamageOverride, bTreatZeroDamageAsAttackDamage);

	Super::HandleProjectileImpact_Implementation(Shooter, ImpactLocation, ProjectileClass, EffectiveDamage,
		ProjectileEffect, ProjectileEffect2, ProjectileEffect3);

	// A bot has no HUD of its own, but whoever shot it does — this is what puts a hitmarker on
	// screen when a player lands a round on a hostile. Health is read after Super so the lethal
	// flag describes this hit.
	const bool bLethal = Attributes && Attributes->GetMaxHealth() > 0.f && Attributes->GetHealth() <= 0.f;
	AMassShooterCharacter::ReportHitToHUDs(this, Shooter, bLethal);
}
