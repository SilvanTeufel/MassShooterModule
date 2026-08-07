// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Abilities/MassShooterGrenadeAbility.h"
#include "Components/MassShooterLoadoutComponent.h"
#include "MassShooterLog.h"

#include "Characters/Unit/UnitBase.h"
#include "Actors/EffectArea.h"

#include "Engine/World.h"
#include "TimerManager.h"

UMassShooterGrenadeAbility::UMassShooterGrenadeAbility()
{
	AbilityName = TEXT("Grenade: \n\n");
	KeyboardKey = TEXT("G");
	AbilityInputID = EGASAbilityInputID::AbilityFour;

	// A grenade is thrown on the move; stopping the thrower would be wrong for a shooter.
	bStopMovementOnActivation = false;
}

void UMassShooterGrenadeAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AUnitBase* Thrower = ActorInfo ? Cast<AUnitBase>(ActorInfo->AvatarActor.Get()) : nullptr;
	UWorld* World = Thrower ? Thrower->GetWorld() : nullptr;

	if (!Thrower || !World || !EffectAreaClass)
	{
		if (!EffectAreaClass)
		{
			UE_LOG(LogMassShooter, Warning,
				TEXT("%s has no EffectAreaClass set — nothing to throw."), *GetName());
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	// Charges are spent on the server only; the ability instance only exists there anyway.
	if (bConsumeGrenadeCharge && Thrower->HasAuthority())
	{
		UMassShooterLoadoutComponent* Loadout = Thrower->FindComponentByClass<UMassShooterLoadoutComponent>();
		if (Loadout && !Loadout->ConsumeGrenade())
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, /*bWasCancelled*/ true);
			return;
		}
	}

	// GetTargetLocation() is the aim point the activation wrote from the hit result. A zero value
	// means the ability was triggered without one (an AI or a debug call), so fall back to a point
	// straight ahead rather than exploding on the thrower.
	FVector Target = GetTargetLocation();
	const FVector Origin = Thrower->GetProjectileSpawnLocation();

	if (Target.IsNearlyZero())
	{
		Target = Origin + Thrower->GetActorForwardVector() * MaxThrowDistance;
	}

	FVector ToTarget = Target - Origin;
	if (ToTarget.SizeSquared() > FMath::Square(MaxThrowDistance))
	{
		Target = Origin + ToTarget.GetSafeNormal() * MaxThrowDistance;
	}

	const int32 TeamId = Thrower->TeamId;
	const TSubclassOf<AEffectArea> AreaClass = EffectAreaClass;
	const FVector Scale = ExplosionScale;
	TWeakObjectPtr<AUnitBase> WeakThrower = Thrower;

	// Detonate after the fuse. Weak capture so a thrower who dies mid-flight does not keep the
	// timer alive on a destroyed actor — the grenade simply does not go off, which is the correct
	// behaviour when the owner (and its team attribution) is gone.
	FTimerHandle FuseHandle;
	World->GetTimerManager().SetTimer(FuseHandle, FTimerDelegate::CreateLambda(
		[WeakThrower, TeamId, Target, Scale, AreaClass]()
		{
			if (AUnitBase* Owner = WeakThrower.Get())
			{
				// SpawnEffectArea is RTSUnitTemplate's own entry point; the area it creates runs
				// through the standard Mass impact processor, so it damages units exactly like any
				// other RTS explosion.
				Owner->SpawnEffectArea(TeamId, Target, Scale, AreaClass, nullptr);
			}
		}), FMath::Max(0.05f, FuseSeconds), false);

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
