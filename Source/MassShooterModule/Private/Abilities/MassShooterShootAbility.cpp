// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Abilities/MassShooterShootAbility.h"
#include "Characters/MassShooterCharacter.h"
#include "Components/MassShooterCombatComponent.h"
#include "MassShooterLog.h"

#include "Characters/Unit/UnitBase.h"
#include "Controller/PlayerController/ExtendedControllerBase.h"
#include "Components/WeaponComponent.h"
#include "Abilities/WeaponAttributeSet.h"

#include "Engine/World.h"
#include "TimerManager.h"
#include "Animations/UnitBaseAnimInstance.h"  // Anzeige-Zustand fuer die Schussanimation
#include "Components/SkeletalMeshComponent.h"
#include "Mass/UnitMassTag.h"                 // FMassStateContinuousAttackTag
#include "MassEntityManager.h"

UMassShooterShootAbility::UMassShooterShootAbility()
{
	// Server-only, like every other RTSUnitTemplate ability: the projectile is authoritative and
	// replicates itself. InstancedPerActor so the repeating timer has somewhere to live.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	// NOT continuous: bIsContinuousAbility makes UGameplayAbilityBase tag the unit with
	// FMassStateContinuousAttackTag and hand firing to the RTS attack state, which aims at a
	// perceived target rather than at the crosshair.
	//
	// Die ANZEIGE-Haelfte dieses Flags brauchen wir aber trotzdem, sonst spielt die
	// Schussanimation nur einmal je Eintritt in Attack. Deshalb setzt SetContinuousAttackTag den
	// Tag selbst - ohne das Flag und damit ohne den fremden Feuerpfad.
	bIsContinuousAbility = false;
}

AUnitBase* UMassShooterShootAbility::GetShootingUnit() const
{
	return Cast<AUnitBase>(GetAvatarActorFromActorInfo());
}

void UMassShooterShootAbility::SetContinuousAttackTag(bool bAdd)
{
	AUnitBase* Unit = GetShootingUnit();
	if (!Unit)
	{
		return;
	}

	FMassEntityManager* EntityManager = nullptr;
	FMassEntityHandle Entity;
	if (!Unit->GetMassEntityData(EntityManager, Entity) || !EntityManager || !Entity.IsSet()
		|| !EntityManager->IsEntityValid(Entity))
	{
		return;
	}

	if (bAdd)
	{
		// Nullen wie in AExtendedControllerBase::BatchSetRotateToMouseTagLocally, damit der Zyklus
		// am Anfang steht und nicht mitten in einer Schussphase.
		//
		// WICHTIG: auf einem Client wirkt das hier NICHT - dort nullt
		// ApplyReplicatedTagBits (UnitMassTag.h) den Timer bei Ankunft des Tags noch einmal
		// selbst. Wie lang die anfaengliche Zielphase ist, entscheidet daher allein
		// ContinuousAttackStartDelayMultiplier am AnimInstance; fuer den MassShooter steht der
		// in BP_AnimInst_Soldier_AH_MassShooter auf 0, damit schon der ERSTE Schuss die
		// Schussanimation zeigt (beim WeaponModule bleibt er auf 0,8).
		if (FMassAIStateFragment* StateFrag = EntityManager->GetFragmentDataPtr<FMassAIStateFragment>(Entity))
		{
			StateFrag->StateTimerClient = 0.f;
		}
		EntityManager->Defer().AddTag<FMassStateContinuousAttackTag>(Entity);
	}
	else
	{
		EntityManager->Defer().RemoveTag<FMassStateContinuousAttackTag>(Entity);
	}
}

bool UMassShooterShootAbility::ResolveAimPoint(FVector& OutAim) const
{
	const AUnitBase* Unit = GetShootingUnit();
	if (!Unit)
	{
		return false;
	}

	// Preferred source: the shooter's own camera trace. It is the only value that is correct at
	// the instant this round leaves, and it is full 3D.
	if (const AMassShooterCharacter* Shooter = Cast<AMassShooterCharacter>(Unit))
	{
		if (const UMassShooterCombatComponent* Combat = Shooter->GetCombat())
		{
			const FVector AimPoint = Combat->GetAimPoint();
			if (!AimPoint.IsNearlyZero())
			{
				OutAim = AimPoint;
				return true;
			}
		}
	}

	// Fallback for a non-shooter pawn granted this ability: the RTS aim value. Same source
	// UGameplayAbilityBase::GetTargetLocation uses.
	const FVector Replicated = GetTargetLocation();
	if (!Replicated.IsNearlyZero())
	{
		OutAim = Replicated;
		return true;
	}

	return false;
}

void UMassShooterShootAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Feuertakt auf die Einheit schreiben, BEVOR der Tag gesetzt wird: an
	// AAbilityUnit::ContinuousAttackDuration haengt der Zyklus in ComputeState und die PlayRate im
	// UnitAnimationProcessor. Beim WeaponModule fuellt UWeaponComponent diesen Wert
	// (WeaponComponent.cpp: FireRate * FireRateMultiplier); ist dort nichts gelaufen, stuende hier
	// der Vorgabewert 1.0 und die Animation liefe im falschen Takt.
	{
		TSubclassOf<AProjectile> RateProjectileClass;
		FWeaponData RateWeaponData;
		float RateExtraDamage = 0.f;
		int32 RateMaxPierced = 1;
		TSubclassOf<UGameplayEffect> RateEffect1, RateEffect2, RateEffect3;

		if (AAbilityUnit* AbilityUnit = Cast<AAbilityUnit>(GetShootingUnit()))
		{
			if (GetShootInfo(RateProjectileClass, RateWeaponData, RateExtraDamage, RateMaxPierced,
				RateEffect1, RateEffect2, RateEffect3))
			{
				AbilityUnit->ContinuousAttackDuration = FMath::Max(0.02f,
					RateWeaponData.FireRate * FMath::Max(0.01f, RateWeaponData.FireRateMultiplier));
			}
		}
	}

	SetContinuousAttackTag(true);

	// The first round leaves NOW, not one interval from now.
	//
	// WeaponModule's Blueprint arms its looping timer and waits for the first callback, so a pull
	// shorter than the weapon's interval was cancelled before anything was fired — with the table
	// default of FireRate 1.0 that meant a full second of nothing, and a tap produced no shot at
	// all.
	FireOneRound();

	TSubclassOf<AProjectile> ProjectileClass;
	FWeaponData WeaponData;
	float ExtraDamage = 0.f;
	int32 MaxPierced = 1;
	TSubclassOf<UGameplayEffect> Effect1, Effect2, Effect3;

	float Interval = 0.15f;
	if (GetShootInfo(ProjectileClass, WeaponData, ExtraDamage, MaxPierced, Effect1, Effect2, Effect3))
	{
		Interval = WeaponData.FireRate * FMath::Max(0.01f, WeaponData.FireRateMultiplier);
	}
	Interval = FMath::Max(0.02f, Interval);

	World->GetTimerManager().SetTimer(FireTimer, this, &UMassShooterShootAbility::FireOneRound,
		Interval, /*bLoop*/ true);
}

void UMassShooterShootAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireTimer);
	}

	// Zwingend: der Tag hat in ComputeState die hoechste Prioritaet nach Dead. Bleibt er liegen,
	// sieht die Einheit dauerhaft schiessend aus - auch im Laufen und im Stillstand.
	// UMassShooterCombatComponent::Server_StopFire entfernt ihn zusaetzlich; das ist Absicht, denn
	// ein Abbruch, der einen Frame zu spaet landet, darf den Tag nicht stehen lassen.
	SetContinuousAttackTag(false);

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UMassShooterShootAbility::FireOneRound()
{
	AUnitBase* Unit = GetShootingUnit();
	if (!Unit)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	TSubclassOf<AProjectile> ProjectileClass;
	FWeaponData WeaponData;
	float ExtraDamage = 0.f;
	int32 MaxPierced = 1;
	TSubclassOf<UGameplayEffect> Effect1, Effect2, Effect3;

	if (!GetShootInfo(ProjectileClass, WeaponData, ExtraDamage, MaxPierced, Effect1, Effect2, Effect3)
		|| !ProjectileClass)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	// Die Schussanimation laeuft NICHT mehr je Schuss von hier aus.
	//
	// Bis Change 545 stand hier ein SetAnimStateOverride(UnitData::Attack, Haltezeit) je Schuss.
	// Das konnte nicht mehr als ein Zucken je Schuss erzeugen, weil der Zustandsautomat die
	// Animation bei jedem Eintritt in Attack neu startet und dazwischen eine Luecke braucht -
	// sichtbar als "Mischung aus Schuss- und Idle-Animation".
	//
	// Stattdessen traegt jetzt FMassStateContinuousAttackTag den ganzen Feuerstoss, gesetzt in
	// ActivateAbility und entfernt in EndAbility. Damit laeuft genau dieselbe Mechanik wie beim
	// WeaponModule-Charakter: ComputeState wechselt zyklisch zwischen ContinousAttack und Aim und
	// der UnitAnimationProcessor stellt PlayRate und Startposition auf den Feuertakt ein.
	//
	// AttackAnimHoldSeconds und AttackAnimDutyCycle bleiben als Eigenschaften bestehen, wirken auf
	// diesem Weg aber nicht mehr - den Takt bestimmen ContinuousAttackDuration und die drei
	// Continuous*-Werte am AnimInstance (SpeedMultiplier, StartDelayMultiplier, CycleRatio).

	// Out of ammo ends the burst rather than firing blanks. The combat component's auto-reload
	// picks it up from there.
	const UWeaponAttributeSet* WeaponAttributes = GetWeaponAttributeSet();
	if (WeaponAttributes && WeaponAttributes->GetAmmo() <= 0.f)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
		return;
	}

	FVector Aim;
	if (!ResolveAimPoint(Aim))
	{
		return;
	}

	FEffectAreaInfo AreaInfo;
	GetEffectAreaInfo(0, AreaInfo);

	const int32 ProjectileCount = FMath::Max(1, GetProjectileCount());

	// The aim keeps its Z. That single difference from WeaponModule's Blueprint is the whole point
	// of this class: SpawnProjectileFromClassWithAim builds the flight direction as
	// (aim - muzzle) and preserves whatever height the caller passes in.
	const AMassShooterCharacter* Shooter = Cast<AMassShooterCharacter>(Unit);
	const bool bMuzzleFromBounds = Shooter && Shooter->bDeriveMuzzleFromWeaponBounds;

	Unit->SpawnProjectileFromClassWithAim(
		Aim,
		ProjectileClass,
		MaxPierced,
		ProjectileCount,
		ProjectileCount > 1 ? MultiProjectileSpread : 0.f,
		/*IsBouncingNext*/ false,
		/*IsBouncingBack*/ false,
		AimZOffset,
		/*Scale*/ 1.f,
		// Null, sobald der MuzzleLocalspunkt aus den Waffenmassen kommt: AUnitBase addiert diesen
		// Versatz NOCH EINMAL und dreht ihn mit der AKTORrotation, nicht mit dem Socket. Beides
		// zusammen haette die Lauflaenge verdoppelt und waere der Animation trotzdem nicht gefolgt.
		bMuzzleFromBounds ? FVector::ZeroVector : WeaponData.MuzzleSpawnOffset,
		ExtraDamage,
		Effect1, Effect2, Effect3,
		AreaInfo);

	ModifyAmmo(-1.f);
	ApplyGlobalCooldown();
}
