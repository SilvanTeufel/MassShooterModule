// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Mass/MassShooterProjectileAuditProcessor.h"
#include "MassShooterLog.h"

#include "Mass/UnitMassTag.h"
// Declares the TMassFragmentTraits opt-outs for RTSUnitTemplate's non-trivially-copyable
// fragments. Without it the AddRequirement below fails a static_assert.
#include "Mass/MassFragmentTraitsOverrides.h"
#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
	int32 GLogProjectiles = 0;
	FAutoConsoleVariableRef CVarLogProjectiles(
		TEXT("Shooter.Debug.LogProjectiles"), GLogProjectiles,
		TEXT("1 = log every live Mass projectile's flight direction, height and target."), ECVF_Cheat);
}

UMassShooterProjectileAuditProcessor::UMassShooterProjectileAuditProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;

	// PostPhysics: sample after this frame's movement processor has already moved them, so the
	// numbers describe where the projectile actually got to rather than where it was.
	ProcessingPhase = EMassProcessingPhase::PostPhysics;
}

void UMassShooterProjectileAuditProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.Initialize(EntityManager);
	EntityQuery.AddRequirement<FMassProjectileFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.RegisterWithProcessor(*this);
}

void UMassShooterProjectileAuditProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (GLogProjectiles == 0)
	{
		return;
	}

	const UWorld* World = EntityManager.GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	if (Now - LastLogTime < 0.5f)
	{
		return;
	}
	LastLogTime = Now;

	// One line per projectile per sample. Deliberately not aggregated: a burst where the first
	// round climbs and the rest do not is a different bug from one where none of them do.
	int32 Reported = 0;
	EntityQuery.ForEachEntityChunk(Context, [&Reported](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FMassProjectileFragment> Projectiles = ChunkContext.GetFragmentView<FMassProjectileFragment>();
		const TConstArrayView<FTransformFragment> Transforms = ChunkContext.GetFragmentView<FTransformFragment>();

		for (int32 Index = 0; Index < ChunkContext.GetNumEntities() && Reported < 8; ++Index)
		{
			const FMassProjectileFragment& Projectile = Projectiles[Index];
			const FVector Location = Transforms[Index].GetTransform().GetLocation();
			const FVector Start = Projectile.ArcStartLocation;

			// travelledPitch is the honest number: the angle of the line from where the shot
			// started to where it is now. A projectile whose FlightDirection has pitch but whose
			// travelled path is flat is being overridden somewhere in the movement processor.
			const FVector Travelled = Location - Start;
			const float TravelledPitch = Travelled.IsNearlyZero() ? 0.f : Travelled.Rotation().Pitch;

			UE_LOG(LogMassShooter, Log,
				TEXT("PROJ: dirPitch=%.1f travelledPitch=%.1f z=%.0f startZ=%.0f targetZ=%.0f life=%.2f follow=%d arc=%.0f homing=%d"),
				Projectile.FlightDirection.Rotation().Pitch, TravelledPitch,
				Location.Z, Start.Z, Projectile.TargetLocation.Z, Projectile.LifeTime,
				(int32)Projectile.bFollowTarget, Projectile.ArcHeight, (int32)Projectile.bIsHoming);
			++Reported;
		}
	});
}
