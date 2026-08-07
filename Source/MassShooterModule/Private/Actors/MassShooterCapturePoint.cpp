// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Actors/MassShooterCapturePoint.h"
#include "Characters/MassShooterCharacter.h"
#include "Characters/MassShooterBot.h"
#include "GameStates/MassShooterGameState.h"
#include "PlayerState/MassShooterPlayerState.h"
#include "MassShooterLog.h"

#include "Characters/Unit/UnitBase.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AMassShooterCapturePoint::AMassShooterCapturePoint()
{
	PrimaryActorTick.bCanEverTick = true;

	// A quarter-second cadence: capture is a multi-second action, so per-frame precision would be
	// spent overlap-sweeping for nothing.
	PrimaryActorTick.TickInterval = 0.25f;

	bReplicates = true;

	CaptureVolume = CreateDefaultSubobject<USphereComponent>(TEXT("CaptureVolume"));
	SetRootComponent(CaptureVolume);
	CaptureVolume->SetSphereRadius(CaptureRadius);
	CaptureVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CaptureVolume->SetCollisionObjectType(ECC_WorldDynamic);
	CaptureVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	CaptureVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(CaptureVolume);
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Index 0 is neutral; 1..N are teams.
	TeamColors = {
		FLinearColor(0.6f, 0.6f, 0.6f, 1.f),
		FLinearColor(0.15f, 0.5f, 1.f, 1.f),
		FLinearColor(1.f, 0.25f, 0.2f, 1.f),
		FLinearColor(0.3f, 0.9f, 0.35f, 1.f),
		FLinearColor(0.95f, 0.8f, 0.15f, 1.f)
	};
}

void AMassShooterCapturePoint::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMassShooterCapturePoint, OwnerTeamId);
	DOREPLIFETIME(AMassShooterCapturePoint, CapturingTeamId);
	DOREPLIFETIME(AMassShooterCapturePoint, CaptureProgress);
	DOREPLIFETIME(AMassShooterCapturePoint, CaptureState);
}

void AMassShooterCapturePoint::BeginPlay()
{
	Super::BeginPlay();

	CaptureVolume->SetSphereRadius(CaptureRadius);

	if (HasAuthority())
	{
		OwnerTeamId = InitialOwnerTeamId;
		CaptureState = OwnerTeamId > 0 ? EMassShooterCaptureState::Owned : EMassShooterCaptureState::Neutral;
		CaptureProgress = OwnerTeamId > 0 ? 1.f : 0.f;
		CapturingTeamId = OwnerTeamId;
	}

	if (Marker && Marker->GetMaterial(0))
	{
		MarkerMID = Marker->CreateAndSetMaterialInstanceDynamic(0);
	}
	RefreshVisuals();
}

void AMassShooterCapturePoint::OnRep_OwnerTeamId()
{
	RefreshVisuals();
	OnCaptured.Broadcast(this, OwnerTeamId);
}

void AMassShooterCapturePoint::RefreshVisuals()
{
	if (!MarkerMID || TeamColors.Num() == 0)
	{
		return;
	}

	const int32 ColorIndex = FMath::Clamp(OwnerTeamId, 0, TeamColors.Num() - 1);
	MarkerMID->SetVectorParameterValue(FName(TEXT("Color")), TeamColors[ColorIndex]);
	MarkerMID->SetVectorParameterValue(FName(TEXT("BaseColor")), TeamColors[ColorIndex]);
}

void AMassShooterCapturePoint::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

	// Ask who is actually inside right now. Overlap bookkeeping would drift: a player can die,
	// respawn or teleport inside the volume, and each of those paths can drop an End event.
	TArray<AActor*> Overlapping;
	CaptureVolume->GetOverlappingActors(Overlapping, AUnitBase::StaticClass());

	TMap<int32, int32> PerTeamCount;
	for (AActor* Actor : Overlapping)
	{
		const AUnitBase* Unit = Cast<AUnitBase>(Actor);
		if (!Unit || Unit->GetUnitState() == UnitData::Dead)
		{
			continue;
		}

		if (const AMassShooterCharacter* Player = Cast<AMassShooterCharacter>(Unit))
		{
			if (Player->IsDeadShooter())
			{
				continue;
			}
		}
		else if (!bBotsCanCapture)
		{
			continue;
		}

		if (Unit->TeamId > 0)
		{
			PerTeamCount.FindOrAdd(Unit->TeamId) += 1;
		}
	}

	// Pick the team with the most bodies present, and note whether anyone contests it.
	int32 DominantTeam = 0;
	int32 DominantCount = 0;
	bool bContested = false;
	for (const TPair<int32, int32>& Pair : PerTeamCount)
	{
		if (Pair.Value > DominantCount)
		{
			DominantCount = Pair.Value;
			DominantTeam = Pair.Key;
		}
	}
	bContested = PerTeamCount.Num() > 1;

	if (bContested)
	{
		// Two sides in the circle: nothing moves. Freezing beats letting the larger group win
		// instantly — it is what makes a point worth fighting over instead of worth zerging.
		CaptureState = EMassShooterCaptureState::Contested;
	}
	else if (DominantTeam == 0)
	{
		// Empty: hold whatever state the point is in.
		CaptureState = OwnerTeamId > 0 ? EMassShooterCaptureState::Owned : EMassShooterCaptureState::Neutral;
	}
	else if (DominantTeam == OwnerTeamId)
	{
		CaptureProgress = 1.f;
		CapturingTeamId = OwnerTeamId;
		CaptureState = EMassShooterCaptureState::Owned;
	}
	else
	{
		const float SpeedMultiplier = FMath::Min(MaxCaptureSpeedMultiplier,
			1.f + PerPlayerSpeedBonus * static_cast<float>(DominantCount - 1));
		const float Rate = SpeedMultiplier / FMath::Max(0.5f, CaptureSeconds);

		if (CapturingTeamId != DominantTeam)
		{
			// A different team started taking it: first drain the previous owner's hold to zero,
			// then build up. Modelled by reusing the same progress value in both directions.
			CaptureProgress -= Rate * DeltaSeconds;
			if (CaptureProgress <= 0.f)
			{
				CaptureProgress = 0.f;
				CapturingTeamId = DominantTeam;

				if (OwnerTeamId != 0)
				{
					OwnerTeamId = 0; // neutralised before it can be taken
					OnRep_OwnerTeamId();
				}
			}
		}
		else
		{
			CaptureProgress += Rate * DeltaSeconds;
			if (CaptureProgress >= 1.f)
			{
				CaptureProgress = 1.f;
				if (OwnerTeamId != DominantTeam)
				{
					OwnerTeamId = DominantTeam;
					OnRep_OwnerTeamId();

					UE_LOG(LogMassShooterMatch, Log, TEXT("Capture point %s taken by team %d."),
						*PointName, OwnerTeamId);
				}
			}
		}

		CaptureState = EMassShooterCaptureState::Capturing;
	}

	// Held points tick score. Accumulated because ScorePerSecond is deliberately fractional —
	// a point should be worth holding for a while, not a point per second.
	if (OwnerTeamId > 0 && ScorePerSecond > 0.f)
	{
		ScoreAccumulator += ScorePerSecond * DeltaSeconds;
		if (ScoreAccumulator >= 1.f)
		{
			const int32 Whole = FMath::FloorToInt(ScoreAccumulator);
			ScoreAccumulator -= Whole;

			if (AMassShooterGameState* GS = GetWorld()->GetGameState<AMassShooterGameState>())
			{
				GS->AddTeamScore(OwnerTeamId, Whole, /*bCountAsKill*/ false);
			}
		}
	}
}
