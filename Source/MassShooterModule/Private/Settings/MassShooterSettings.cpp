// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#include "Settings/MassShooterSettings.h"

UMassShooterSettings::UMassShooterSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Mass Shooter Module");
}

const UMassShooterSettings* UMassShooterSettings::Get()
{
	// GetDefault always returns a valid CDO, so callers never have to null-check.
	return GetDefault<UMassShooterSettings>();
}
