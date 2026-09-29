// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityTypes.h"
#include "MassEntityQuery.h"
#include "MassShooterLocomotionSyncProcessor.generated.h"

/**
 * Traegt die tatsaechliche Geschwindigkeit des Shooter-Aktors in sein FMassVelocityFragment.
 *
 * Warum das noetig ist: UUnitBaseAnimInstance liest das Tempo NICHT vom Aktor, sondern aus
 * FMassVelocityFragment. Dieses Fragment beschreiben ausschliesslich die Zustands-Prozessoren
 * des RTSUnitTemplate (Run, Chase, Patrol, Attack, Death, Pause) - alle an Mass-Zustandstags
 * gebunden. Der MassShooter setzt aber nur UnitData::Idle und verlaesst diesen Zustand nie; er
 * bewegt sich ueber sein eigenes UMassShooterMovementComponent, also aktorseitig.
 *
 * Folge ohne diesen Prozessor: MassSpeed bleibt 0, die Mischpunkte bleiben auf (0, 0) und die
 * Figur steht beim Laufen still. Der IdleStateProcessor nullt die Geschwindigkeit nicht, es gibt
 * also keinen Schreiber, der dagegenhaelt.
 *
 * Bewusst hier im MassShooterModule und nicht im RTSUnitTemplate: dort wuerde eine Aenderung
 * alle Einheiten aller drei Projekte betreffen, obwohl nur der Shooter seine Bewegung selbst
 * fuehrt.
 *
 * Laeuft nur auf dem Server; der Client bekommt die Animation ueber die uebliche Replikation.
 */
UCLASS()
class MASSSHOOTERMODULE_API UMassShooterLocomotionSyncProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassShooterLocomotionSyncProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery UnitQuery;
};
