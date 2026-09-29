// Copyright 2026 Silvan Teufel / Teufel-Engineering.com All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MassShooterKillCredit.generated.h"

/**
 * Kill credit for units this module does not own.
 *
 * The scoreboard runs off UMassShooterHealthComponent::OnDeath, and that broadcast carries a Killer
 * only if something called NotifyDamageFrom first. For AMassShooterBot and AMassShooterCharacter
 * that happens in their HandleProjectileImpact override. A foreign RTSUnitTemplate unit - a
 * Xenocrypta Skitterling fielded as a wave enemy - has no such override, so it died with
 * "Killer: <unknown>" and the player scored nothing for it.
 *
 * RTSUnitTemplate hands the shooter to the victim exactly once, as an argument to
 * AUnitBase::HandleProjectileImpact, and stores it nowhere afterwards: no last-attacker field on
 * the unit, and ApplyAttackedDetectionBonus writes only a detection radius. The one place the
 * information is offered to anyone else is AProjectile::ImpactEvent, a BlueprintImplementableEvent
 * that UMassProjectileImpactProcessor fires once per hit unit, server-side, with the victim actor
 * and the SHOOTER'S TEAM. That is the hook this library exists to receive.
 *
 * Team rather than actor is enough here and is not a shortcut: the shooter's team uniquely
 * identifies a human player in this mode, because AMassShooterGameMode puts every Survival player
 * on UMassShooterSettings::FirstPlayerTeamId while every AI faction owns a different id. When more
 * than one pawn shares the reported team the credit is dropped rather than guessed - a wrong name
 * in the kill feed is worse than none.
 */
UCLASS()
class MASSSHOOTERMODULE_API UMassShooterKillCredit : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Server: record that a projectile fired by ShooterTeamId hit HitActor.
	 *
	 * Call this from the ImpactEvent of a projectile Blueprint. Safe to call for any actor and any
	 * team - hits on units this module already tracks, or on units with no health component, cost
	 * one failed lookup and nothing else.
	 *
	 * The world comes from HitActor, deliberately, and there is NO WorldContext pin. Unreal fires
	 * ImpactEvent on the projectile's CLASS DEFAULT OBJECT (UMassProjectileImpactProcessor calls
	 * ProjCDO->ImpactEvent), and a CDO has no world - so a hidden `meta = (WorldContext = ...)` pin,
	 * which Unreal auto-fills with `self`, resolved to null and made this function return before it
	 * did anything. Measured: the player fired 207 times and every kill still logged
	 * "Killer: <unknown>". RTSUnitTemplate warns about exactly this in Projectile.cpp
	 * ("ImpactEvent/GroundHit fire on the CDO, so never trust 'self' for the world"). HitActor is a
	 * live actor in the level and is the one argument that always carries a real world.
	 */
	UFUNCTION(BlueprintCallable, Category = "MassShooter|Score")
	static void ReportProjectileHit(AActor* HitActor, int32 ShooterTeamId);
};
