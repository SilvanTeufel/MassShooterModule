// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Hud/MassShooterHUD.h"
#include "Characters/MassShooterCharacter.h"
#include "Controller/MassShooterPlayerController.h"
#include "Components/MassShooterCombatComponent.h"
#include "Components/MassShooterLoadoutComponent.h"
#include "Components/MassShooterHealthComponent.h"
#include "GameStates/MassShooterGameState.h"
#include "PlayerState/MassShooterPlayerState.h"
#include "Actors/MassShooterCapturePoint.h"
#include "Settings/MassShooterSettings.h"

#include "GAS/GameplayAbilityBase.h"
#include "Engine/Texture2D.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/World.h"

namespace
{
	// One place for the palette so the crosshair, bars and scoreboard cannot drift apart.
	const FLinearColor ColorHealth(0.30f, 0.85f, 0.35f, 1.f);
	const FLinearColor ColorShield(0.30f, 0.70f, 1.00f, 1.f);
	const FLinearColor ColorAmmo(0.95f, 0.85f, 0.30f, 1.f);
	const FLinearColor ColorText(0.92f, 0.94f, 0.96f, 1.f);
	const FLinearColor ColorDim(0.62f, 0.66f, 0.70f, 1.f);
	const FLinearColor ColorPanel(0.03f, 0.04f, 0.06f, 0.62f);
	const FLinearColor ColorDanger(1.00f, 0.30f, 0.25f, 1.f);
}

AMassShooterHUD::AMassShooterHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

FLinearColor AMassShooterHUD::GetTeamColor(int32 TeamId)
{
	switch (TeamId)
	{
	case 1:  return FLinearColor(0.25f, 0.60f, 1.00f, 1.f);
	case 2:  return FLinearColor(1.00f, 0.35f, 0.30f, 1.f);
	case 3:  return FLinearColor(0.35f, 0.90f, 0.45f, 1.f);
	case 4:  return FLinearColor(0.95f, 0.80f, 0.20f, 1.f);
	default: return FLinearColor(0.70f, 0.70f, 0.74f, 1.f);
	}
}

void AMassShooterHUD::ShowHitMarker(bool bLethal)
{
	if (const UWorld* World = GetWorld())
	{
		HitMarkerEndTime = World->GetTimeSeconds() + HitMarkerDuration;
		bHitMarkerLethal = bLethal;
	}
}

void AMassShooterHUD::ShowDamageDirection(const FVector& FromWorldLocation)
{
	if (const UWorld* World = GetWorld())
	{
		FDamageDirection Entry;
		Entry.WorldLocation = FromWorldLocation;
		Entry.EndTime = World->GetTimeSeconds() + DamageIndicatorDuration;
		DamageDirections.Add(Entry);
	}
}

void AMassShooterHUD::DrawShadowedText(const FString& Text, float X, float Y, const FLinearColor& Color, float Scale)
{
	if (!Canvas)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;

	// The shadow is what makes white text survive a bright skybox; without it the HUD is unreadable
	// on half the maps anyone will build.
	DrawText(Text, FLinearColor(0.f, 0.f, 0.f, Color.A * 0.8f), X + 1.f, Y + 1.f, Font, Scale);
	DrawText(Text, Color, X, Y, Font, Scale);
}

void AMassShooterHUD::DrawBar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& FillColor)
{
	if (!Canvas)
	{
		return;
	}

	Fraction = FMath::Clamp(Fraction, 0.f, 1.f);

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), X - 1.f, Y - 1.f, Width + 2.f, Height + 2.f);
	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.08f), X, Y, Width, Height);
	DrawRect(FillColor, X, Y, Width * Fraction, Height);
}

void AMassShooterHUD::RefreshWeaponSelection()
{
	AMassShooterCharacter* Pawn = ShooterPC ? ShooterPC->GetShooterCharacter() : Cast<AMassShooterCharacter>(GetOwningPawn());

	// Rebuild only on an actual change: SelectedUnits is read by a WeaponModule timer, and
	// churning the array every frame would make it rebuild its panels every frame too.
	if (SelectedUnits.Num() == 1 && SelectedUnits[0] == Pawn)
	{
		return;
	}

	SelectedUnits.Reset();
	SelectedUnitsSet.Reset();
	if (Pawn)
	{
		SelectedUnits.Add(Pawn);
		SelectedUnitsSet.Add(Pawn);
	}
}

void AMassShooterHUD::DrawHUD()
{
	// AHUDBase's own drawing is the RTS presentation — selection rectangle, unit indicators,
	// health bars over every unit. Skipped unless explicitly asked for; AHUD::DrawHUD is still
	// called so hitbox processing and Blueprint HUD events keep working.
	if (bDrawRTSHUD)
	{
		Super::DrawHUD();
	}
	else
	{
		AHUD::DrawHUD();
	}

	ShooterPC = Cast<AMassShooterPlayerController>(PlayerOwner);

	// Done before the early-out below: WeaponModule's panels must keep tracking the pawn even for
	// a project that has switched the built-in Canvas HUD off.
	RefreshWeaponSelection();

	if (!Canvas || !UMassShooterSettings::Get()->bDrawDefaultHUD)
	{
		return;
	}

	AMassShooterCharacter* Pawn = ShooterPC ? ShooterPC->GetShooterCharacter() : Cast<AMassShooterCharacter>(GetOwningPawn());
	AMassShooterGameState* GS = GetWorld() ? GetWorld()->GetGameState<AMassShooterGameState>() : nullptr;

	DrawMatchBanner(GS);
	DrawObjectives(GS);
	DrawKillFeed(GS);
	DrawVitals(Pawn);
	DrawAmmo(Pawn);
	DrawAbilityBar(Pawn);
	DrawDamageIndicators();
	DrawCrosshair(Pawn);
	DrawRespawnPrompt(Pawn);

	// The scoreboard covers the world, so it is drawn last and only while asked for — or
	// automatically once the match is decided, which is when everyone wants to see it anyway.
	const bool bForceScoreboard = GS && GS->MatchPhase == EMassShooterMatchPhase::PostMatch;
	if (bForceScoreboard || (ShooterPC && ShooterPC->IsScoreboardHeld()))
	{
		DrawScoreboard(GS);
	}
}

void AMassShooterHUD::DrawCrosshair(AMassShooterCharacter* Pawn)
{
	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;

	if (!Pawn || Pawn->IsDeadShooter())
	{
		return;
	}

	const UMassShooterCombatComponent* Combat = Pawn->GetCombat();

	// Gap grows with the actual spread value the shots use, so the reticle never lies about where
	// a bullet can go.
	const float SpreadDegrees = Combat ? Combat->GetCurrentSpreadDegrees() : 1.f;
	const float Gap = 5.f + SpreadDegrees * 3.5f;
	const float Length = 9.f;
	const float Thickness = 2.f;

	const FLinearColor Color = FLinearColor(1.f, 1.f, 1.f, 0.85f);

	DrawRect(Color, CenterX - Gap - Length, CenterY - Thickness * 0.5f, Length, Thickness);
	DrawRect(Color, CenterX + Gap,          CenterY - Thickness * 0.5f, Length, Thickness);
	DrawRect(Color, CenterX - Thickness * 0.5f, CenterY - Gap - Length, Thickness, Length);
	DrawRect(Color, CenterX - Thickness * 0.5f, CenterY + Gap,          Thickness, Length);

	// Centre dot only while aiming, as the "you are precise now" signal.
	if (Combat && Combat->IsAiming())
	{
		DrawRect(Color, CenterX - 1.f, CenterY - 1.f, 2.f, 2.f);
	}

	if (GetWorld() && GetWorld()->GetTimeSeconds() < HitMarkerEndTime)
	{
		const FLinearColor MarkerColor = bHitMarkerLethal ? ColorDanger : FLinearColor(1.f, 1.f, 1.f, 0.95f);
		const float Inner = 4.f;
		const float Outer = 11.f;

		// Four diagonal ticks, drawn as small squares stepping outward — a rotated line is not
		// something Canvas does cheaply, and this reads identically at HUD scale.
		for (float Step = Inner; Step <= Outer; Step += 2.f)
		{
			DrawRect(MarkerColor, CenterX + Step, CenterY + Step, 2.f, 2.f);
			DrawRect(MarkerColor, CenterX - Step, CenterY + Step, 2.f, 2.f);
			DrawRect(MarkerColor, CenterX + Step, CenterY - Step, 2.f, 2.f);
			DrawRect(MarkerColor, CenterX - Step, CenterY - Step, 2.f, 2.f);
		}
	}
}

void AMassShooterHUD::DrawVitals(AMassShooterCharacter* Pawn)
{
	if (!Pawn)
	{
		return;
	}

	const UMassShooterHealthComponent* Health = Pawn->GetShooterHealth();
	if (!Health)
	{
		return;
	}

	const float X = 40.f;
	const float Y = Canvas->ClipY - 110.f;
	const float Width = 320.f;

	const float MaxHealth = FMath::Max(1.f, Health->GetMaxHealthValue());
	DrawBar(X, Y, Width, 16.f, Health->GetHealthValue() / MaxHealth, ColorHealth);
	DrawShadowedText(FString::Printf(TEXT("%.0f / %.0f"), Health->GetHealthValue(), MaxHealth),
		X + Width + 12.f, Y - 2.f, ColorText);

	if (Health->GetMaxShieldValue() > 0.f)
	{
		DrawBar(X, Y + 22.f, Width, 8.f, Health->GetShieldValue() / Health->GetMaxShieldValue(), ColorShield);
	}

	if (Health->IsSpawnProtected())
	{
		DrawShadowedText(TEXT("SPAWN PROTECTED"), X, Y - 24.f, ColorShield);
	}
}

void AMassShooterHUD::DrawAmmo(AMassShooterCharacter* Pawn)
{
	const UMassShooterLoadoutComponent* Loadout = Pawn ? Pawn->GetLoadout() : nullptr;
	if (!Loadout)
	{
		return;
	}

	const float RightEdge = Canvas->ClipX - 40.f;
	const float Y = Canvas->ClipY - 110.f;

	const FString AmmoText = FString::Printf(TEXT("%.0f / %.0f"),
		Loadout->GetCurrentAmmo(), Loadout->GetCurrentMagazineCount());

	// Right-aligned by measuring first — a fixed offset drifts as the magazine count changes width.
	float TextWidth = 0.f;
	float TextHeight = 0.f;
	GetTextSize(AmmoText, TextWidth, TextHeight, GEngine ? GEngine->GetMediumFont() : nullptr, 1.6f);

	const FLinearColor AmmoColor = Loadout->GetCurrentAmmo() <= 0.f ? ColorDanger : ColorAmmo;
	DrawShadowedText(AmmoText, RightEdge - TextWidth, Y - 6.f, AmmoColor, 1.6f);

	const FString WeaponName = Loadout->GetCurrentWeaponName();
	if (!WeaponName.IsEmpty())
	{
		float NameWidth = 0.f;
		float NameHeight = 0.f;
		GetTextSize(WeaponName, NameWidth, NameHeight, GEngine ? GEngine->GetMediumFont() : nullptr, 1.f);
		DrawShadowedText(WeaponName, RightEdge - NameWidth, Y + 26.f, ColorDim);
	}

	const FString GrenadeText = FString::Printf(TEXT("Grenades  %d"), Loadout->GetGrenades());
	float GrenadeWidth = 0.f;
	float GrenadeHeight = 0.f;
	GetTextSize(GrenadeText, GrenadeWidth, GrenadeHeight, GEngine ? GEngine->GetMediumFont() : nullptr, 1.f);
	DrawShadowedText(GrenadeText, RightEdge - GrenadeWidth, Y + 46.f, ColorDim);
}

void AMassShooterHUD::DrawMatchBanner(AMassShooterGameState* GS)
{
	if (!GS)
	{
		return;
	}

	const float CenterX = Canvas->ClipX * 0.5f;

	const int32 Seconds = FMath::CeilToInt(GS->GetPhaseTimeRemaining());
	const FString TimeText = FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);

	float TimeWidth = 0.f;
	float TimeHeight = 0.f;
	GetTextSize(TimeText, TimeWidth, TimeHeight, GEngine ? GEngine->GetMediumFont() : nullptr, 1.8f);

	DrawRect(ColorPanel, CenterX - 150.f, 14.f, 300.f, 62.f);
	DrawShadowedText(TimeText, CenterX - TimeWidth * 0.5f, 20.f, ColorText, 1.8f);

	// Team scores flank the clock, in team colour, so which side you are on is readable at a
	// glance without reading a number.
	float OffsetX = -140.f;
	for (const FMassShooterTeamScore& Row : GS->TeamScores)
	{
		const FString ScoreText = FString::Printf(TEXT("%d"), Row.Score);
		DrawShadowedText(ScoreText, CenterX + OffsetX, 52.f, GetTeamColor(Row.TeamId), 1.2f);
		OffsetX += 70.f;
		if (OffsetX > 140.f)
		{
			break;
		}
	}

	FString PhaseText;
	switch (GS->MatchPhase)
	{
	case EMassShooterMatchPhase::Warmup:
		PhaseText = TEXT("WARMUP");
		break;
	case EMassShooterMatchPhase::PostMatch:
		PhaseText = GS->WinningTeamId > 0
			? FString::Printf(TEXT("TEAM %d WINS"), GS->WinningTeamId)
			: FString(TEXT("DRAW"));
		break;
	default:
		break;
	}

	if (!PhaseText.IsEmpty())
	{
		float PhaseWidth = 0.f;
		float PhaseHeight = 0.f;
		GetTextSize(PhaseText, PhaseWidth, PhaseHeight, GEngine ? GEngine->GetMediumFont() : nullptr, 2.2f);
		DrawShadowedText(PhaseText, CenterX - PhaseWidth * 0.5f, 96.f, ColorText, 2.2f);
	}

	if (GS->CurrentWave > 0)
	{
		DrawShadowedText(FString::Printf(TEXT("Wave %d   -   %d hostiles"), GS->CurrentWave, GS->BotsAlive),
			CenterX - 140.f, 78.f, ColorDim);
	}
}

void AMassShooterHUD::DrawObjectives(AMassShooterGameState* GS)
{
	if (!GS || GS->MatchMode != EMassShooterMatchMode::Domination || !GetWorld())
	{
		return;
	}

	float X = 40.f;
	const float Y = 40.f;

	for (TActorIterator<AMassShooterCapturePoint> It(GetWorld()); It; ++It)
	{
		const AMassShooterCapturePoint* Point = *It;

		DrawRect(ColorPanel, X - 6.f, Y - 6.f, 76.f, 58.f);
		DrawShadowedText(Point->PointName, X, Y, GetTeamColor(Point->OwnerTeamId), 1.6f);

		// Progress reads as "how far from being taken", which is the thing a player watching the
		// bar actually needs; contested is flagged in red because nothing is moving.
		const FLinearColor BarColor = Point->IsContested() ? ColorDanger : GetTeamColor(Point->CapturingTeamId);
		DrawBar(X, Y + 32.f, 60.f, 6.f, Point->CaptureProgress, BarColor);

		X += 84.f;
	}
}

void AMassShooterHUD::DrawKillFeed(AMassShooterGameState* GS)
{
	if (!GS)
	{
		return;
	}

	const float RightEdge = Canvas->ClipX - 40.f;
	float Y = 40.f;

	const float Now = GS->GetServerWorldTimeSeconds();

	for (const FMassShooterKillFeedEntry& Entry : GS->KillFeed)
	{
		const float Age = Now - Entry.ServerTime;
		if (Age > KillFeedEntryLifetime)
		{
			continue;
		}

		// Fade over the last second so lines leave rather than blink out.
		const float Alpha = FMath::Clamp(KillFeedEntryLifetime - Age, 0.f, 1.f);

		FString Line = Entry.WeaponName.IsEmpty()
			? FString::Printf(TEXT("%s  >>  %s"), *Entry.KillerName, *Entry.VictimName)
			: FString::Printf(TEXT("%s  [%s]  %s"), *Entry.KillerName, *Entry.WeaponName, *Entry.VictimName);

		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Line, Width, Height, GEngine ? GEngine->GetMediumFont() : nullptr, 1.f);

		FLinearColor Color = GetTeamColor(Entry.KillerTeamId);
		Color.A = Alpha;

		DrawShadowedText(Line, RightEdge - Width, Y, Color);
		Y += 20.f;
	}
}

void AMassShooterHUD::DrawRespawnPrompt(AMassShooterCharacter* Pawn)
{
	if (!Pawn || !Pawn->IsDeadShooter() || !PlayerOwner)
	{
		return;
	}

	const AMassShooterPlayerState* State = PlayerOwner->GetPlayerState<AMassShooterPlayerState>();
	const AMassShooterGameState* GS = GetWorld() ? GetWorld()->GetGameState<AMassShooterGameState>() : nullptr;
	if (!State || !GS)
	{
		return;
	}

	const float Remaining = FMath::Max(0.f, State->RespawnAvailableTime - GS->GetServerWorldTimeSeconds());

	const FString Text = Remaining > 0.f
		? FString::Printf(TEXT("RESPAWNING IN %.0f"), FMath::CeilToFloat(Remaining))
		: FString(TEXT("PRESS ENTER TO RESPAWN"));

	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height, GEngine ? GEngine->GetMediumFont() : nullptr, 2.f);

	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;

	DrawRect(FLinearColor(0.4f, 0.f, 0.f, 0.25f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	DrawShadowedText(Text, CenterX - Width * 0.5f, CenterY - 40.f, ColorDanger, 2.f);
}

void AMassShooterHUD::DrawDamageIndicators()
{
	if (!PlayerOwner || !PlayerOwner->GetPawn() || !GetWorld())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	DamageDirections.RemoveAll([Now](const FDamageDirection& Entry) { return Entry.EndTime <= Now; });

	const FVector PawnLocation = PlayerOwner->GetPawn()->GetActorLocation();
	const FRotator ViewRotation = PlayerOwner->GetControlRotation();

	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	const float Radius = FMath::Min(Canvas->ClipX, Canvas->ClipY) * 0.28f;

	for (const FDamageDirection& Entry : DamageDirections)
	{
		FVector ToSource = Entry.WorldLocation - PawnLocation;
		ToSource.Z = 0.f;
		if (ToSource.IsNearlyZero())
		{
			continue;
		}

		// Angle relative to where the player is FACING, so the marker points at the shooter as the
		// player turns rather than staying pinned to a world direction.
		const float SourceYaw = FMath::RadiansToDegrees(FMath::Atan2(ToSource.Y, ToSource.X));
		const float RelativeYaw = FMath::DegreesToRadians(FRotator::NormalizeAxis(SourceYaw - ViewRotation.Yaw));

		const float X = CenterX + FMath::Sin(RelativeYaw) * Radius;
		const float Y = CenterY - FMath::Cos(RelativeYaw) * Radius;

		FLinearColor Color = ColorDanger;
		Color.A = FMath::Clamp((Entry.EndTime - Now) / FMath::Max(0.01f, DamageIndicatorDuration), 0.f, 1.f);

		DrawRect(Color, X - 9.f, Y - 3.f, 18.f, 6.f);
	}
}

void AMassShooterHUD::DrawAbilityBar(AMassShooterCharacter* Pawn)
{
	if (!bDrawAbilityBar || !Pawn)
	{
		return;
	}

	// DefaultAbilities is the slot-ordered list the input handlers index into, so drawing it in
	// order means slot N on screen really is the ability key N activates.
	const TArray<TSubclassOf<UGameplayAbilityBase>>& Abilities = Pawn->DefaultAbilities;
	if (Abilities.Num() == 0)
	{
		return;
	}

	const float SlotStride = AbilitySlotSize + AbilitySlotPadding;
	const float TotalWidth = SlotStride * Abilities.Num() - AbilitySlotPadding;
	const float StartX = (Canvas->ClipX - TotalWidth) * 0.5f;
	const float Y = Canvas->ClipY - AbilityBarBottomMargin - AbilitySlotSize;

	for (int32 Index = 0; Index < Abilities.Num(); ++Index)
	{
		const TSubclassOf<UGameplayAbilityBase> AbilityClass = Abilities[Index];
		const float X = StartX + Index * SlotStride;

		// Frame first, so an ability with no icon still reads as an occupied slot.
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), X - 2.f, Y - 2.f, AbilitySlotSize + 4.f, AbilitySlotSize + 4.f);
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.07f), X, Y, AbilitySlotSize, AbilitySlotSize);

		if (!AbilityClass)
		{
			continue;
		}

		const UGameplayAbilityBase* CDO = AbilityClass->GetDefaultObject<UGameplayAbilityBase>();
		if (!CDO)
		{
			continue;
		}

		if (UTexture2D* Icon = CDO->AbilityIcon)
		{
			DrawTexture(Icon, X, Y, AbilitySlotSize, AbilitySlotSize, 0.f, 0.f, 1.f, 1.f, FLinearColor::White);
		}
		else
		{
			// No icon authored: show the start of the ability's name so the slot is still readable.
			const FString Fallback = CDO->AbilityName.Left(3);
			DrawShadowedText(Fallback, X + 6.f, Y + AbilitySlotSize * 0.5f - 8.f, ColorDim);
		}

		// Cooldown: darken the whole slot. IsAbilityOnCooldownByClass is RTSUnitTemplate's own
		// query and works for Blueprint abilities without needing their spec handle.
		if (Pawn->IsAbilityOnCooldownByClass(AbilityClass))
		{
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.62f), X, Y, AbilitySlotSize, AbilitySlotSize);
		}

		// Key hint = the slot number, which is what actually activates it (keys 1..6 map to slots
		// 0..5). Deliberately NOT UGameplayAbilityBase::KeyboardKey: that field defaults to "X" and
		// most abilities never change it, so the bar showed a row of X's that corresponded to no
		// key at all.
		if (Index < 9)
		{
			DrawShadowedText(FString::FromInt(Index + 1), X + 5.f, Y + AbilitySlotSize - 19.f, ColorText, 1.1f);
		}
	}
}

void AMassShooterHUD::DrawScoreboard(AMassShooterGameState* GS)
{
	if (!GS)
	{
		return;
	}

	const float PanelWidth = FMath::Min(760.f, Canvas->ClipX - 80.f);
	const float PanelX = (Canvas->ClipX - PanelWidth) * 0.5f;
	const float PanelY = 130.f;
	const float RowHeight = 24.f;

	const int32 RowCount = GS->PlayerArray.Num();
	const float PanelHeight = 70.f + RowHeight * FMath::Max(1, RowCount);

	DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.88f), PanelX, PanelY, PanelWidth, PanelHeight);

	DrawShadowedText(TEXT("PLAYER"), PanelX + 20.f,  PanelY + 16.f, ColorDim);
	DrawShadowedText(TEXT("TEAM"),   PanelX + 300.f, PanelY + 16.f, ColorDim);
	DrawShadowedText(TEXT("KILLS"),  PanelX + 380.f, PanelY + 16.f, ColorDim);
	DrawShadowedText(TEXT("BOTS"),   PanelX + 460.f, PanelY + 16.f, ColorDim);
	DrawShadowedText(TEXT("DEATHS"), PanelX + 540.f, PanelY + 16.f, ColorDim);
	DrawShadowedText(TEXT("SCORE"),  PanelX + 640.f, PanelY + 16.f, ColorDim);

	// Sort a local copy: PlayerArray order is join order, which is not a ranking.
	TArray<AMassShooterPlayerState*> Rows;
	Rows.Reserve(RowCount);
	for (const TObjectPtr<APlayerState>& State : GS->PlayerArray)
	{
		if (AMassShooterPlayerState* ShooterState = Cast<AMassShooterPlayerState>(State.Get()))
		{
			Rows.Add(ShooterState);
		}
	}
	Rows.Sort([](const AMassShooterPlayerState& A, const AMassShooterPlayerState& B)
	{
		return A.MatchScore > B.MatchScore;
	});

	float Y = PanelY + 46.f;
	for (const AMassShooterPlayerState* Row : Rows)
	{
		const FLinearColor TeamColor = GetTeamColor(Row->ShooterTeamId);

		DrawShadowedText(Row->GetPlayerName(),                          PanelX + 20.f,  Y, ColorText);
		DrawShadowedText(FString::Printf(TEXT("%d"), Row->ShooterTeamId), PanelX + 300.f, Y, TeamColor);
		DrawShadowedText(FString::Printf(TEXT("%d"), Row->Kills),         PanelX + 380.f, Y, ColorText);
		DrawShadowedText(FString::Printf(TEXT("%d"), Row->BotKills),      PanelX + 460.f, Y, ColorText);
		DrawShadowedText(FString::Printf(TEXT("%d"), Row->Deaths),        PanelX + 540.f, Y, ColorDim);
		DrawShadowedText(FString::Printf(TEXT("%d"), Row->MatchScore),    PanelX + 640.f, Y, TeamColor);

		Y += RowHeight;
	}
}
