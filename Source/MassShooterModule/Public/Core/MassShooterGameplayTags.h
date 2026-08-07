// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Input tags, resolved to UInputAction assets through RTSUnitTemplate's UInputConfig
 * (a tag -> UInputAction map with FindInputActionForTag). Reusing that asset type rather than
 * inventing a parallel one is exactly the read-only reuse this module is allowed.
 *
 * Native tags are declared here and defined in the .cpp with UE_DEFINE_GAMEPLAY_TAG, which
 * registers them during module load — no dependency on the project's AssetManager settings, and
 * no manual DoneAddingNativeTags call (that must never be issued from an add callback).
 */
namespace MassShooterTags
{
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Move);
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Look);
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Jump);
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Sprint);
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Crouch);
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Fire);
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_AimDownSights);
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Reload);
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_NextWeapon);
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_PrevWeapon);
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Grenade);
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Dash);
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_ToggleView);
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Scoreboard);
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Interact);
	MASSSHOOTERMODULE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Respawn);
}
