// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "PlayerState/MassShooterPlayerState.h"
#include "Net/UnrealNetwork.h"

AMassShooterPlayerState::AMassShooterPlayerState()
{
	// A scoreboard row does not need 30 Hz, but it does need to be visibly live during a firefight.
	SetNetUpdateFrequency(4.f);
}

void AMassShooterPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMassShooterPlayerState, bIsAiPlayer);
	DOREPLIFETIME(AMassShooterPlayerState, ShooterTeamId);
	DOREPLIFETIME(AMassShooterPlayerState, Kills);
	DOREPLIFETIME(AMassShooterPlayerState, Deaths);
	DOREPLIFETIME(AMassShooterPlayerState, BotKills);
	DOREPLIFETIME(AMassShooterPlayerState, MatchScore);
	DOREPLIFETIME(AMassShooterPlayerState, KillStreak);
	DOREPLIFETIME(AMassShooterPlayerState, BestKillStreak);
	DOREPLIFETIME(AMassShooterPlayerState, RespawnAvailableTime);
	DOREPLIFETIME(AMassShooterPlayerState, bAwaitingRespawn);
}

void AMassShooterPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	if (AMassShooterPlayerState* Other = Cast<AMassShooterPlayerState>(PlayerState))
	{
		Other->ShooterTeamId = ShooterTeamId;
		Other->Kills = Kills;
		Other->Deaths = Deaths;
		Other->BotKills = BotKills;
		Other->MatchScore = MatchScore;
		Other->BestKillStreak = BestKillStreak;
	}
}

void AMassShooterPlayerState::AddKill(bool bVictimWasBot, int32 ScoreValue)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bVictimWasBot)
	{
		++BotKills;
	}
	else
	{
		++Kills;
	}

	// Bots feed the streak too — in Survival they are the only thing there is to kill.
	++KillStreak;
	BestKillStreak = FMath::Max(BestKillStreak, KillStreak);

	MatchScore += ScoreValue;
}

void AMassShooterPlayerState::AddDeath()
{
	if (!HasAuthority())
	{
		return;
	}

	++Deaths;
	KillStreak = 0;
}

void AMassShooterPlayerState::AddScore(int32 Delta)
{
	if (HasAuthority())
	{
		MatchScore += Delta;
	}
}

void AMassShooterPlayerState::SetShooterTeam(int32 NewTeamId)
{
	if (HasAuthority())
	{
		ShooterTeamId = NewTeamId;
	}
}

void AMassShooterPlayerState::BeginRespawnWait(float AvailableAtServerTime)
{
	if (HasAuthority())
	{
		bAwaitingRespawn = true;
		RespawnAvailableTime = AvailableAtServerTime;
	}
}

void AMassShooterPlayerState::EndRespawnWait()
{
	if (HasAuthority())
	{
		bAwaitingRespawn = false;
		RespawnAvailableTime = 0.f;
	}
}

float AMassShooterPlayerState::GetKDRatio() const
{
	const int32 TotalKills = Kills + BotKills;
	return Deaths > 0 ? static_cast<float>(TotalKills) / static_cast<float>(Deaths) : static_cast<float>(TotalKills);
}
