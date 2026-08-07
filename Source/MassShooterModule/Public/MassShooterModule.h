// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class AActor;

/**
 * MassShooterModule.
 *
 * A multiplayer mass-shooter layer built ON TOP of RTSUnitTemplate + WeaponModule. Neither base
 * plugin is modified: everything here is a subclass, a component, or a subscriber to a hook the
 * base plugin already exposes.
 *
 * The one hook this module installs at startup is
 * UMassActorBindingComponent::OnMassArchetypeBuilding — RTSUnitTemplate broadcasts it while
 * building a unit's Mass archetype, before CreateArchetype, precisely so external plugins can
 * extend (or trim) the archetype without touching the plugin.
 */
class FMassShooterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/**
	 * Archetype hook for shooter player pawns.
	 *
	 * A shooter player is an AUnitBase (so RTS bots can perceive, target and damage it through
	 * the normal Mass pipeline), but its POSITION comes from the CharacterMovementComponent —
	 * Unreal's own rollback-networked mover — not from the RTS locomotion processors. Two movers
	 * fighting over one transform is jitter, so the player opts out of RTS locomotion by dropping
	 * FUnitMassTag, which every RTS locomotion/avoidance processor requires as All:
	 * UnitMovementProcessor, UnitApplyMassMovementProcessor, ActorTransformSyncProcessor,
	 * UnitSoftAvoidanceProcessor, UnitSeparationProcessor, MassUnitHoverProcessor.
	 *
	 * Perception is deliberately NOT gated on FUnitMassTag in RTSUnitTemplate (UDetectionProcessor
	 * and UUnitSightProcessor only need FTransformFragment + FMassCombatStatsFragment +
	 * FMassSightFragment + FMassAllianceFragment), so bots still see the player. What the player
	 * loses with FUnitMassTag is the fragment->actor transform push, which is exactly what we
	 * want; UMassShooterPawnSyncProcessor supplies the opposite direction (actor->fragment).
	 */
	static void OnMassArchetypeBuilding(AActor* Owner, TArray<const UScriptStruct*>& FragmentsAndTags);

	FDelegateHandle ArchetypeBuildingHandle;
};
