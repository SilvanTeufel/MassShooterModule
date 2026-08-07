// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Characters/MassShooterCharacter.h"
#include "Characters/MassShooterMovementComponent.h"
#include "Components/MassShooterCombatComponent.h"
#include "Components/MassShooterLoadoutComponent.h"
#include "Components/MassShooterHealthComponent.h"
#include "Abilities/MassShooterGrenadeAbility.h"
#include "Abilities/MassShooterDashAbility.h"
#include "Hud/MassShooterHUD.h"
#include "Settings/MassShooterSettings.h"
#include "MassShooterLog.h"

// RTSUnitTemplate (read-only use).
#include "Mass/MassActorBindingComponent.h"
#include "Mass/UnitMassTag.h"
#include "Controller/PlayerController/ExtendedControllerBase.h"
#include "GAS/AttributeSetBase.h"
#include "GAS/GameplayAbilityBase.h"
#include "System/PlayerTeamSubsystem.h"

// WeaponModule (read-only use).
#include "Components/WeaponComponent.h"

#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "MassEntityManager.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
// GetGameInstance() returns a forward-declared UGameInstance*; calling GetSubsystem on it needs the
// full type. In this project's build the unity blob happened to supply it, so the omission only
// surfaced in a standalone BuildPlugin compile — which is the compile Fab runs.
#include "Engine/GameInstance.h"

AMassShooterCharacter::AMassShooterCharacter(const FObjectInitializer& ObjectInitializer)
	// Swap the mover in at construction. AUnitBase forwards the FObjectInitializer, so this
	// reaches ACharacter's subobject before it is created.
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UMassShooterMovementComponent>(
			ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	// AUnitBase ships TickInterval = 0.25 s — plenty for an RTS unit whose motion comes from Mass,
	// far too coarse for a pawn whose camera and trigger live in Tick.
	PrimaryActorTick.TickInterval = 0.f;
	TickInterval = 0.f;

	// AUnitBase leaves NetUpdateFrequency at the RTS default of 2 Hz because unit positions travel
	// through the Mass bubble, not through actor replication. This pawn's position DOES travel
	// through actor replication (CharacterMovement), so 2 Hz would mean everyone else sees a
	// slideshow.
	SetNetUpdateFrequency(60.f);
	SetMinNetUpdateFrequency(30.f);

	// ---- Camera rig --------------------------------------------------------------------------
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(GetCapsuleComponent());
	SpringArm->TargetArmLength = ThirdPersonArmLength;
	SpringArm->SocketOffset = ThirdPersonSocketOffset;
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bDoCollisionTest = true;
	SpringArm->SetRelativeLocation(FVector(0.f, 0.f, 60.f));

	ThirdPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ThirdPersonCamera"));
	ThirdPersonCamera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	ThirdPersonCamera->bUsePawnControlRotation = false;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(10.f, 0.f, FirstPersonEyeHeight));
	FirstPersonCamera->bUsePawnControlRotation = true;

	// ---- Muzzle ------------------------------------------------------------------------------
	ProjectileSpawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ProjectileSpawnPoint"));
	ProjectileSpawnPoint->SetupAttachment(GetCapsuleComponent());
	ProjectileSpawnPoint->SetRelativeLocation(FVector(60.f, 0.f, 40.f));
	ProjectileSpawnPoint->ComponentTags.Add(FName(TEXT("ProjectileSpawn")));

	// ---- Combat ------------------------------------------------------------------------------
	WeaponComp = CreateDefaultSubobject<UWeaponComponent>(TEXT("WeaponComp"));

	// Starting weapon data from WeaponModule's own content. Referencing another plugin's content
	// is fine; the plugin itself stays untouched. UWeaponComponent::LoadDataFromTables() turns
	// this into AvailableWeapons and seeds ammo during its BeginPlay.
	static ConstructorHelpers::FObjectFinder<UDataTable> WeaponDT(
		TEXT("/WeaponModule/WeaponModule/DataTables/DT_WeaponData_Start.DT_WeaponData_Start"));
	if (WeaponDT.Succeeded())
	{
		WeaponComp->WeaponDataTable = WeaponDT.Object;
	}

	Combat = CreateDefaultSubobject<UMassShooterCombatComponent>(TEXT("Combat"));
	Loadout = CreateDefaultSubobject<UMassShooterLoadoutComponent>(TEXT("Loadout"));
	ShooterHealth = CreateDefaultSubobject<UMassShooterHealthComponent>(TEXT("ShooterHealth"));

	// Weapon abilities, in the slot order WeaponModule's reference unit uses:
	// [0] Shoot, [1] Reload, [2] SwitchWeapon.
	//
	// The order is load-bearing, not cosmetic: the ShootAbility graph reloads by activating slot 1
	// (AbilityTwo), so a Reload anywhere else means the weapon empties and never refills.
	// AAbilityUnit grants OffensiveAbilities at startup and does NOT grant DefaultAbilities, so
	// each ability is listed in both: DefaultAbilities is what ActivateAbilityByInputID indexes,
	// OffensiveAbilities is what actually hands them to the ASC.
	static ConstructorHelpers::FClassFinder<UGameplayAbilityBase> ShootAbilityBP(
		TEXT("/WeaponModule/WeaponModule/Abilities/BP_ShootAbility"));
	static ConstructorHelpers::FClassFinder<UGameplayAbilityBase> ReloadAbilityBP(
		TEXT("/WeaponModule/WeaponModule/Abilities/Reload/BP_ReloadAbility"));
	static ConstructorHelpers::FClassFinder<UGameplayAbilityBase> SwitchWeaponAbilityBP(
		TEXT("/WeaponModule/WeaponModule/Abilities/BP_SwitchWeaponAbility"));
	if (ShootAbilityBP.Succeeded() && ReloadAbilityBP.Succeeded() && SwitchWeaponAbilityBP.Succeeded())
	{
		DefaultAbilities.Add(ShootAbilityBP.Class);
		DefaultAbilities.Add(ReloadAbilityBP.Class);
		DefaultAbilities.Add(SwitchWeaponAbilityBP.Class);

		OffensiveAbilities.Add(ShootAbilityBP.Class);
		OffensiveAbilities.Add(ReloadAbilityBP.Class);
		OffensiveAbilities.Add(SwitchWeaponAbilityBP.Class);
	}
	else
	{
		// Keep the slot indices stable even if WeaponModule's content is missing, so grenade and
		// dash below still land on AbilityFour/AbilityFive instead of silently shifting down onto
		// the fire button.
		DefaultAbilities.SetNum(3);
		UE_LOG(LogMassShooter, Warning,
			TEXT("WeaponModule ability Blueprints not found — %s will spawn unarmed."), *GetName());
	}

	// [3] Grenade (G), [4] Dash (Q). Native C++ abilities, so they need no content to exist; the
	// grenade still needs an EffectAreaClass assigned on the Blueprint to actually explode.
	DefaultAbilities.Add(UMassShooterGrenadeAbility::StaticClass());
	DefaultAbilities.Add(UMassShooterDashAbility::StaticClass());

	// AAbilityUnit grants OffensiveAbilities at startup — this is what puts them on the ASC.
	OffensiveAbilities.Add(UMassShooterGrenadeAbility::StaticClass());
	OffensiveAbilities.Add(UMassShooterDashAbility::StaticClass());

	// ---- Pawn behaviour ----------------------------------------------------------------------
	// A shooter's body faces where the player looks; the RTS default (face your movement) would
	// make strafing look like a car.
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = false;
	}

	// Standard mannequin fit: dropped to the capsule bottom, yawed -90 to face +X.
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -88.f), FRotator(0.f, -90.f, 0.f));
	}

	// This pawn is a person, not an RTS squad member: skip the ISM/selection presentation.
	IsPlayer = true;
	CanBeSelected = false;
	DestroyAfterDeath = false;

	// The RTS attack pipeline must never fire for a player pawn. Dropping the detection tag (see
	// bRtsAutoAttackSuppressed) stops the pawn ACQUIRING targets, but this flag is the one the
	// attack state processors actually gate on — it is synced into FMassAIStateFragment::CanAttack
	// by RTSUnitTemplate's own actor-to-fragment sync, so setting it here is sufficient and needs
	// no per-frame maintenance.
	//
	// It does not touch WeaponModule: firing is a GameplayAbility activation, an entirely separate
	// path from the RTS attack state machine. Without this the pawn kills anything that walks into
	// reach while the player does nothing — which is what a headless run caught.
	CanAttack = false;

	// NOTE: the pawn must stay DETECTABLE. Anything that adds FMassStopUnitDetectionTag (notably
	// AMassUnitBase::EditUnitDetection(false)) removes it from every bot's target search, so the
	// player becomes unshootable. CanAttack alone stops it attacking without hiding it.
}

void AMassShooterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMassShooterCharacter, bShooterDead);
	DOREPLIFETIME(AMassShooterCharacter, bSprinting);
}

void AMassShooterCharacter::BeginPlay()
{
	Super::BeginPlay();

	ShooterMovement = Cast<UMassShooterMovementComponent>(GetCharacterMovement());

	// AUnitBase::BeginPlay calls SetReplicateMovement(false) — correct for an RTS unit whose
	// transform travels through the Mass bubble, wrong for a CharacterMovement pawn, whose
	// simulated proxies get nothing without it.
	SetReplicateMovement(true);

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		// State it explicitly rather than trusting an inherited default: RTS units are spawned
		// standing still on purpose, and one stray SetMovementMode elsewhere is all it takes.
		Move->SetMovementMode(MOVE_Walking);
	}

	// RTSUnitTemplate creates a unit's Mass entity from SetupMassOnActor, which is BlueprintCallable
	// with no C++ caller — RTS units invoke it from their unit Blueprint's BeginPlay. Calling it
	// here means a player pawn works even as a bare C++ class, with no Blueprint to forget.
	if (MassActorBindingComponent)
	{
		MassActorBindingComponent->SetupMassOnActor();
	}
	else
	{
		UE_LOG(LogMassShooter, Error, TEXT("%s has no MassActorBindingComponent — bots will never see it."), *GetName());
	}

	// APerformanceUnit hides the skeletal mesh at spawn because RTS units render as pooled ISM
	// instances. This pawn renders as a real skeletal mesh (it is metres from the camera) and,
	// having dropped FUnitMassTag, no ISM system drives it either — so without this it has no
	// visible body at all.
	SetBodyVisibleForShooter(true);

	if (ShooterHealth)
	{
		ShooterHealth->OnDeath.AddDynamic(this, &AMassShooterCharacter::HandleDeath);
	}

	bFirstPerson = bStartFirstPerson;
	SetFirstPerson(bFirstPerson);
	RefreshStanceSpeed();

	if (HasAuthority())
	{
		SetShooterTeam(TeamId);
	}
}

void AMassShooterCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// The health component owns the stat block and re-applies it over a bounded startup window
	// (RTSUnitTemplate's own attribute init runs later and would otherwise clobber an early write).
	if (HasAuthority() && ShooterHealth)
	{
		ShooterHealth->ApplyDefaultStats();
		ShooterHealth->GrantSpawnProtection(UMassShooterSettings::Get()->SpawnProtectionSeconds);
	}
}

void AMassShooterCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The camera rig follows the designer-facing values so they can be tuned live in PIE.
	if (SpringArm)
	{
		SpringArm->TargetArmLength = ThirdPersonArmLength;
		SpringArm->SocketOffset = ThirdPersonSocketOffset;
	}

	// Clear whatever target the RTS acquired before CanAttack took effect, once, as soon as the
	// entity exists.
	//
	// This deliberately does NOT call EditUnitDetection(false). That function's parameter is named
	// IsDetectable, and false makes the unit UNDETECTABLE: it adds FMassStopUnitDetectionTag, which
	// UDetectionProcessor uses to skip an entity as a TARGET (DetectionProcessor.cpp:160). Using it
	// to stop the pawn attacking also stopped every bot from ever seeing the player — which is
	// exactly why incoming fire did no damage. CanAttack = false (set in the constructor) is the
	// one-directional lever and is sufficient on its own.
	if (HasAuthority() && !bRtsAutoAttackSuppressed)
	{
		FMassEntityManager* EntityManager = nullptr;
		FMassEntityHandle Entity;
		if (GetMassEntityData(EntityManager, Entity) && EntityManager && Entity.IsSet()
			&& EntityManager->IsEntityValid(Entity))
		{
			ResetTarget();
			bRtsAutoAttackSuppressed = true;

			UE_LOG(LogMassShooter, Log,
				TEXT("%s: RTS target cleared; firing is player-driven, and the pawn stays detectable."), *GetName());
		}
	}
}

void AMassShooterCharacter::SetBodyVisibleForShooter(bool bVisible)
{
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetHiddenInGame(!bVisible, true);
		MeshComp->SetVisibility(bVisible, true);
		MeshComp->SetComponentTickEnabled(bVisible);
	}
}

// ---------------------------------------------------------------------------------------------
//  Stance / view
// ---------------------------------------------------------------------------------------------

void AMassShooterCharacter::SetSprinting(bool bNewSprinting)
{
	if (bSprinting == bNewSprinting)
	{
		return;
	}

	bSprinting = bNewSprinting;
	RefreshStanceSpeed();

	if (!HasAuthority())
	{
		Server_SetSprinting(bNewSprinting);
	}
}

void AMassShooterCharacter::Server_SetSprinting_Implementation(bool bNewSprinting)
{
	bSprinting = bNewSprinting;
	RefreshStanceSpeed();
}

void AMassShooterCharacter::SetCrouching(bool bNewCrouching)
{
	// ACharacter's own crouch is already replicated and rolled back with the movement, so there is
	// nothing to reimplement here.
	if (bNewCrouching)
	{
		Crouch();
	}
	else
	{
		UnCrouch();
	}
}

void AMassShooterCharacter::RefreshStanceSpeed()
{
	if (ShooterMovement)
	{
		ShooterMovement->ApplyStanceSpeed(bSprinting, Combat && Combat->IsAiming());
	}
}

void AMassShooterCharacter::SetFirstPerson(bool bNewFirstPerson)
{
	bFirstPerson = bNewFirstPerson;

	if (FirstPersonCamera)
	{
		FirstPersonCamera->SetActive(bFirstPerson);
		FirstPersonCamera->SetRelativeLocation(FVector(10.f, 0.f, FirstPersonEyeHeight));
	}
	if (ThirdPersonCamera)
	{
		ThirdPersonCamera->SetActive(!bFirstPerson);
	}

	// In first person the player is inside their own mesh, so hide it from the owning view only.
	// bOwnerNoSee keeps the body fully visible to everyone else — no ghost players.
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetOwnerNoSee(bFirstPerson);
	}
}

void AMassShooterCharacter::ToggleViewMode()
{
	SetFirstPerson(!bFirstPerson);
}

// ---------------------------------------------------------------------------------------------
//  Team
// ---------------------------------------------------------------------------------------------

void AMassShooterCharacter::SetShooterTeam(int32 NewTeamId)
{
	TeamId = NewTeamId;

	if (!HasAuthority())
	{
		return;
	}

	// The alliance mask is what the Mass perception queries actually read. AUnitBase::BeginPlay
	// computes it once at spawn; a team assigned afterwards (which is the normal case for a
	// shooter, where the game mode balances teams at login) needs it recomputed.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UPlayerTeamSubsystem* TeamSubsystem = GI->GetSubsystem<UPlayerTeamSubsystem>())
		{
			AlliedTeamsMask = TeamSubsystem->GetAlliedTeamsMask(TeamId);
		}
	}
}

void AMassShooterCharacter::ActivateAbilitySlot(int32 Slot)
{
	if (!DefaultAbilities.IsValidIndex(Slot) || bShooterDead)
	{
		return;
	}

	// The aim point travels with the request: only the owning client knows where its camera is
	// looking, and abilities like the grenade need a world target rather than the pawn's feet.
	const FVector AimPoint = Combat ? Combat->GetAimPoint() : GetActorLocation() + GetActorForwardVector() * 1000.f;

	if (HasAuthority())
	{
		Server_ActivateAbilitySlot_Implementation(Slot, AimPoint);
	}
	else
	{
		Server_ActivateAbilitySlot(Slot, AimPoint);
	}
}

void AMassShooterCharacter::Server_ActivateAbilitySlot_Implementation(int32 Slot, FVector_NetQuantize AimLocation)
{
	if (!DefaultAbilities.IsValidIndex(Slot) || bShooterDead)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());

	// Abilities aim with AExtendedControllerBase::ReplicatedMouseLocation — see the long note in
	// UMassShooterCombatComponent::ExecuteFire. Without this a thrown grenade lands wherever that
	// stale value points instead of at the crosshair.
	if (AExtendedControllerBase* ExtPC = Cast<AExtendedControllerBase>(PC))
	{
		ExtPC->ReplicatedMouseLocation = AimLocation;
	}

	// Same synthesized blocking hit the fire path uses: AGASUnit::FireMouseHitAbility only writes
	// the ability's target location when the hit is a valid blocking hit.
	FHitResult AimHit;
	AimHit.bBlockingHit = true;
	AimHit.Location = AimLocation;
	AimHit.ImpactPoint = AimLocation;

	const EGASAbilityInputID InputID =
		static_cast<EGASAbilityInputID>(static_cast<int32>(EGASAbilityInputID::AbilityOne) + Slot);

	ActivateAbilityByInputID(InputID, DefaultAbilities, AimHit, PC);
}

void AMassShooterCharacter::Multicast_Launch_Implementation(FVector LaunchVelocity, bool bOverrideXY, bool bOverrideZ)
{
	if (bShooterDead)
	{
		return;
	}

	LaunchCharacter(LaunchVelocity, bOverrideXY, bOverrideZ);
}

// ---------------------------------------------------------------------------------------------
//  Damage / death / respawn
// ---------------------------------------------------------------------------------------------

void AMassShooterCharacter::HandleProjectileImpact_Implementation(AActor* Shooter, const FVector& ImpactLocation,
	TSubclassOf<AProjectile> ProjectileClass, float DamageOverride,
	TSubclassOf<UGameplayEffect> ProjectileEffect,
	TSubclassOf<UGameplayEffect> ProjectileEffect2,
	TSubclassOf<UGameplayEffect> ProjectileEffect3)
{
	// Record who shot us BEFORE the damage lands, so if this hit is lethal the credit is already
	// in place when the health component notices the death on its next poll.
	if (ShooterHealth)
	{
		ShooterHealth->NotifyDamageFrom(Shooter);
	}

	const bool bLogDamage = UMassShooterHealthComponent::IsHitLoggingEnabled() && Attributes;
	const float PreHealth = bLogDamage ? Attributes->GetHealth() : 0.f;
	const float PreShield = bLogDamage ? Attributes->GetShield() : 0.f;

	const float EffectiveDamage = ResolveImpactDamage(Shooter, DamageOverride, bTreatZeroDamageAsAttackDamage);

	// Chain: the base plugin applies the damage, using the resolved value.
	Super::HandleProjectileImpact_Implementation(Shooter, ImpactLocation, ProjectileClass, EffectiveDamage,
		ProjectileEffect, ProjectileEffect2, ProjectileEffect3);

	if (bLogDamage)
	{
		// Brackets the base plugin's damage application. AUnitBase::HandleProjectileImpact bails
		// out early when ProjectileClass (or its CDO) is null, so "class=None" here means no damage
		// was even attempted — a very different problem from damage being computed as zero.
		UE_LOG(LogMassShooter, Log,
			TEXT("DMG: %s class=%s override=%.1f->%.1f | health %.1f->%.1f shield %.1f->%.1f"),
			*GetName(), *GetNameSafe(ProjectileClass), DamageOverride, EffectiveDamage,
			PreHealth, Attributes->GetHealth(), PreShield, Attributes->GetShield());
	}

	// Read health AFTER Super so "lethal" reflects this hit rather than the previous one.
	const bool bLethal = Attributes && Attributes->GetMaxHealth() > 0.f && Attributes->GetHealth() <= 0.f;
	ReportHitToHUDs(this, Shooter, bLethal);
}

float AMassShooterCharacter::ResolveImpactDamage(const AActor* Shooter, float DamageOverride, bool bEnabled)
{
	// Anything the shooter actually declared is respected untouched.
	if (!bEnabled || !FMath::IsNearlyZero(DamageOverride))
	{
		return DamageOverride;
	}

	if (const AUnitBase* ShooterUnit = Cast<AUnitBase>(Shooter))
	{
		if (ShooterUnit->Attributes && ShooterUnit->Attributes->GetAttackDamage() > 0.f)
		{
			return ShooterUnit->Attributes->GetAttackDamage();
		}
	}

	// Negative hands the decision back to the projectile CDO, which is the base plugin's own
	// "no override" contract — strictly better than applying a literal zero.
	return -1.f;
}

void AMassShooterCharacter::ReportHitToHUDs(AActor* Victim, AActor* Shooter, bool bLethal)
{
	if (!Victim || !Victim->HasAuthority() || !Shooter || Shooter == Victim)
	{
		return;
	}

	// Tell the shooter their round landed.
	if (AMassShooterCharacter* ShooterPawn = Cast<AMassShooterCharacter>(Shooter))
	{
		ShooterPawn->Client_NotifyHitConfirmed(bLethal);
	}

	// Tell the victim which way to look. Sent from the shooter's position, not the impact point:
	// the impact is on the victim's own body and would point at nothing useful.
	if (AMassShooterCharacter* VictimPawn = Cast<AMassShooterCharacter>(Victim))
	{
		VictimPawn->Client_NotifyDamageFrom(Shooter->GetActorLocation());
	}
}

void AMassShooterCharacter::Client_NotifyHitConfirmed_Implementation(bool bLethal)
{
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (AMassShooterHUD* HUD = Cast<AMassShooterHUD>(PC->GetHUD()))
		{
			HUD->ShowHitMarker(bLethal);
		}
	}
}

void AMassShooterCharacter::Client_NotifyDamageFrom_Implementation(FVector_NetQuantize SourceLocation)
{
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (AMassShooterHUD* HUD = Cast<AMassShooterHUD>(PC->GetHUD()))
		{
			HUD->ShowDamageDirection(SourceLocation);
		}
	}
}

void AMassShooterCharacter::HandleDeath(AActor* /*Victim*/, AActor* /*Killer*/)
{
	// The game mode listens to the same delegate for scoring and schedules the respawn; this
	// override only handles the pawn's own presentation.
	EnterDeadState();
}

void AMassShooterCharacter::EnterDeadState()
{
	if (!HasAuthority() || bShooterDead)
	{
		return;
	}

	bShooterDead = true;

	if (Combat)
	{
		Combat->SetFiring(false);
	}

	// Stop dead where you fell rather than sliding to a halt.
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}

	SetActorEnableCollision(false);
	SetBodyVisibleForShooter(false);

	OnRep_ShooterDead();
}

void AMassShooterCharacter::OnRep_ShooterDead()
{
	// Runs on every machine. The authority path above already did the server-side half; this makes
	// remote clients agree about the corpse.
	SetBodyVisibleForShooter(!bShooterDead);
}

void AMassShooterCharacter::RespawnAt(const FTransform& Where)
{
	if (!HasAuthority())
	{
		return;
	}

	// RTSUnitTemplate has already reacted to Health <= 0: PostGameplayEffectExecute called
	// SwitchEntityTagByState(Dead) and DeadEffectsAndEvents(), so the Mass entity carries
	// FMassStateDeadTag and UDeathStateProcessor is counting down to despawn it. Undo that and
	// revive the SAME entity rather than letting it die and building a new one — a fresh entity
	// costs the full Mass creation delay, during which the player is invisible to every bot.
	//
	// Every call below is RTSUnitTemplate's own public API.
	SwitchEntityTagByState(UnitData::Idle, UnitData::Idle);
	{
		FMassEntityManager* EntityManager = nullptr;
		FMassEntityHandle Entity;
		if (GetMassEntityData(EntityManager, Entity) && EntityManager && Entity.IsSet()
			&& EntityManager->IsEntityValid(Entity))
		{
			// SwitchEntityTagByState adds the Idle tag but does NOT remove Dead (that line is
			// commented out in MassUnitBase.cpp), so without this explicit removal the entity keeps
			// looking dead to the death processor and gets despawned anyway.
			EntityManager->Defer().RemoveTag<FMassStateDeadTag>(Entity);
		}
	}

	SetUnitState(UnitData::Idle);

	// Gate flag for DeadEffectsAndEvents: without clearing it a LATER death would be swallowed as
	// "already executed" and play no death effects at all.
	DeadEffectsExecuted = false;

	SetActorEnableCollision(true);
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	SetActorLocationAndRotation(Where.GetLocation(), Where.Rotator(), false, nullptr, ETeleportType::TeleportPhysics);

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->SetMovementMode(MOVE_Walking);
		Move->StopMovementImmediately();
	}

	if (ShooterHealth)
	{
		ShooterHealth->ResetForRespawn();
		ShooterHealth->GrantSpawnProtection(UMassShooterSettings::Get()->SpawnProtectionSeconds);

		// Push the restored health into the Mass entity immediately; the actor-to-fragment sync
		// would get there on its own, but a bot querying in between would see a 0-health target.
		UpdateEntityHealth(ShooterHealth->DefaultMaxHealth, ShooterHealth->GetMaxShieldValue());
	}

	if (Loadout)
	{
		Loadout->ResetLoadout();
	}

	// Drop whatever was locked onto the corpse.
	ResetTarget();

	bShooterDead = false;
	SetBodyVisibleForShooter(true);
	OnRep_ShooterDead();

	UE_LOG(LogMassShooterMatch, Log, TEXT("%s respawned at %s."), *GetName(), *Where.GetLocation().ToCompactString());
}

// ---------------------------------------------------------------------------------------------
//  Debug / test hooks
// ---------------------------------------------------------------------------------------------

void AMassShooterCharacter::DebugFireAt(FVector AimLocation)
{
	if (HasAuthority() && Combat)
	{
		Combat->FireAt(AimLocation);
	}
}

void AMassShooterCharacter::DebugApplyDamage(float Amount, AActor* FromInstigator)
{
	if (!HasAuthority() || !AbilitySystemComponent || !Attributes)
	{
		return;
	}

	if (ShooterHealth && FromInstigator)
	{
		ShooterHealth->NotifyDamageFrom(FromInstigator);
	}

	// Build the same instant EffectDamage effect a real hit uses, so this exercises
	// UAttributeSetBase::PostGameplayEffectExecute (the real death path) rather than the SetHealth
	// funnel, which force-kills and would prove nothing.
	UGameplayEffect* Effect = NewObject<UGameplayEffect>(GetTransientPackage(), FName(TEXT("MassShooterDebugDamageGE")));
	Effect->DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayModifierInfo Mod;
	Mod.Attribute = UAttributeSetBase::GetEffectDamageAttribute();
	Mod.ModifierOp = EGameplayModOp::Additive;
	Mod.ModifierMagnitude = FScalableFloat(-FMath::Abs(Amount)); // damage is NEGATIVE EffectDamage
	Effect->Modifiers.Add(Mod);

	AbilitySystemComponent->ApplyGameplayEffectToSelf(Effect, 1.f, AbilitySystemComponent->MakeEffectContext());
}
