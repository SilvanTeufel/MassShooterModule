// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Abilities/MassShooterDashAbility.h"
#include "Characters/MassShooterCharacter.h"

UMassShooterDashAbility::UMassShooterDashAbility()
{
	AbilityName = TEXT("Dash: \n\n");
	KeyboardKey = TEXT("Q");
	AbilityInputID = EGASAbilityInputID::AbilityFive;

	// The whole point is to move; freezing the caster would cancel the dash.
	bStopMovementOnActivation = false;
}

void UMassShooterDashAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AMassShooterCharacter* Dasher = ActorInfo ? Cast<AMassShooterCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!Dasher || Dasher->IsDeadShooter())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, /*bWasCancelled*/ true);
		return;
	}

	// Dash where the player is already going; standing still dashes forward. Flattened so a dash
	// while looking down does not drive the pawn into the floor.
	FVector Direction = Dasher->GetVelocity();
	Direction.Z = 0.f;

	if (Direction.IsNearlyZero())
	{
		Direction = Dasher->GetActorForwardVector();
		Direction.Z = 0.f;
	}
	Direction = Direction.GetSafeNormal();

	const FVector Launch = Direction * DashSpeed + FVector(0.f, 0.f, DashLift);
	Dasher->Multicast_Launch(Launch, /*bOverrideXY*/ true, /*bOverrideZ*/ true);

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
