// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "MassShooterModule.h"
#include "MassShooterLog.h"
#include "Mass/MassShooterFragments.h"
#include "Characters/MassShooterCharacter.h"

// RTSUnitTemplate — read-only use of the hook it already exposes.
#include "Mass/MassActorBindingComponent.h"
#include "Mass/UnitMassTag.h"

#define LOCTEXT_NAMESPACE "FMassShooterModule"

DEFINE_LOG_CATEGORY(LogMassShooter);
DEFINE_LOG_CATEGORY(LogMassShooterMatch);

void FMassShooterModule::OnMassArchetypeBuilding(AActor* Owner, TArray<const UScriptStruct*>& FragmentsAndTags)
{
	// Bots and every other RTS unit keep the stock archetype. Only the player pawn is special.
	if (!Cast<AMassShooterCharacter>(Owner))
	{
		return;
	}

	FragmentsAndTags.Add(FMassShooterPawnTag::StaticStruct());
	FragmentsAndTags.Add(FMassShooterPawnFragment::StaticStruct());

	// See the header for why this specific tag: it is the single durable lever that opts an entity
	// out of RTS locomotion. FMassStateStopMovementTag would NOT work — that one is a transient
	// startup-freeze marker the base plugin adds at spawn and strips once the unit is "ready",
	// taking ours with it.
	//
	// The list already contains FUnitMassTag at this point (the delegate is broadcast after the
	// hardcoded fragment list is built and before CreateArchetype), so removing it here means the
	// entity is never in an RTS-mover archetype in the first place — no later archetype move, no
	// one-frame window where the RTS mover owns the player.
	FragmentsAndTags.Remove(FUnitMassTag::StaticStruct());

	UE_LOG(LogMassShooter, Log,
		TEXT("Mass archetype for shooter pawn %s: added shooter pawn tag/fragment, removed FUnitMassTag."),
		*Owner->GetName());
}

void FMassShooterModule::StartupModule()
{
	ArchetypeBuildingHandle =
		UMassActorBindingComponent::OnMassArchetypeBuilding.AddStatic(&FMassShooterModule::OnMassArchetypeBuilding);

	UE_LOG(LogMassShooter, Log, TEXT("MassShooterModule started."));
}

void FMassShooterModule::ShutdownModule()
{
	if (ArchetypeBuildingHandle.IsValid())
	{
		UMassActorBindingComponent::OnMassArchetypeBuilding.Remove(ArchetypeBuildingHandle);
		ArchetypeBuildingHandle.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMassShooterModule, MassShooterModule)
