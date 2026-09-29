// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Mass/MassShooterLocomotionSyncProcessor.h"

#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassMovementFragments.h"
#include "MassActorSubsystem.h"
#include "Characters/MassShooterCharacter.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

/** Diagnose: meldet, wie viele Shooter-Entitaeten je Durchlauf ein Tempo bekommen haben. */
static TAutoConsoleVariable<int32> CVarRTS_ShooterLocoDiag(
	TEXT("rts.shooter.loco.diag"),
	0,
	TEXT("1 = meldet Anzahl und Tempo der synchronisierten Shooter-Entitaeten."),
	ECVF_Default);

UMassShooterLocomotionSyncProcessor::UMassShooterLocomotionSyncProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
	// Der Aktor wird gelesen, also GameThread.
	bRequiresGameThreadExecution = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::Server | (int32)EProcessorExecutionFlags::Standalone;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Tasks;
}

void UMassShooterLocomotionSyncProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	UnitQuery.Initialize(EntityManager);
	// ReadWrite, weil nur GetMutable() einen nicht-konstanten Aktor liefert.
	UnitQuery.AddRequirement<FMassActorFragment>(EMassFragmentAccess::ReadWrite);
	UnitQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
	UnitQuery.RegisterWithProcessor(*this);
}

void UMassShooterLocomotionSyncProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	int32 SyncedCount = 0;
	float FastestSpeed = 0.f;

	UnitQuery.ForEachEntityChunk(Context, [&SyncedCount, &FastestSpeed](FMassExecutionContext& ChunkContext)
	{
		const int32 EntityCount = ChunkContext.GetNumEntities();
		const TArrayView<FMassActorFragment> Actors = ChunkContext.GetMutableFragmentView<FMassActorFragment>();
		const TArrayView<FMassVelocityFragment> Velocities = ChunkContext.GetMutableFragmentView<FMassVelocityFragment>();

		for (int32 i = 0; i < EntityCount; ++i)
		{
			// Nur Shooter-Charaktere: alle anderen Einheiten bekommen ihre Geschwindigkeit
			// weiterhin von den Zustands-Prozessoren, und die duerfen wir nicht ueberschreiben.
			const AMassShooterCharacter* Shooter = Cast<AMassShooterCharacter>(Actors[i].GetMutable());
			if (!Shooter)
			{
				continue;
			}

			Velocities[i].Value = Shooter->GetVelocity();
			++SyncedCount;
			FastestSpeed = FMath::Max(FastestSpeed, (float)Velocities[i].Value.Size2D());
		}
	});

	if (SyncedCount > 0 && CVarRTS_ShooterLocoDiag.GetValueOnGameThread() != 0)
	{
		static int32 DiagCounter = 0;
		if (++DiagCounter % 60 == 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ShooterLoco] Nr.%d %d Entitaeten synchronisiert, schnellste %.0f uu/s"),
				DiagCounter, SyncedCount, FastestSpeed);
		}
	}
}
