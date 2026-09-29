// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "GameModes/MassShooterGameMode.h"
#include "Controller/PlayerController/ControllerBase.h"
#include "System/PlayerTeamSubsystem.h"
#include "GameStates/MassShooterGameState.h"
#include "PlayerState/MassShooterPlayerState.h"
#include "Controller/MassShooterPlayerController.h"
#include "Characters/MassShooterCharacter.h"
#include "Characters/MassShooterBot.h"
#include "Actors/MassShooterBotSpawner.h"
#include "Actors/MassShooterPlayerStart.h"
#include "Components/MassShooterHealthComponent.h"
#include "Components/MassShooterLoadoutComponent.h"
#include "Hud/MassShooterHUD.h"
#include "Settings/MassShooterSettings.h"
#include "MassShooterLog.h"

#include "Characters/Unit/UnitBase.h"

#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"  // GetGameInstance()->GetSubsystem braucht den vollstaendigen Typ

AMassShooterGameMode::AMassShooterGameMode()
{
	PrimaryActorTick.bCanEverTick = true;

	GameStateClass = AMassShooterGameState::StaticClass();
	PlayerStateClass = AMassShooterPlayerState::StaticClass();
	PlayerControllerClass = AMassShooterPlayerController::StaticClass();
	DefaultPawnClass = AMassShooterCharacter::StaticClass();
	HUDClass = AMassShooterHUD::StaticClass();

	// ARTSGameModeBase ships DisableSpawn = true, which suppresses its DataTable-driven RTS spawn
	// waves. That is what we want: bots come from AMassShooterBotSpawner instead. Stated here so
	// nobody has to go and check the base default.
	DisableSpawn = true;

	bStartPlayersAsSpectators = false;
}

void AMassShooterGameMode::BeginPlay()
{
	// ARTSGameModeBase::BeginPlay does the navigation initialisation and unit-array setup the
	// spawners depend on, so this one IS chained (unlike the controller's).
	Super::BeginPlay();

	CacheSpawners();
	ApplyTeamAlliances();

	if (AMassShooterGameState* GS = GetGameState<AMassShooterGameState>())
	{
		GS->MatchMode = MatchMode;
		GS->ScoreLimit = ScoreLimit;

		// Seed a score row per team so the scoreboard shows both sides from the first frame
		// instead of popping a team in when it first scores.
		const UMassShooterSettings* Settings = UMassShooterSettings::Get();
		for (int32 Index = 0; Index < Settings->NumPlayerTeams; ++Index)
		{
			GS->RegisterTeam(Settings->FirstPlayerTeamId + Index);
		}
	}

	SetMatchPhase(EMassShooterMatchPhase::Warmup);

	UE_LOG(LogMassShooterMatch, Log, TEXT("Match starting: mode=%d scoreLimit=%d matchSeconds=%.0f spawners=%d"),
		static_cast<int32>(MatchMode), ScoreLimit, MatchSeconds, Spawners.Num());
}

void AMassShooterGameMode::CacheSpawners()
{
	Spawners.Reset();
	for (TActorIterator<AMassShooterBotSpawner> It(GetWorld()); It; ++It)
	{
		if (It->bUseForWaves)
		{
			Spawners.Add(*It);
		}
	}
}

void AMassShooterGameMode::CheckWinLoseCondition(AUnitBase* /*DestroyedUnit*/)
{
	// Intentionally empty. See the class comment: the inherited rule ends the match when a team
	// runs out of units, which in a respawning shooter fires constantly and wrongly.
}

// ---------------------------------------------------------------------------------------------
//  Login / teams / spawns
// ---------------------------------------------------------------------------------------------

int32 AMassShooterGameMode::PickTeamForNewPlayer(AController* Joining) const
{
	const UMassShooterSettings* Settings = UMassShooterSettings::Get();

	// Survival is co-op: everyone shares one team, or the players shoot each other.
	if (MatchMode == EMassShooterMatchMode::Survival)
	{
		return Settings->FirstPlayerTeamId;
	}

	TArray<int32> Counts;
	Counts.SetNumZeroed(FMath::Max(1, Settings->NumPlayerTeams));

	const APlayerState* JoiningState = Joining ? Joining->PlayerState : nullptr;

	if (GameState)
	{
		for (const TObjectPtr<APlayerState>& State : GameState->PlayerArray)
		{
			// Skip the player being assigned: its state is already in the array with the default
			// team, and counting it would make that team look occupied by the player we are about
			// to place.
			if (State.Get() == JoiningState)
			{
				continue;
			}

			if (const AMassShooterPlayerState* ShooterState = Cast<AMassShooterPlayerState>(State.Get()))
			{
				// RTS AI commanders own a player state too. Counting them would make their team
				// look full and push every human onto the other side.
				if (ShooterState->bIsAiPlayer)
				{
					continue;
				}

				const int32 Index = ShooterState->ShooterTeamId - Settings->FirstPlayerTeamId;
				if (Counts.IsValidIndex(Index))
				{
					++Counts[Index];
				}
			}
		}
	}

	int32 SmallestIndex = 0;
	for (int32 Index = 1; Index < Counts.Num(); ++Index)
	{
		if (Counts[Index] < Counts[SmallestIndex])
		{
			SmallestIndex = Index;
		}
	}

	return Settings->FirstPlayerTeamId + SmallestIndex;
}

void AMassShooterGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	const int32 TeamId = PickTeamForNewPlayer(NewPlayer);

	if (AMassShooterPlayerState* State = NewPlayer->GetPlayerState<AMassShooterPlayerState>())
	{
		State->SetShooterTeam(TeamId);
	}

	// The controller's team id is what RTSUnitTemplate's selection/alliance plumbing reads.
	if (AMassShooterPlayerController* PC = Cast<AMassShooterPlayerController>(NewPlayer))
	{
		PC->SelectableTeamId = TeamId;
	}

	if (AMassShooterCharacter* Pawn = Cast<AMassShooterCharacter>(NewPlayer->GetPawn()))
	{
		Pawn->SetShooterTeam(TeamId);
		RegisterUnitForDeathTracking(Pawn);
	}

	if (AMassShooterGameState* GS = GetGameState<AMassShooterGameState>())
	{
		GS->RegisterTeam(TeamId);
	}

	UE_LOG(LogMassShooterMatch, Log, TEXT("%s joined on team %d."), *NewPlayer->GetName(), TeamId);
}

void AMassShooterGameMode::Logout(AController* Exiting)
{
	if (APawn* Pawn = Exiting ? Exiting->GetPawn() : nullptr)
	{
		TrackedUnits.Remove(Pawn);
	}

	Super::Logout(Exiting);
}

AActor* AMassShooterGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	int32 WantedTeam = 0;
	if (const AMassShooterPlayerState* State = Player ? Player->GetPlayerState<AMassShooterPlayerState>() : nullptr)
	{
		WantedTeam = State->ShooterTeamId;
	}

	// Score every candidate: correct team first, then distance to the nearest enemy. Spawning
	// into someone's crosshairs is the single worst thing a shooter spawn selector can do, so
	// enemy proximity is the tiebreaker rather than round-robin.
	AActor* BestStart = nullptr;
	float BestScore = -FLT_MAX;

	for (TActorIterator<AMassShooterPlayerStart> It(GetWorld()); It; ++It)
	{
		AMassShooterPlayerStart* Start = *It;
		if (!Start->bEnabled)
		{
			continue;
		}

		if (Start->TeamId != 0 && WantedTeam != 0 && Start->TeamId != WantedTeam)
		{
			continue;
		}

		float NearestEnemyDistance = FLT_MAX;
		for (TActorIterator<AUnitBase> UnitIt(GetWorld()); UnitIt; ++UnitIt)
		{
			const AUnitBase* Unit = *UnitIt;
			if (!Unit || Unit->TeamId == WantedTeam || Unit->GetUnitState() == UnitData::Dead)
			{
				continue;
			}
			NearestEnemyDistance = FMath::Min(NearestEnemyDistance,
				FVector::Dist(Unit->GetActorLocation(), Start->GetActorLocation()));
		}

		// Clamp so a start with no enemies anywhere near does not beat every other consideration,
		// and add jitter so repeated deaths do not always return the same corner.
		const float Safety = FMath::Min(NearestEnemyDistance, Start->EnemyAvoidanceRadius * 2.f);
		const float Score = Safety + FMath::FRandRange(0.f, 200.f);

		if (Score > BestScore)
		{
			BestScore = Score;
			BestStart = Start;
		}
	}

	if (BestStart)
	{
		return BestStart;
	}

	// No shooter starts placed: fall back to the standard selector so a level with plain
	// APlayerStarts still works.
	return Super::ChoosePlayerStart_Implementation(Player);
}

// ---------------------------------------------------------------------------------------------
//  Death tracking + scoring
// ---------------------------------------------------------------------------------------------

void AMassShooterGameMode::RegisterUnitForDeathTracking(AActor* Unit)
{
	if (!Unit || TrackedUnits.Contains(Unit))
	{
		return;
	}

	UMassShooterHealthComponent* Health = Unit->FindComponentByClass<UMassShooterHealthComponent>();
	if (!Health)
	{
		return;
	}

	Health->OnDeath.AddDynamic(this, &AMassShooterGameMode::HandleUnitDeath);
	TrackedUnits.Add(Unit);
}

int32 AMassShooterGameMode::GetActorTeamId(const AActor* Actor)
{
	if (const AUnitBase* Unit = Cast<AUnitBase>(Actor))
	{
		return Unit->TeamId;
	}

	// A projectile or ability actor: fall back to whoever owns it.
	if (Actor)
	{
		if (const AUnitBase* OwnerUnit = Cast<AUnitBase>(Actor->GetOwner()))
		{
			return OwnerUnit->TeamId;
		}
	}
	return 0;
}

void AMassShooterGameMode::HandleUnitDeath(AActor* Victim, AActor* Killer)
{
	if (!HasAuthority())
	{
		return;
	}

	ScoreKill(Victim, Killer);
	RefreshBotCount();
	EvaluateEndConditions();
}

FString AMassShooterGameMode::ResolveVictimName(const AActor* Victim, const AMassShooterBot* VictimBot)
{
	// This module's own bots carry an authored kill-feed name.
	if (VictimBot && !VictimBot->BotDisplayName.IsEmpty())
	{
		return VictimBot->BotDisplayName;
	}

	// A foreign RTS unit does not, but RTSUnitTemplate gives every unit an editable Name. "Unit"
	// is that field's default and means nobody filled it in, so it is worth no more than the
	// generic fallback - and the raw actor name (BP_UnitBase_Xeno_Skitterling_AH_C_3) is worse
	// than either in a kill feed.
	if (const AUnitBase* Unit = Cast<AUnitBase>(Victim))
	{
		if (!Unit->Name.IsEmpty() && Unit->Name != TEXT("Unit"))
		{
			return Unit->Name;
		}
	}

	return TEXT("Hostile");
}

void AMassShooterGameMode::ScoreKill(AActor* Victim, AActor* Killer)
{
	AMassShooterGameState* GS = GetGameState<AMassShooterGameState>();
	if (!GS)
	{
		return;
	}

	AMassShooterCharacter* VictimPlayer = Cast<AMassShooterCharacter>(Victim);
	const AMassShooterBot* VictimBot = Cast<AMassShooterBot>(Victim);

	// "Not a player" is exactly "a bot" here, and that is not a shortcut: this function only ever
	// runs for actors carrying a UMassShooterHealthComponent, and only three kinds do - player
	// pawns, this module's own AMassShooterBot, and foreign RTS units the spawner adapted for a
	// wave (bAdaptForeignUnits). Testing for AMassShooterBot alone therefore scored every
	// Xenocrypta wave unit as a PLAYER kill: 2 points instead of 1, counted in Kills instead of
	// BotKills. Measured on Level_14 - one Skitterling kill, MatchScore 2, BotKills 0.
	const bool bVictimWasBot = (VictimPlayer == nullptr);

	const int32 VictimTeam = GetActorTeamId(Victim);
	const int32 KillerTeam = GetActorTeamId(Killer);

	// ---- Victim bookkeeping -------------------------------------------------------------------
	FString VictimName = ResolveVictimName(Victim, VictimBot);

	if (VictimPlayer)
	{
		if (AMassShooterPlayerState* State = VictimPlayer->GetPlayerState<AMassShooterPlayerState>())
		{
			VictimName = State->GetPlayerName();
			State->AddDeath();
			State->BeginRespawnWait(GS->GetServerWorldTimeSeconds() + RespawnDelay);
		}
	}

	// ---- Killer bookkeeping -------------------------------------------------------------------
	FString KillerName = TEXT("World");
	FString WeaponName;
	int32 ScoreValue = 0;

	if (Killer && Killer != Victim)
	{
		const bool bTeamKill = KillerTeam != 0 && KillerTeam == VictimTeam;

		if (bTeamKill)
		{
			ScoreValue = ScorePerTeamKill;
		}
		else if (bVictimWasBot)
		{
			// A foreign wave unit has no per-unit ScoreValue, so it falls back to the mode's rate.
			ScoreValue = (VictimBot && VictimBot->ScoreValue > 0) ? VictimBot->ScoreValue : ScorePerBotKill;
		}
		else
		{
			ScoreValue = ScorePerPlayerKill;
		}

		if (AMassShooterCharacter* KillerPlayer = Cast<AMassShooterCharacter>(Killer))
		{
			if (AMassShooterPlayerState* State = KillerPlayer->GetPlayerState<AMassShooterPlayerState>())
			{
				KillerName = State->GetPlayerName();

				// A team kill still counts as a death for the victim, but must not feed the
				// killer's streak — so score it without touching the kill counters.
				if (bTeamKill)
				{
					State->AddScore(ScoreValue);
				}
				else
				{
					State->AddKill(bVictimWasBot, ScoreValue);
				}
			}

			if (const UMassShooterLoadoutComponent* KillerLoadout = KillerPlayer->GetLoadout())
			{
				WeaponName = KillerLoadout->GetCurrentWeaponName();
			}
		}
		else if (const AMassShooterBot* KillerBot = Cast<AMassShooterBot>(Killer))
		{
			KillerName = KillerBot->BotDisplayName;
		}

		// Team score only moves during the match proper — warmup kills are practice.
		if (KillerTeam > 0 && GS->MatchPhase == EMassShooterMatchPhase::InProgress)
		{
			GS->AddTeamScore(KillerTeam, ScoreValue, /*bCountAsKill*/ !bTeamKill);
		}
	}

	FMassShooterKillFeedEntry Entry;
	Entry.KillerName = KillerName;
	Entry.VictimName = VictimName;
	Entry.WeaponName = WeaponName;
	Entry.KillerTeamId = KillerTeam;
	Entry.bVictimWasBot = bVictimWasBot;
	Entry.ServerTime = GS->GetServerWorldTimeSeconds();
	GS->PushKillFeed(Entry);
}

// ---------------------------------------------------------------------------------------------
//  Respawn
// ---------------------------------------------------------------------------------------------

bool AMassShooterGameMode::TryRespawnPlayer(AController* Controller)
{
	if (!HasAuthority() || !Controller)
	{
		return false;
	}

	AMassShooterCharacter* Pawn = Cast<AMassShooterCharacter>(Controller->GetPawn());
	AMassShooterPlayerState* State = Controller->GetPlayerState<AMassShooterPlayerState>();
	if (!Pawn || !State)
	{
		return false;
	}

	if (!Pawn->IsDeadShooter())
	{
		return false;
	}

	// The client can ask early; the server is where the wait is actually enforced.
	if (GameState && GameState->GetServerWorldTimeSeconds() < State->RespawnAvailableTime)
	{
		return false;
	}

	AActor* Start = ChoosePlayerStart(Controller);
	const FTransform Where = Start ? Start->GetActorTransform() : Pawn->GetActorTransform();

	Pawn->RespawnAt(Where);
	State->EndRespawnWait();
	return true;
}

void AMassShooterGameMode::RespawnAllPlayers()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}

		if (AMassShooterCharacter* Pawn = Cast<AMassShooterCharacter>(PC->GetPawn()))
		{
			AActor* Start = ChoosePlayerStart(PC);
			Pawn->RespawnAt(Start ? Start->GetActorTransform() : Pawn->GetActorTransform());
		}

		if (AMassShooterPlayerState* State = PC->GetPlayerState<AMassShooterPlayerState>())
		{
			State->EndRespawnWait();
		}
	}
}

// ---------------------------------------------------------------------------------------------
//  Phases, waves, end conditions
// ---------------------------------------------------------------------------------------------

void AMassShooterGameMode::SetMatchPhase(EMassShooterMatchPhase NewPhase)
{
	AMassShooterGameState* GS = GetGameState<AMassShooterGameState>();
	if (!GS)
	{
		return;
	}

	GS->MatchPhase = NewPhase;

	const float Now = GS->GetServerWorldTimeSeconds();
	switch (NewPhase)
	{
	case EMassShooterMatchPhase::Warmup:
		GS->PhaseEndTime = Now + FMath::Max(0.f, WarmupSeconds);
		break;

	case EMassShooterMatchPhase::InProgress:
		// A zero match length means "score limit only" — park the deadline far out rather than
		// special-casing it in every countdown reader.
		GS->PhaseEndTime = MatchSeconds > 0.f ? Now + MatchSeconds : Now + 3600.f * 24.f;
		WaveTimer = 0.f;
		break;

	case EMassShooterMatchPhase::PostMatch:
		GS->PhaseEndTime = Now + FMath::Max(1.f, PostMatchSeconds);
		break;
	}

	UE_LOG(LogMassShooterMatch, Log, TEXT("Match phase -> %d (ends at %.1f)"),
		static_cast<int32>(NewPhase), GS->PhaseEndTime);
}

void AMassShooterGameMode::RestartMatch()
{
	AMassShooterGameState* GS = GetGameState<AMassShooterGameState>();
	if (!GS)
	{
		return;
	}

	for (FMassShooterTeamScore& Row : GS->TeamScores)
	{
		Row.Score = 0;
		Row.Kills = 0;
	}
	GS->KillFeed.Reset();
	GS->WinningTeamId = -1;
	GS->CurrentWave = 0;

	for (const TObjectPtr<APlayerState>& State : GS->PlayerArray)
	{
		if (AMassShooterPlayerState* ShooterState = Cast<AMassShooterPlayerState>(State.Get()))
		{
			ShooterState->Kills = 0;
			ShooterState->Deaths = 0;
			ShooterState->BotKills = 0;
			ShooterState->MatchScore = 0;
			ShooterState->KillStreak = 0;
			ShooterState->BestKillStreak = 0;
			ShooterState->EndRespawnWait();
		}
	}

	RespawnAllPlayers();
	SetMatchPhase(EMassShooterMatchPhase::Warmup);
}

void AMassShooterGameMode::SpawnNextWave()
{
	AMassShooterGameState* GS = GetGameState<AMassShooterGameState>();
	if (!GS || Spawners.Num() == 0)
	{
		return;
	}

	RefreshBotCount();
	if (GS->BotsAlive >= MaxBotsAlive)
	{
		// The field is already full. Skipping rather than queueing keeps a stalled wave from
		// turning into a burst of 200 bots the moment the field clears.
		return;
	}

	++GS->CurrentWave;

	const int32 Wanted = BotsInFirstWave + BotsAddedPerWave * (GS->CurrentWave - 1);
	const int32 Budget = FMath::Min(Wanted, MaxBotsAlive - GS->BotsAlive);
	if (Budget <= 0)
	{
		return;
	}

	const float Scaling = FMath::Pow(FMath::Max(1.f, WaveScalingPerWave), static_cast<float>(GS->CurrentWave - 1));

	// Spread the wave across every spawner so hostiles arrive from all sides rather than as one
	// column out of a single door.
	int32 Remaining = Budget;
	const int32 PerSpawner = FMath::Max(1, Budget / Spawners.Num());

	for (AMassShooterBotSpawner* Spawner : Spawners)
	{
		if (Remaining <= 0)
		{
			break;
		}
		if (!Spawner)
		{
			continue;
		}

		const int32 Count = FMath::Min(Remaining, PerSpawner);
		Remaining -= Spawner->SpawnWave(Count, Scaling, Scaling);
	}

	// Count again NOW, not on the next 2 s sweep. The early-release trigger in Tick reads
	// GS->BotsAlive, and leaving it at the pre-spawn value meant "field nearly clear" stayed true
	// for every frame until BotScanTimer caught up - so the wave after an emptied field fired
	// again on the very next frame. Measured: waves 4 and 5 25 ms apart.
	RefreshBotCount();

	UE_LOG(LogMassShooterMatch, Log, TEXT("Wave %d: %d bots requested (scaling %.2f, %d alive)."),
		GS->CurrentWave, Budget, Scaling, GS->BotsAlive);
}

void AMassShooterGameMode::MarkAiPlayerStates()
{
	if (!GameState)
	{
		return;
	}

	// Stamped from a sweep rather than at creation time: the AI player controllers are made by
	// ARTSGameModeBase, which this module does not modify and cannot hook, and their bIsAi is set
	// after the controller (and its player state) already exist. Once true it stays true, so the
	// work is one cast per player state per two seconds and stops mattering after the first pass.
	for (const TObjectPtr<APlayerState>& State : GameState->PlayerArray)
	{
		AMassShooterPlayerState* ShooterState = Cast<AMassShooterPlayerState>(State.Get());
		if (!ShooterState || ShooterState->bIsAiPlayer)
		{
			continue;
		}

		if (const AControllerBase* Controller = Cast<AControllerBase>(ShooterState->GetOwningController()))
		{
			if (Controller->bIsAi)
			{
				ShooterState->bIsAiPlayer = true;
			}
		}
	}
}

void AMassShooterGameMode::RefreshBotCount()
{
	AMassShooterGameState* GS = GetGameState<AMassShooterGameState>();
	if (!GS)
	{
		return;
	}

	int32 Alive = 0;
	for (AMassShooterBotSpawner* Spawner : Spawners)
	{
		if (Spawner)
		{
			Alive += Spawner->GetAliveCount();
		}
	}
	GS->BotsAlive = Alive;
}

void AMassShooterGameMode::EvaluateEndConditions()
{
	AMassShooterGameState* GS = GetGameState<AMassShooterGameState>();
	if (!GS || GS->MatchPhase != EMassShooterMatchPhase::InProgress)
	{
		return;
	}

	if (ScoreLimit > 0)
	{
		for (const FMassShooterTeamScore& Row : GS->TeamScores)
		{
			if (Row.Score >= ScoreLimit)
			{
				GS->WinningTeamId = Row.TeamId;
				SetMatchPhase(EMassShooterMatchPhase::PostMatch);
				UE_LOG(LogMassShooterMatch, Log, TEXT("Team %d reached the score limit."), Row.TeamId);
				return;
			}
		}
	}

	if (GS->GetPhaseTimeRemaining() <= 0.f)
	{
		// Time out: the leader wins, and GetLeadingTeam already returns -1 on a tie, which the
		// HUD renders as a draw.
		GS->WinningTeamId = GS->GetLeadingTeam();
		SetMatchPhase(EMassShooterMatchPhase::PostMatch);
	}
}

// ---------------------------------------------------------------------------------------------
//  Tick
// ---------------------------------------------------------------------------------------------

void AMassShooterGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

	AMassShooterGameState* GS = GetGameState<AMassShooterGameState>();
	if (!GS)
	{
		return;
	}

	// Pick up units that appeared after login: pawns respawned by other systems, bots from a
	// wave, anything a Blueprint spawned. Subscribing is idempotent (TrackedUnits guards it), so
	// a periodic sweep is simpler and more robust than trying to hook every creation path.
	DeathScanTimer += DeltaSeconds;
	if (DeathScanTimer >= 1.f)
	{
		DeathScanTimer = 0.f;
		for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
		{
			RegisterUnitForDeathTracking(*It);
		}
	}

	BotScanTimer += DeltaSeconds;
	if (BotScanTimer >= 2.f)
	{
		BotScanTimer = 0.f;
		RefreshBotCount();
		MarkAiPlayerStates();
	}

	switch (GS->MatchPhase)
	{
	case EMassShooterMatchPhase::Warmup:
		if (GS->GetPhaseTimeRemaining() <= 0.f)
		{
			SetMatchPhase(EMassShooterMatchPhase::InProgress);
			RespawnAllPlayers();
		}
		break;

	case EMassShooterMatchPhase::InProgress:
	{
		// Respawn anyone whose timer has elapsed. Polling rather than one timer per player: a
		// player can disconnect or the match can end mid-wait, and a stale timer firing into a
		// finished match is exactly the class of bug that is hard to reproduce.
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get())
			{
				const AMassShooterPlayerState* State = PC->GetPlayerState<AMassShooterPlayerState>();
				if (State && State->bAwaitingRespawn)
				{
					TryRespawnPlayer(PC);
				}
			}
		}

		if (bSpawnBotWaves || MatchMode == EMassShooterMatchMode::Survival)
		{
			WaveTimer += DeltaSeconds;

			// Send the next wave on the clock, or early once the field is nearly clear — waiting
			// out the full timer with two bots left is dead air.
			//
			// The early release is floored by MinSecondsBetweenWaves as a second line of defence:
			// refreshing the count above fixes the case where the bots DID spawn, but a spawn that
			// yields nothing (no spawn table, every SpawnOne failing) would otherwise re-trigger
			// this branch every frame and march the wave counter - and its scaling - upwards
			// without a single bot on the field.
			const bool bFieldNearlyClear = GS->CurrentWave > 0 && GS->BotsAlive <= 2
				&& WaveTimer >= MinSecondsBetweenWaves;
			if (WaveTimer >= SecondsBetweenWaves || GS->CurrentWave == 0 || bFieldNearlyClear)
			{
				WaveTimer = 0.f;
				SpawnNextWave();
			}
		}

		EvaluateEndConditions();
		break;
	}

	case EMassShooterMatchPhase::PostMatch:
		if (bRestartAfterPostMatch && GS->GetPhaseTimeRemaining() <= 0.f)
		{
			RestartMatch();
		}
		break;
	}
}

void AMassShooterGameMode::ApplyTeamAlliances()
{
	if (TeamAlliances.Num() == 0 || !HasAuthority())
	{
		return;
	}

	const UGameInstance* GI = GetGameInstance();
	UPlayerTeamSubsystem* Teams = GI ? GI->GetSubsystem<UPlayerTeamSubsystem>() : nullptr;
	if (!Teams)
	{
		return;
	}

	for (const FMassShooterTeamAlliance& Pair : TeamAlliances)
	{
		if (Pair.TeamA == Pair.TeamB)
		{
			continue;
		}

		// A team id of 64 or more cannot be represented in the int64 mask. Saying so once is worth
		// more than a silently inert alliance.
		if (Pair.TeamA < 0 || Pair.TeamA >= 64 || Pair.TeamB < 0 || Pair.TeamB >= 64)
		{
			UE_LOG(LogMassShooterMatch, Warning,
				TEXT("Alliance %d/%d ignored: team ids must be in 0..63 to fit the allied mask."),
				Pair.TeamA, Pair.TeamB);
			continue;
		}

		Teams->SetTeamsAllied(Pair.TeamA, Pair.TeamB, /*bAllied*/ true);
	}
}
