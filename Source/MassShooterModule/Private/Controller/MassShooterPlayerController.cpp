// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Controller/MassShooterPlayerController.h"
#include "Characters/MassShooterCharacter.h"
#include "Components/MassShooterCombatComponent.h"
#include "Components/MassShooterLoadoutComponent.h"
#include "Core/MassShooterGameplayTags.h"
#include "GameModes/MassShooterGameMode.h"
#include "MassShooterLog.h"

// RTSUnitTemplate (read-only use).
#include "Controller/Input/InputConfig.h"
#include "System/PlayerTeamSubsystem.h"
#include "Characters/Unit/UnitBase.h"

// WeaponModule (read-only use).
#include "Components/WeaponHUDComponent.h"
#include "Components/WeaponComponent.h"
#include "UI/WeaponSelectionHUDWidget.h"
#include "Hud/HUDBase.h"

namespace
{
	int32 GLogHud = 0;
	FAutoConsoleVariableRef CVarLogHud(
		TEXT("Shooter.Debug.LogHud"), GLogHud,
		TEXT("1 = log the weapon panel's runtime state once a second."), ECVF_Cheat);
}

#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"  // ViewportClient->GetViewportSize braucht den vollstaendigen Typ
#include "Blueprint/UserWidget.h"
#include "UObject/ConstructorHelpers.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerInput.h"
#include "Engine/LocalPlayer.h"
// Needed for GetGameInstance()->GetSubsystem<>(): the returned pointer is only forward-declared in
// the engine headers this file already pulls in. Missing it compiles fine inside this project's
// unity build and fails in a standalone plugin build.
#include "Engine/GameInstance.h"

AMassShooterPlayerController::AMassShooterPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;

	// AControllerBase's constructor sets bShowMouseCursor = true, DefaultMouseCursor = Crosshairs
	// and both click/mouse-over event flags — correct for an RTS, and the cause of three separate
	// shooter symptoms at once:
	//   * the OS crosshair cursor is visible and follows the mouse,
	//   * the mouse is never captured, so mouse motion never reaches the look input, and
	//   * Slate consumes the mouse-UP, so a click starts firing and the release never arrives
	//     (the weapon then empties its magazine from a single click).
	//
	// Overridden HERE rather than only in BeginPlay: these are read while the viewport is being
	// set up, so fixing them after the fact leaves a window in which the wrong mode is applied.
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
	bEnableTouchEvents = false;
	DefaultMouseCursor = EMouseCursor::None;
	CurrentMouseCursor = EMouseCursor::None;

	// Nulled so AControllerBase::ApplyCustomMouseCursor cannot re-show a software cursor widget.
	MouseCursorWidgetClass = nullptr;

	WeaponHUD = CreateDefaultSubobject<UWeaponHUDComponent>(TEXT("WeaponHUD"));

	// WeaponModule's own weapon panel, so the weapon/ammo/magazine/level UI is there with no
	// setup. Referencing another plugin's content is fine; the plugin itself stays untouched.
	static ConstructorHelpers::FClassFinder<UWeaponSelectionHUDWidget> WeaponPanelBP(
		TEXT("/WeaponModule/WeaponModule/Widget/BP_WeaponSelectionHUDWidget"));
	if (WeaponPanelBP.Succeeded())
	{
		WeaponSelectionWidgetClass = WeaponPanelBP.Class;
	}
}

void AMassShooterPlayerController::BeginPlay()
{
	// NOT Super::BeginPlay(). See the class comment: this deliberately skips AControllerBase,
	// AWidgetController, AExtendedControllerBase and ACustomControllerBase, whose BeginPlay bodies
	// build RTS camera/HUD/minimap state a shooter does not have.
	APlayerController::BeginPlay();

	// The one thing AControllerBase::BeginPlay did that we DO want, stated explicitly.
	if (HasAuthority())
	{
		if (const UGameInstance* GI = GetGameInstance())
		{
			if (UPlayerTeamSubsystem* TeamSubsystem = GI->GetSubsystem<UPlayerTeamSubsystem>())
			{
				AlliedTeamsMask = TeamSubsystem->GetAlliedTeamsMask(SelectableTeamId);
			}
		}
	}

	if (IsLocalController())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			if (ShooterMappingContext)
			{
				Subsystem->AddMappingContext(ShooterMappingContext, MappingPriority);
			}
		}

		// A shooter aims with the camera, not a cursor. Re-asserted here on top of the constructor
		// defaults because PIE can hand the viewport back in UI mode.
		SetShowMouseCursor(false);
		SetInputMode(FInputModeGameOnly());

		SetupWeaponModuleHUD();
	}
}

void AMassShooterPlayerController::AuditWeaponHUD()
{
	// Reports the weapon panel's ACTUAL runtime state rather than its preconditions.
	//
	// Every static check on this passed — the widget class is set, the panel count is 3, the pawn
	// carries a UWeaponComponent, the HUD derives AHUDBase — while the panel stayed invisible. The
	// values below are the ones UWeaponSelectionHUDWidget::UpdateSelection actually reads.
	static IConsoleVariable* CVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("Shooter.Debug.LogHud"));
	if (!CVar || CVar->GetInt() == 0)
	{
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (Now - LastHudAuditTime < 1.f)
	{
		return;
	}
	LastHudAuditTime = Now;

	AHUDBase* HudBase = Cast<AHUDBase>(GetHUD());
	const int32 SelectedCount = HudBase ? HudBase->SelectedUnits.Num() : -1;
	const bool bSelectedHasWeapon = HudBase && HudBase->SelectedUnits.Num() > 0
		&& HudBase->SelectedUnits[0]
		&& HudBase->SelectedUnits[0]->FindComponentByClass<UWeaponComponent>() != nullptr;

	// The panel's own internals, read through the reflection system.
	//
	// WeaponHUDWidgets, WeaponHUDContainer and ControllerBase are protected, and they are exactly
	// the three values that decide whether anything renders. Guessing at them cost two wrong
	// hypotheses already, so they get read rather than inferred. WeaponModule stays untouched —
	// this only looks at UPROPERTYs it already publishes to the engine.
	int32 PanelCount = -1;
	int32 ContainerSet = -1;
	int32 ControllerSet = -1;
	if (WeaponSelectionWidget)
	{
		UClass* WidgetClass = WeaponSelectionWidget->GetClass();
		if (FArrayProperty* Panels = FindFProperty<FArrayProperty>(WidgetClass, TEXT("WeaponHUDWidgets")))
		{
			FScriptArrayHelper Helper(Panels, Panels->ContainerPtrToValuePtr<void>(WeaponSelectionWidget));
			PanelCount = Helper.Num();
		}
		if (FObjectPropertyBase* Container = FindFProperty<FObjectPropertyBase>(WidgetClass, TEXT("WeaponHUDContainer")))
		{
			ContainerSet = Container->GetObjectPropertyValue_InContainer(WeaponSelectionWidget) != nullptr;
		}
		if (FObjectPropertyBase* Ctrl = FindFProperty<FObjectPropertyBase>(WidgetClass, TEXT("ControllerBase")))
		{
			ControllerSet = Ctrl->GetObjectPropertyValue_InContainer(WeaponSelectionWidget) != nullptr;
		}
	}

	// Where the first panel actually ended up. Everything upstream reports healthy, so the
	// remaining possibilities are all geometric: collapsed, zero-sized, or laid out off-screen.
	FString PanelGeom = TEXT("n/a");
	if (WeaponSelectionWidget)
	{
		if (FArrayProperty* Panels = FindFProperty<FArrayProperty>(WeaponSelectionWidget->GetClass(), TEXT("WeaponHUDWidgets")))
		{
			FScriptArrayHelper Helper(Panels, Panels->ContainerPtrToValuePtr<void>(WeaponSelectionWidget));
			if (Helper.Num() > 0)
			{
				if (UWidget* Panel = *reinterpret_cast<UWidget**>(Helper.GetRawPtr(0)))
				{
					const FGeometry& G = Panel->GetCachedGeometry();
					const FVector2D Pos = G.GetAbsolutePosition();
					const FVector2D Size = G.GetLocalSize();
					// Absolute Slate coordinates are desktop-space for a windowed game, so the
					// panel's position only means anything relative to the root widget and the
					// viewport size.
					const FVector2D RootPos = WeaponSelectionWidget->GetCachedGeometry().GetAbsolutePosition();
					FVector2D Viewport = FVector2D::ZeroVector;
					if (GetLocalPlayer() && GetLocalPlayer()->ViewportClient)
					{
						GetLocalPlayer()->ViewportClient->GetViewportSize(Viewport);
					}
					// Root SIZE is the disambiguator: if the root fills the viewport then its
					// absolute position is the viewport origin, and the panel's offset from it is
					// its on-screen position. If it does not, the panel is laid out somewhere the
					// player cannot see.
					const FVector2D RootSize = WeaponSelectionWidget->GetCachedGeometry().GetLocalSize();
					PanelGeom = FString::Printf(
						TEXT("vis=%d size=(%.0f,%.0f) relToRoot=(%.0f,%.0f) rootAbs=(%.0f,%.0f) rootSize=(%.0f,%.0f) viewport=(%.0f,%.0f)"),
						(int32)Panel->GetVisibility(), Size.X, Size.Y,
						Pos.X - RootPos.X, Pos.Y - RootPos.Y,
						RootPos.X, RootPos.Y, RootSize.X, RootSize.Y, Viewport.X, Viewport.Y);
				}
			}
		}
	}

	UE_LOG(LogMassShooter, Log, TEXT("HUD AUDIT panel0: %s"), *PanelGeom);

	// Walk the panel's ancestors and print each one's visibility.
	//
	// ESlateVisibility::Hidden is the one state that reproduces every measurement taken so far: a
	// hidden widget still participates in layout, so its children keep valid non-zero geometry and
	// report Visible, yet nothing in that subtree is drawn. If an ancestor is Hidden, this finds
	// which one.
	if (WeaponSelectionWidget)
	{
		if (FArrayProperty* Panels = FindFProperty<FArrayProperty>(WeaponSelectionWidget->GetClass(), TEXT("WeaponHUDWidgets")))
		{
			FScriptArrayHelper Helper(Panels, Panels->ContainerPtrToValuePtr<void>(WeaponSelectionWidget));
			if (Helper.Num() > 0)
			{
				FString Chain;
				UWidget* Node = *reinterpret_cast<UWidget**>(Helper.GetRawPtr(0));
				int32 Depth = 0;
				while (Node && Depth < 12)
				{
					Chain += FString::Printf(TEXT("%s(vis=%d,op=%.2f) < "),
						*Node->GetName(), (int32)Node->GetVisibility(), Node->GetRenderOpacity());
					Node = Node->GetParent();
					++Depth;
				}
				UE_LOG(LogMassShooter, Log, TEXT("HUD AUDIT chain: %s"), *Chain);
			}
		}
	}

	UE_LOG(LogMassShooter, Log,
		TEXT("HUD AUDIT: widget=%d inViewport=%d vis=%d hudIsAHUDBase=%d selected=%d hasWeapon=%d | panels=%d container=%d ctrl=%d"),
		WeaponSelectionWidget != nullptr,
		WeaponSelectionWidget ? (int32)WeaponSelectionWidget->IsInViewport() : -1,
		WeaponSelectionWidget ? (int32)WeaponSelectionWidget->GetVisibility() : -1,
		HudBase != nullptr,
		SelectedCount,
		(int32)bSelectedHasWeapon,
		PanelCount, ContainerSet, ControllerSet);
}

void AMassShooterPlayerController::SetupWeaponModuleHUD()
{
	if (!WeaponSelectionWidgetClass || WeaponSelectionWidget || !WeaponHUD)
	{
		return;
	}

	WeaponSelectionWidget = CreateWidget<UWeaponSelectionHUDWidget>(this, WeaponSelectionWidgetClass);
	if (!WeaponSelectionWidget)
	{
		UE_LOG(LogMassShooter, Warning, TEXT("Failed to create the WeaponModule selection widget."));
		return;
	}

	WeaponSelectionWidget->AddToViewport();

	// WeaponModule's own entry point. It starts a timer that mirrors AHUDBase::SelectedUnits into
	// the per-weapon panels — which is exactly why AMassShooterHUD derives AHUDBase and keeps the
	// local pawn in that array (see AMassShooterHUD::RefreshWeaponSelection).
	WeaponHUD->RegisterWeaponHUD(WeaponSelectionWidget, this);

	UE_LOG(LogMassShooter, Log, TEXT("WeaponModule HUD registered (%s)."), *WeaponSelectionWidgetClass->GetName());
}

void AMassShooterPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	ShooterCharacter = Cast<AMassShooterCharacter>(InPawn);
	if (InPawn && !ShooterCharacter)
	{
		UE_LOG(LogMassShooter, Warning, TEXT("%s possessed %s, which is not an AMassShooterCharacter."),
			*GetName(), *InPawn->GetName());
	}
}

void AMassShooterPlayerController::ResolveShooterCharacter()
{
	// OnPossess is authority-only. On the owning client the pawn arrives through replication
	// (OnRep_Pawn / AcknowledgePossession) and OnPossess never runs, so caching there alone leaves
	// this null on the client and no input would ever reach the pawn. The cast is cheap and the
	// pawn legitimately changes (respawn, possession handoff), so resolve it from GetPawn().
	if (ShooterCharacter && ShooterCharacter == GetPawn())
	{
		return;
	}
	ShooterCharacter = Cast<AMassShooterCharacter>(GetPawn());
}

void AMassShooterPlayerController::Tick(float DeltaSeconds)
{
	// NOT Super::Tick(). AControllerBase::Tick traces under the cursor every frame with no
	// IsLocalController() gate — see the class comment.
	APlayerController::Tick(DeltaSeconds);

	ResolveShooterCharacter();

	if (!IsLocalController() || !ShooterCharacter)
	{
		return;
	}

	// Legacy fallback movement: polled rather than bound, because W/A/S/D are digital keys and
	// polling gives a correct "released" edge for free. Enhanced Input, when configured, drives
	// the same ApplyMoveInput through Input_Move instead.
	//
	// Look is NOT polled here: GetInputMouseDelta only reports anything while the mouse is
	// captured, which made it silently return zero. Mouse look is bound as a real axis instead —
	// see RegisterLegacyInputMappings.
	if (bUseLegacyInputFallback && !ShooterMappingContext)
	{
		const float Forward = (IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
		const float Right = (IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
		ApplyMoveInput(Forward, Right);
	}

	AuditWeaponHUD();

	// Keep the RTS "mouse world position" pointed at the crosshair.
	//
	// Several RTSUnitTemplate systems read AExtendedControllerBase::ReplicatedMouseLocation as
	// "where this player is pointing" — UGameplayAbilityBase::GetTargetLocation and
	// MassRotateToMouseProcessor among them. It is normally fed by AControllerBase::Tick's cursor
	// trace, which this controller skips. Feeding it the camera aim point keeps those systems
	// working; the throttled setter is the same one the RTS controller uses, so this costs the
	// same handful of RPCs a moving mouse would.
	if (ShooterCharacter && ShooterCharacter->GetCombat())
	{
		const FVector AimPoint = ShooterCharacter->GetCombat()->GetAimPoint();
		UpdateMouseLocationWithThrottling(AimPoint);

		// The sustained rounds of a burst are fired by the ability's own looping timer, and each of
		// them reads ReplicatedMouseLocation at the moment it fires — not the value at activation.
		// So the only way to know whether a held burst really tracks the crosshair is to watch that
		// value between shots. If it ever collapses toward ground height while the aim point is
		// high, something is writing it behind this controller's back.
		static IConsoleVariable* DrawAimCVar =
			IConsoleManager::Get().FindConsoleVariable(TEXT("Shooter.Debug.DrawAim"));
		if (DrawAimCVar && DrawAimCVar->GetInt() != 0)
		{
			const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
			if (Now - LastAimAuditTime >= 1.f)
			{
				LastAimAuditTime = Now;
				UE_LOG(LogMassShooter, Log, TEXT("AIM AUDIT: aimZ=%.0f replicatedMouseZ=%.0f delta=%.0f"),
					AimPoint.Z, ReplicatedMouseLocation.Z, AimPoint.Z - ReplicatedMouseLocation.Z);
			}
		}
	}

	// Trigger safety net. The fire release is a bound edge, and an edge can be lost: Slate can eat
	// a mouse-up, alt-tab drops the key entirely, and a pawn swap mid-burst leaves the old state
	// behind. Any of those means the weapon keeps firing until the magazine runs dry. The physical
	// key state is the ground truth, so reconcile against it.
	if (ShooterCharacter && ShooterCharacter->GetCombat() && ShooterCharacter->GetCombat()->IsFiring()
		&& !UMassShooterCombatComponent::IsTestTriggerActive())
	{
		const bool bFireKeyHeld = IsInputKeyDown(EKeys::LeftMouseButton) || IsInputKeyDown(EKeys::Gamepad_RightTrigger);
		if (!bFireKeyHeld)
		{
			ShooterCharacter->GetCombat()->SetFiring(false);
		}
	}
}

void AMassShooterPlayerController::ApplyMoveInput(float Forward, float Right)
{
	if (!ShooterCharacter || ShooterCharacter->IsDeadShooter())
	{
		return;
	}

	// Move relative to where the player is LOOKING, flattened. Using the control rotation rather
	// than the pawn rotation keeps strafing correct while the body is mid-turn.
	const FRotator YawOnly(0.f, GetControlRotation().Yaw, 0.f);

	if (!FMath::IsNearlyZero(Forward))
	{
		ShooterCharacter->AddMovementInput(FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X), Forward);
	}
	if (!FMath::IsNearlyZero(Right))
	{
		ShooterCharacter->AddMovementInput(FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y), Right);
	}
}

// ---------------------------------------------------------------------------------------------
//  Input setup
// ---------------------------------------------------------------------------------------------

void AMassShooterPlayerController::BindTaggedAction(UEnhancedInputComponent* EIC, const FGameplayTag& Tag,
	ETriggerEvent Event, void (AMassShooterPlayerController::*Func)(const FInputActionValue&))
{
	if (!InputConfig)
	{
		return;
	}

	if (const UInputAction* Action = InputConfig->FindInputActionForTag(Tag))
	{
		EIC->BindAction(Action, Event, this, Func);
	}
	else
	{
		// Loud, because a silently unbound action is indistinguishable from broken hardware.
		UE_LOG(LogMassShooter, Warning, TEXT("InputConfig '%s' has no action for tag '%s'."),
			*InputConfig->GetName(), *Tag.ToString());
	}
}

void AMassShooterPlayerController::SetupInputComponent()
{
	// AControllerBase's override only calls Super, so going through it costs nothing — but for
	// consistency with BeginPlay/Tick we call the base we actually mean.
	APlayerController::SetupInputComponent();

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (InputConfig)
		{
			BindTaggedAction(EIC, MassShooterTags::Input_Move,   ETriggerEvent::Triggered, &AMassShooterPlayerController::Input_Move);
			BindTaggedAction(EIC, MassShooterTags::Input_Look,   ETriggerEvent::Triggered, &AMassShooterPlayerController::Input_Look);

			// Held inputs need both edges: Started sets, Completed clears. Binding only Started
			// means the trigger never comes back up.
			BindTaggedAction(EIC, MassShooterTags::Input_Jump,   ETriggerEvent::Started,   &AMassShooterPlayerController::Input_JumpStarted);
			BindTaggedAction(EIC, MassShooterTags::Input_Jump,   ETriggerEvent::Completed, &AMassShooterPlayerController::Input_JumpCompleted);
			BindTaggedAction(EIC, MassShooterTags::Input_Sprint, ETriggerEvent::Started,   &AMassShooterPlayerController::Input_SprintStarted);
			BindTaggedAction(EIC, MassShooterTags::Input_Sprint, ETriggerEvent::Completed, &AMassShooterPlayerController::Input_SprintCompleted);
			BindTaggedAction(EIC, MassShooterTags::Input_Fire,   ETriggerEvent::Started,   &AMassShooterPlayerController::Input_FireStarted);
			BindTaggedAction(EIC, MassShooterTags::Input_Fire,   ETriggerEvent::Completed, &AMassShooterPlayerController::Input_FireCompleted);
			BindTaggedAction(EIC, MassShooterTags::Input_AimDownSights, ETriggerEvent::Started,   &AMassShooterPlayerController::Input_AimStarted);
			BindTaggedAction(EIC, MassShooterTags::Input_AimDownSights, ETriggerEvent::Completed, &AMassShooterPlayerController::Input_AimCompleted);
			BindTaggedAction(EIC, MassShooterTags::Input_Scoreboard, ETriggerEvent::Started,   &AMassShooterPlayerController::Input_ScoreboardStarted);
			BindTaggedAction(EIC, MassShooterTags::Input_Scoreboard, ETriggerEvent::Completed, &AMassShooterPlayerController::Input_ScoreboardCompleted);

			BindTaggedAction(EIC, MassShooterTags::Input_Crouch,     ETriggerEvent::Started, &AMassShooterPlayerController::Input_CrouchToggle);
			BindTaggedAction(EIC, MassShooterTags::Input_Reload,     ETriggerEvent::Started, &AMassShooterPlayerController::Input_Reload);
			BindTaggedAction(EIC, MassShooterTags::Input_NextWeapon, ETriggerEvent::Started, &AMassShooterPlayerController::Input_NextWeapon);
			BindTaggedAction(EIC, MassShooterTags::Input_PrevWeapon, ETriggerEvent::Started, &AMassShooterPlayerController::Input_PrevWeapon);
			BindTaggedAction(EIC, MassShooterTags::Input_Grenade,    ETriggerEvent::Started, &AMassShooterPlayerController::Input_Grenade);
			BindTaggedAction(EIC, MassShooterTags::Input_Dash,       ETriggerEvent::Started, &AMassShooterPlayerController::Input_Dash);
			BindTaggedAction(EIC, MassShooterTags::Input_ToggleView, ETriggerEvent::Started, &AMassShooterPlayerController::Input_ToggleView);
			BindTaggedAction(EIC, MassShooterTags::Input_Respawn,    ETriggerEvent::Started, &AMassShooterPlayerController::Input_Respawn);
		}
	}

	// Direct key bindings, so the module is playable the moment it is enabled — before anyone has
	// authored InputAction assets. BindKey binds a physical key with no mapping asset in between,
	// which is exactly what a "works out of the box" fallback needs.
	//
	// Gated on the mapping context being ABSENT, matching Tick: once a project supplies real
	// Enhanced Input bindings, leaving these registered too would deliver every press twice.
	if (bUseLegacyInputFallback && !ShooterMappingContext && InputComponent)
	{
		RegisterLegacyInputMappings();
	}
}

void AMassShooterPlayerController::RegisterLegacyInputMappings()
{
	// Mouse look. BindAxisKey binds the raw MouseX/MouseY axes with no mapping asset in between
	// and delivers a per-frame delta, which is what the polling version was trying (and failing)
	// to get out of GetInputMouseDelta.
	InputComponent->BindAxisKey(EKeys::MouseX, this, &AMassShooterPlayerController::Legacy_Turn);
	InputComponent->BindAxisKey(EKeys::MouseY, this, &AMassShooterPlayerController::Legacy_LookUp);

	InputComponent->BindKey(EKeys::LeftMouseButton,  IE_Pressed,  this, &AMassShooterPlayerController::Legacy_FirePressed);
	InputComponent->BindKey(EKeys::LeftMouseButton,  IE_Released, this, &AMassShooterPlayerController::Legacy_FireReleased);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed,  this, &AMassShooterPlayerController::Legacy_AimPressed);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AMassShooterPlayerController::Legacy_AimReleased);

	InputComponent->BindKey(EKeys::LeftShift, IE_Pressed,  this, &AMassShooterPlayerController::Legacy_SprintPressed);
	InputComponent->BindKey(EKeys::LeftShift, IE_Released, this, &AMassShooterPlayerController::Legacy_SprintReleased);

	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed,  this, &AMassShooterPlayerController::Legacy_JumpPressed);
	InputComponent->BindKey(EKeys::SpaceBar, IE_Released, this, &AMassShooterPlayerController::Legacy_JumpReleased);

	InputComponent->BindKey(EKeys::LeftControl, IE_Pressed, this, &AMassShooterPlayerController::Legacy_CrouchToggle);
	InputComponent->BindKey(EKeys::R,           IE_Pressed, this, &AMassShooterPlayerController::Legacy_Reload);
	InputComponent->BindKey(EKeys::G,           IE_Pressed, this, &AMassShooterPlayerController::Legacy_Grenade);
	InputComponent->BindKey(EKeys::Q,           IE_Pressed, this, &AMassShooterPlayerController::Legacy_Dash);
	InputComponent->BindKey(EKeys::V,           IE_Pressed, this, &AMassShooterPlayerController::Legacy_ToggleView);
	InputComponent->BindKey(EKeys::Enter,       IE_Pressed, this, &AMassShooterPlayerController::Legacy_Respawn);

	InputComponent->BindKey(EKeys::MouseScrollUp,   IE_Pressed, this, &AMassShooterPlayerController::Legacy_NextWeapon);
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AMassShooterPlayerController::Legacy_PrevWeapon);

	InputComponent->BindKey(EKeys::Tab, IE_Pressed,  this, &AMassShooterPlayerController::Legacy_ScoreboardPressed);
	InputComponent->BindKey(EKeys::Tab, IE_Released, this, &AMassShooterPlayerController::Legacy_ScoreboardReleased);

	// Ability bar on the number row, matching what the HUD labels each slot. Bound as six explicit
	// one-liners rather than a loop because BindKey needs a distinct member function pointer per
	// binding — a lambda cannot be used here.
	InputComponent->BindKey(EKeys::One,   IE_Pressed, this, &AMassShooterPlayerController::Legacy_Ability1);
	InputComponent->BindKey(EKeys::Two,   IE_Pressed, this, &AMassShooterPlayerController::Legacy_Ability2);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AMassShooterPlayerController::Legacy_Ability3);
	InputComponent->BindKey(EKeys::Four,  IE_Pressed, this, &AMassShooterPlayerController::Legacy_Ability4);
	InputComponent->BindKey(EKeys::Five,  IE_Pressed, this, &AMassShooterPlayerController::Legacy_Ability5);
	InputComponent->BindKey(EKeys::Six,   IE_Pressed, this, &AMassShooterPlayerController::Legacy_Ability6);
}

void AMassShooterPlayerController::ActivateAbilitySlot(int32 Slot)
{
	ResolveShooterCharacter();
	if (ShooterCharacter)
	{
		ShooterCharacter->ActivateAbilitySlot(Slot);
	}
}

void AMassShooterPlayerController::Legacy_Ability1() { ActivateAbilitySlot(0); }
void AMassShooterPlayerController::Legacy_Ability2() { ActivateAbilitySlot(1); }
void AMassShooterPlayerController::Legacy_Ability3() { ActivateAbilitySlot(2); }
void AMassShooterPlayerController::Legacy_Ability4() { ActivateAbilitySlot(3); }
void AMassShooterPlayerController::Legacy_Ability5() { ActivateAbilitySlot(4); }
void AMassShooterPlayerController::Legacy_Ability6() { ActivateAbilitySlot(5); }

// ---------------------------------------------------------------------------------------------
//  Enhanced Input handlers
// ---------------------------------------------------------------------------------------------

void AMassShooterPlayerController::Input_Move(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	ApplyMoveInput(Axis.Y, Axis.X);
}

void AMassShooterPlayerController::Input_Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddYawInput(Axis.X * LookSensitivity);
	AddPitchInput(-Axis.Y * LookSensitivity);
}

void AMassShooterPlayerController::Input_JumpStarted(const FInputActionValue&)    { Legacy_JumpPressed(); }
void AMassShooterPlayerController::Input_JumpCompleted(const FInputActionValue&)  { Legacy_JumpReleased(); }
void AMassShooterPlayerController::Input_SprintStarted(const FInputActionValue&)  { Legacy_SprintPressed(); }
void AMassShooterPlayerController::Input_SprintCompleted(const FInputActionValue&){ Legacy_SprintReleased(); }
void AMassShooterPlayerController::Input_CrouchToggle(const FInputActionValue&)   { Legacy_CrouchToggle(); }
void AMassShooterPlayerController::Input_FireStarted(const FInputActionValue&)    { Legacy_FirePressed(); }
void AMassShooterPlayerController::Input_FireCompleted(const FInputActionValue&)  { Legacy_FireReleased(); }
void AMassShooterPlayerController::Input_AimStarted(const FInputActionValue&)     { Legacy_AimPressed(); }
void AMassShooterPlayerController::Input_AimCompleted(const FInputActionValue&)   { Legacy_AimReleased(); }
void AMassShooterPlayerController::Input_Reload(const FInputActionValue&)         { Legacy_Reload(); }
void AMassShooterPlayerController::Input_NextWeapon(const FInputActionValue&)     { Legacy_NextWeapon(); }
void AMassShooterPlayerController::Input_PrevWeapon(const FInputActionValue&)     { Legacy_PrevWeapon(); }
void AMassShooterPlayerController::Input_Grenade(const FInputActionValue&)        { Legacy_Grenade(); }
void AMassShooterPlayerController::Input_Dash(const FInputActionValue&)           { Legacy_Dash(); }
void AMassShooterPlayerController::Input_ToggleView(const FInputActionValue&)     { Legacy_ToggleView(); }
void AMassShooterPlayerController::Input_ScoreboardStarted(const FInputActionValue&)   { Legacy_ScoreboardPressed(); }
void AMassShooterPlayerController::Input_ScoreboardCompleted(const FInputActionValue&) { Legacy_ScoreboardReleased(); }
void AMassShooterPlayerController::Input_Respawn(const FInputActionValue&)        { Legacy_Respawn(); }

// ---------------------------------------------------------------------------------------------
//  Shared handler bodies (the legacy names are the implementation; Enhanced Input forwards here)
// ---------------------------------------------------------------------------------------------

void AMassShooterPlayerController::Legacy_MoveForward(float Value) { ApplyMoveInput(Value, 0.f); }
void AMassShooterPlayerController::Legacy_MoveRight(float Value)   { ApplyMoveInput(0.f, Value); }
void AMassShooterPlayerController::Legacy_Turn(float Value)        { AddYawInput(Value * LookSensitivity); }
void AMassShooterPlayerController::Legacy_LookUp(float Value)      { AddPitchInput(-Value * LookSensitivity); }

void AMassShooterPlayerController::Legacy_FirePressed()
{
	ResolveShooterCharacter();
	if (ShooterCharacter && ShooterCharacter->GetCombat() && !ShooterCharacter->IsDeadShooter())
	{
		ShooterCharacter->GetCombat()->SetFiring(true);
	}
}

void AMassShooterPlayerController::Legacy_FireReleased()
{
	if (ShooterCharacter && ShooterCharacter->GetCombat())
	{
		ShooterCharacter->GetCombat()->SetFiring(false);
	}
}

void AMassShooterPlayerController::Legacy_AimPressed()
{
	ResolveShooterCharacter();
	if (ShooterCharacter && ShooterCharacter->GetCombat())
	{
		ShooterCharacter->GetCombat()->SetAiming(true);
		ShooterCharacter->RefreshStanceSpeed();
	}
}

void AMassShooterPlayerController::Legacy_AimReleased()
{
	if (ShooterCharacter && ShooterCharacter->GetCombat())
	{
		ShooterCharacter->GetCombat()->SetAiming(false);
		ShooterCharacter->RefreshStanceSpeed();
	}
}

void AMassShooterPlayerController::Legacy_SprintPressed()
{
	ResolveShooterCharacter();
	if (ShooterCharacter)
	{
		ShooterCharacter->SetSprinting(true);
	}
}

void AMassShooterPlayerController::Legacy_SprintReleased()
{
	if (ShooterCharacter)
	{
		ShooterCharacter->SetSprinting(false);
	}
}

void AMassShooterPlayerController::Legacy_JumpPressed()
{
	ResolveShooterCharacter();
	if (ShooterCharacter && !ShooterCharacter->IsDeadShooter())
	{
		ShooterCharacter->Jump();
	}
}

void AMassShooterPlayerController::Legacy_JumpReleased()
{
	if (ShooterCharacter)
	{
		ShooterCharacter->StopJumping();
	}
}

void AMassShooterPlayerController::Legacy_CrouchToggle()
{
	ResolveShooterCharacter();
	if (ShooterCharacter)
	{
		ShooterCharacter->SetCrouching(!ShooterCharacter->bIsCrouched);
	}
}

void AMassShooterPlayerController::Legacy_Reload()
{
	ResolveShooterCharacter();
	if (ShooterCharacter && ShooterCharacter->GetCombat())
	{
		ShooterCharacter->GetCombat()->RequestReload();
	}
}

void AMassShooterPlayerController::Legacy_NextWeapon()
{
	ResolveShooterCharacter();
	if (ShooterCharacter && ShooterCharacter->GetLoadout())
	{
		ShooterCharacter->GetLoadout()->CycleWeapon(1);
	}
}

void AMassShooterPlayerController::Legacy_PrevWeapon()
{
	ResolveShooterCharacter();
	if (ShooterCharacter && ShooterCharacter->GetLoadout())
	{
		ShooterCharacter->GetLoadout()->CycleWeapon(-1);
	}
}

void AMassShooterPlayerController::Legacy_Grenade()
{
	ResolveShooterCharacter();
	if (!ShooterCharacter || ShooterCharacter->IsDeadShooter())
	{
		return;
	}

	// Grenade sits in the fourth ability slot (see AMassShooterCharacter's grant order). The aim
	// point comes from the combat component's camera trace, so the throw lands where the crosshair
	// is rather than at the pawn's feet.
	FHitResult AimHit;
	AimHit.bBlockingHit = true;
	if (ShooterCharacter->GetCombat())
	{
		AimHit.Location = ShooterCharacter->GetCombat()->GetAimPoint();
		AimHit.ImpactPoint = AimHit.Location;
	}
	ShooterCharacter->ActivateAbilityByInputID(EGASAbilityInputID::AbilityFour,
		ShooterCharacter->DefaultAbilities, AimHit, this);
}

void AMassShooterPlayerController::Legacy_Dash()
{
	ResolveShooterCharacter();
	if (!ShooterCharacter || ShooterCharacter->IsDeadShooter())
	{
		return;
	}

	ShooterCharacter->ActivateAbilityByInputID(EGASAbilityInputID::AbilityFive,
		ShooterCharacter->DefaultAbilities, FHitResult(), this);
}

void AMassShooterPlayerController::Legacy_ToggleView()
{
	ResolveShooterCharacter();
	if (ShooterCharacter)
	{
		ShooterCharacter->ToggleViewMode();
	}
}

void AMassShooterPlayerController::Legacy_ScoreboardPressed()  { bScoreboardHeld = true; }
void AMassShooterPlayerController::Legacy_ScoreboardReleased() { bScoreboardHeld = false; }

void AMassShooterPlayerController::ShooterFireAtNearest()
{
	ResolveShooterCharacter();
	if (!ShooterCharacter || !GetWorld())
	{
		return;
	}

	// Nearest LIVE hostile. Deliberately not the crosshair target: a headless run has no camera
	// worth aiming, and the point of this command is to exercise the fire path, not the aim path.
	AUnitBase* Best = nullptr;
	float BestDistance = FLT_MAX;
	for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
	{
		AUnitBase* Unit = *It;
		if (!Unit || Unit == ShooterCharacter || Unit->TeamId == ShooterCharacter->TeamId
			|| Unit->GetUnitState() == UnitData::Dead)
		{
			continue;
		}
		const float Distance = FVector::Dist(Unit->GetActorLocation(), ShooterCharacter->GetActorLocation());
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Unit;
		}
	}

	if (!Best)
	{
		UE_LOG(LogMassShooter, Log, TEXT("ShooterFireAtNearest: no hostile unit found."));
		return;
	}

	UE_LOG(LogMassShooter, Log, TEXT("ShooterFireAtNearest: %s at %.0f uu."), *Best->GetName(), BestDistance);
	ShooterCharacter->DebugFireAt(Best->GetActorLocation());
}

void AMassShooterPlayerController::ShooterHoldFire(int32 bHold)
{
	ResolveShooterCharacter();
	if (ShooterCharacter && ShooterCharacter->GetCombat())
	{
		ShooterCharacter->GetCombat()->SetFiring(bHold != 0);
	}
}

void AMassShooterPlayerController::ShooterDamageSelf(float Amount)
{
	ResolveShooterCharacter();
	if (ShooterCharacter)
	{
		ShooterCharacter->DebugApplyDamage(Amount, ShooterCharacter);
	}
}

void AMassShooterPlayerController::ServerRequestRespawn_Implementation()
{
	if (AMassShooterGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AMassShooterGameMode>() : nullptr)
	{
		// The game mode enforces the delay; a refusal here is silent on purpose, because the HUD
		// already shows the countdown.
		GameMode->TryRespawnPlayer(this);
	}
}

void AMassShooterPlayerController::Legacy_Respawn()
{
	// The game mode respawns on a timer; this is the "skip the wait" key. It is only a request —
	// the server decides, and refuses while the respawn delay has not elapsed.
	if (ShooterCharacter && ShooterCharacter->IsDeadShooter())
	{
		ServerRequestRespawn();
	}
}
