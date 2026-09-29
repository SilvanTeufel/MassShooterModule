// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Core/MassShooterKillCredit.h"
#include "Characters/MassShooterCharacter.h"
#include "Components/MassShooterHealthComponent.h"
#include "MassShooterLog.h"

#include "Engine/World.h"
#include "EngineUtils.h"

void UMassShooterKillCredit::ReportProjectileHit(AActor* HitActor, int32 ShooterTeamId)
{
	if (!HitActor)
	{
		return;
	}

	// From the victim, never from `self`: this runs on the projectile CDO, which has no world.
	const UWorld* World = HitActor->GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	// Only units this module tracks can carry a kill credit at all; everything else is a wall, a
	// pickup or an untracked RTS unit and is not worth a world scan.
	UMassShooterHealthComponent* Health = HitActor->FindComponentByClass<UMassShooterHealthComponent>();
	if (!Health)
	{
		return;
	}

	// Resolve the shooter from its team. Exactly one match counts: two pawns on the reported team
	// means a co-op match where this hook cannot tell them apart, and an unattributed kill is
	// better than crediting the wrong player.
	AMassShooterCharacter* Shooter = nullptr;
	int32 Matches = 0;
	for (TActorIterator<AMassShooterCharacter> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (It->TeamId == ShooterTeamId)
		{
			Shooter = *It;
			++Matches;
		}
	}

	if (Matches != 1 || Shooter == HitActor)
	{
		return;
	}

	Health->NotifyDamageFrom(Shooter);
}
