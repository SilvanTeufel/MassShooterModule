// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "GameStates/MassShooterGameState.h"
#include "Net/UnrealNetwork.h"

AMassShooterGameState::AMassShooterGameState()
{
	// The kill feed and objective bar have to feel immediate; this is a handful of ints.
	SetNetUpdateFrequency(10.f);
}

void AMassShooterGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMassShooterGameState, MatchPhase);
	DOREPLIFETIME(AMassShooterGameState, MatchMode);
	DOREPLIFETIME(AMassShooterGameState, PhaseEndTime);
	DOREPLIFETIME(AMassShooterGameState, ScoreLimit);
	DOREPLIFETIME(AMassShooterGameState, WinningTeamId);
	DOREPLIFETIME(AMassShooterGameState, CurrentWave);
	DOREPLIFETIME(AMassShooterGameState, BotsAlive);
	DOREPLIFETIME(AMassShooterGameState, TeamScores);
	DOREPLIFETIME(AMassShooterGameState, KillFeed);
}

void AMassShooterGameState::RegisterTeam(int32 TeamId)
{
	if (!HasAuthority())
	{
		return;
	}

	const bool bExists = TeamScores.ContainsByPredicate(
		[TeamId](const FMassShooterTeamScore& Row) { return Row.TeamId == TeamId; });

	if (!bExists)
	{
		FMassShooterTeamScore Row;
		Row.TeamId = TeamId;
		TeamScores.Add(Row);
	}
}

void AMassShooterGameState::AddTeamScore(int32 TeamId, int32 Delta, bool bCountAsKill)
{
	if (!HasAuthority())
	{
		return;
	}

	RegisterTeam(TeamId);

	for (FMassShooterTeamScore& Row : TeamScores)
	{
		if (Row.TeamId == TeamId)
		{
			Row.Score += Delta;
			if (bCountAsKill)
			{
				++Row.Kills;
			}
			return;
		}
	}
}

void AMassShooterGameState::SetTeamHeldPoints(int32 TeamId, int32 HeldPoints)
{
	if (!HasAuthority())
	{
		return;
	}

	RegisterTeam(TeamId);

	for (FMassShooterTeamScore& Row : TeamScores)
	{
		if (Row.TeamId == TeamId)
		{
			Row.HeldPoints = HeldPoints;
			return;
		}
	}
}

void AMassShooterGameState::PushKillFeed(const FMassShooterKillFeedEntry& Entry)
{
	if (!HasAuthority())
	{
		return;
	}

	KillFeed.Insert(Entry, 0);

	// Bounded on purpose: this array replicates in full on every change, so an unbounded feed
	// would grow the per-kill payload without limit over a long match.
	while (KillFeed.Num() > FMath::Max(1, MaxKillFeedEntries))
	{
		KillFeed.Pop();
	}
}

int32 AMassShooterGameState::GetTeamScore(int32 TeamId) const
{
	for (const FMassShooterTeamScore& Row : TeamScores)
	{
		if (Row.TeamId == TeamId)
		{
			return Row.Score;
		}
	}
	return 0;
}

int32 AMassShooterGameState::GetLeadingTeam() const
{
	int32 BestTeam = -1;
	int32 BestScore = TNumericLimits<int32>::Lowest();
	bool bTied = false;

	for (const FMassShooterTeamScore& Row : TeamScores)
	{
		if (Row.Score > BestScore)
		{
			BestScore = Row.Score;
			BestTeam = Row.TeamId;
			bTied = false;
		}
		else if (Row.Score == BestScore)
		{
			bTied = true;
		}
	}

	// A tie has no leader — reporting one would put a winner banner on a draw.
	return bTied ? -1 : BestTeam;
}

float AMassShooterGameState::GetPhaseTimeRemaining() const
{
	// GetServerWorldTimeSeconds is the client-side estimate of server time, which is exactly the
	// clock PhaseEndTime was stamped against — so this counts down identically everywhere.
	return FMath::Max(0.f, PhaseEndTime - GetServerWorldTimeSeconds());
}
