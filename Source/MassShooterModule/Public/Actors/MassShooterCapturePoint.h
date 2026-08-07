// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/MassShooterTypes.h"
#include "MassShooterCapturePoint.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/** Fires on every machine when ownership actually changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMassShooterOnCaptured, class AMassShooterCapturePoint*, Point, int32, NewOwnerTeamId);

/**
 * A Domination objective: stand in it to take it, hold it to score.
 *
 * Occupancy is counted by asking who is inside the sphere on the server each tick rather than by
 * caching overlap events. That is deliberate — a player can die, respawn or be teleported inside
 * the volume, and overlap bookkeeping gets those cases wrong in ways that are very hard to see
 * (a point that stays "held" by a corpse). The sweep is a handful of actors on one trigger.
 *
 * Progress is a single float from -1 (fully team B) through 0 (neutral) to +1: contested by two
 * teams at once, it simply stops rather than flip-flopping.
 */
UCLASS(Blueprintable)
class MASSSHOOTERMODULE_API AMassShooterCapturePoint : public AActor
{
	GENERATED_BODY()

public:
	AMassShooterCapturePoint();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Label drawn on the HUD objective bar ("A", "B", "Reactor"...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Capture")
	FString PointName = TEXT("A");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Capture")
	float CaptureRadius = 500.f;

	/** Seconds one player needs to take a neutral point. More players are proportionally faster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Capture")
	float CaptureSeconds = 8.f;

	/** Capture speed multiplier per extra player, capped by MaxCaptureSpeedMultiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Capture")
	float PerPlayerSpeedBonus = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Capture")
	float MaxCaptureSpeedMultiplier = 3.f;

	/** Score the owning team ticks per second while it holds this point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Capture")
	float ScorePerSecond = 0.4f;

	/** Team that owns this at match start. 0 = neutral. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Capture")
	int32 InitialOwnerTeamId = 0;

	/** Whether bots standing in the point count toward capturing it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Capture")
	bool bBotsCanCapture = false;

	UPROPERTY(BlueprintAssignable, Category = "MassShooter|Capture")
	FMassShooterOnCaptured OnCaptured;

	// ---- Replicated state --------------------------------------------------------------------

	UPROPERTY(ReplicatedUsing = OnRep_OwnerTeamId, BlueprintReadOnly, Category = "MassShooter|Capture")
	int32 OwnerTeamId = 0;

	/** Team currently making progress on this point, or 0. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Capture")
	int32 CapturingTeamId = 0;

	/** 0..1 progress of CapturingTeamId toward taking the point. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Capture")
	float CaptureProgress = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "MassShooter|Capture")
	EMassShooterCaptureState CaptureState = EMassShooterCaptureState::Neutral;

	UFUNCTION(BlueprintPure, Category = "MassShooter|Capture")
	bool IsContested() const { return CaptureState == EMassShooterCaptureState::Contested; }

protected:
	UFUNCTION()
	void OnRep_OwnerTeamId();

	/** Colours the marker mesh for the owning team. Called on every machine. */
	void RefreshVisuals();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> CaptureVolume;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MassShooter|Capture", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Marker;

	/** Colour applied to the marker per team index; the last entry is reused past the end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MassShooter|Capture")
	TArray<FLinearColor> TeamColors;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MarkerMID;

	/** Accumulates fractional score so a sub-1 ScorePerSecond still pays out. */
	float ScoreAccumulator = 0.f;
};
