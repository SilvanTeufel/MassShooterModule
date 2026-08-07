// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

using UnrealBuildTool;

public class MassShooterModule : ModuleRules
{
	public MassShooterModule(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",

				// Base products. READ-ONLY: MassShooterModule never modifies RTSUnitTemplate or
				// WeaponModule; it only derives from their classes and calls their public API.
				"RTSUnitTemplate",
				"WeaponModule",

				"GameplayAbilities",
				"GameplayTags",
				"GameplayTasks",

				// Mass. The player pawn owns a Mass entity so RTS bots can perceive it; the bots
				// themselves are plain RTSUnitTemplate Mass units.
				"MassCore",
				"MassEntity",
				"MassCommon",
				"MassMovement",
				"MassNavigation",
				"MassSignals",
				"MassRepresentation",
				"MassSpawner",
				"MassActors",
				"MassReplication",

				"NetCore",
				"EnhancedInput",
				"UMG",
				"Niagara",
				"DeveloperSettings"
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"NavigationSystem",
				"AIModule"
			}
			);
	}
}
