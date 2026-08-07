// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Mass/MassShooterPawnSyncProcessor.h"
#include "Mass/MassShooterFragments.h"
#include "MassCommonFragments.h"
#include "MassActorSubsystem.h"
#include "MassExecutionContext.h"
#include "Mass/UnitMassTag.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

UMassShooterPawnSyncProcessor::UMassShooterPawnSyncProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;

	// Touches AActors, so it must stay on the game thread.
	bRequiresGameThreadExecution = true;

	// PrePhysics, matching UUnitActorToFragmentSyncProcessor: the perception processors run later
	// in the frame and must see this frame's position, not last frame's.
	ProcessingPhase = EMassProcessingPhase::PrePhysics;
}

void UMassShooterPawnSyncProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.Initialize(EntityManager);
	EntityQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadOnly);
	// Not a None requirement: the tag has to be OBSERVED here so it can be removed. See Execute.
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassShooterPawnFragment>(EMassFragmentAccess::ReadWrite);

	// Optional, not All: the characteristics fragment is added by RTSUnitTemplate's binding
	// component, and an entity that somehow lacks it should still get its transform pushed rather
	// than silently dropping out of the query.
	EntityQuery.AddRequirement<FMassAgentCharacteristicsFragment>(EMassFragmentAccess::ReadWrite, EMassFragmentPresence::Optional);
	EntityQuery.AddTagRequirement<FMassShooterPawnTag>(EMassFragmentPresence::All);
	EntityQuery.RegisterWithProcessor(*this);

	// Matches only while the tag is actually on a shooter pawn, so the common case costs a query
	// with zero chunks rather than a deferred command every frame.
	RotateToMouseQuery.Initialize(EntityManager);
	RotateToMouseQuery.AddTagRequirement<FMassShooterPawnTag>(EMassFragmentPresence::All);
	RotateToMouseQuery.AddTagRequirement<FMassRotateToMouseTag>(EMassFragmentPresence::All);
	RotateToMouseQuery.RegisterWithProcessor(*this);
}

void UMassShooterPawnSyncProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	RotateToMouseQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& ChunkContext)
	{
		for (int32 Index = 0; Index < ChunkContext.GetNumEntities(); ++Index)
		{
			ChunkContext.Defer().RemoveTag<FMassRotateToMouseTag>(ChunkContext.GetEntity(Index));
		}
	});

	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FMassActorFragment> ActorList = ChunkContext.GetFragmentView<FMassActorFragment>();
		const TArrayView<FTransformFragment> TransformList = ChunkContext.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FMassShooterPawnFragment> PawnList = ChunkContext.GetMutableFragmentView<FMassShooterPawnFragment>();
		const TArrayView<FMassAgentCharacteristicsFragment> CharList = ChunkContext.GetMutableFragmentView<FMassAgentCharacteristicsFragment>();

		for (int32 Index = 0; Index < ChunkContext.GetNumEntities(); ++Index)
		{
			const AActor* Actor = ActorList[Index].Get();
			if (!Actor)
			{
				continue;
			}

			const FVector Location = Actor->GetActorLocation();
			const float Yaw = Actor->GetActorRotation().Yaw;

			FMassShooterPawnFragment& Pawn = PawnList[Index];

			// Keep the RTS rotate-to-mouse tag off the player.
			//
			// UGameplayAbilityBase::ActivateAbility adds FMassRotateToMouseTag unconditionally to
			// whichever unit activates ANY ability. The moment the player shoots, that tag makes
			// UMassRotateToMouseProcessor::Execute run, and that processor does far more than
			// rotate: every frame it traces GetHitResultUnderCursor and writes the result into
			// AExtendedControllerBase::ReplicatedMouseLocation.
			//
			// ReplicatedMouseLocation is precisely what the shoot ability aims with
			// (UGameplayAbilityBase::GetTargetLocation), and this controller feeds it the camera
			// aim point instead. A cursor trace in a first-person view — where the cursor is hidden
			// and parked — resolves to a point at ground level, so it overwrote the aim with a
			// ground point roughly every frame and every sustained round left flat. That is the
			// reported "Projektile fliegen nur parallel zum Boden".
			//
			// (Stripped by RotateToMouseQuery below, which only matches while the tag is present.)

			// Keep the ground height current.
			//
			// This is not cosmetic. AUnitBase::GetProjectileSpawnLocation() builds the muzzle's Z
			// as LastGroundLocation + capsule half-height + muzzle offset — it never reads the
			// entity transform's Z — and RTSUnitTemplate's shoot path then aims the projectile
			// along (aim - muzzle). RTSUnitTemplate maintains LastGroundLocation in
			// UActorTransformSyncProcessor, whose query requires FUnitMassTag as All; this module
			// removes that tag from the player so the RTS locomotion processors let go of the
			// pawn. The value therefore froze at whatever the ground was when the entity was bound,
			// and every shot left from a muzzle stuck at spawn height, which tilts the trajectory
			// by exactly the height error and is why shots would not follow the crosshair up or
			// down. Nothing in RTSUnitTemplate changes: the fragment is written from here.
			if (CharList.Num() > Index)
			{
				FMassAgentCharacteristicsFragment& Characteristics = CharList[Index];

				if (const ACharacter* Character = Cast<ACharacter>(Actor))
				{
					if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
					{
						const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
						Characteristics.CapsuleHeight = HalfHeight;
						Characteristics.CapsuleRadius = Capsule->GetScaledCapsuleRadius();

						// Capsule bottom, i.e. the floor the pawn is standing on. While airborne
						// this trails the pawn upward, which is what we want: the muzzle stays a
						// fixed distance under the eyes instead of snapping back to the ground.
						Characteristics.LastGroundLocation = Location.Z - HalfHeight;
					}
				}
			}

			// Skip the write while the pawn is standing still. A shooter match is mostly people
			// holding angles; this keeps idle pawns out of the fragment write path entirely.
			if (Location.Equals(Pawn.LastPushedLocation, 1.f) && FMath::IsNearlyEqual(Yaw, Pawn.LastPushedYaw, 1.f))
			{
				continue;
			}

			Pawn.LastPushedLocation = Location;
			Pawn.LastPushedYaw = Yaw;

			FTransform& Transform = TransformList[Index].GetMutableTransform();
			Transform.SetLocation(Location);
			Transform.SetRotation(FRotator(0.f, Yaw, 0.f).Quaternion());
		}
	});
}
