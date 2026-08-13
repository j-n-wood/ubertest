#ifndef DISRUPTOR_H
#define DISRUPTOR_H

#include "raylib.h"
#include "box2d/box2d.h"
#include <span>

struct UnitInstance;
struct WeaponDefinition;

//------------------------------------------------------------------------------
// Disruptor (area weapon) — omnidirectional line-of-sight area damage, ported from uber's
// shot.cpp area effect. Resolve one blast fired from `firePos` by `firer`: damage every candidate
// in `units` that is not the firer, active, alive, NOT `disruptorShielded`, within
// `weapon.maxRange`, and has a clear wall/CLOSED-door line-of-sight from firePos (other units never
// block the sightline). Damage bypasses armour (`applyDamage(..., ignoreArmour=true)`) and sets
// damageAlert/damageFromDir so survivors react. No team filter — an enemy disruptor hits the player
// and other droids alike. Returns the number of units damaged.
//
// Pure over its inputs (no Game/manager coupling) so it is unit-testable against a bare b2World.
// See game_update_disruptors (windup → blast) and docs/weapons.md.
//------------------------------------------------------------------------------
int disruptorBlast(b2WorldId world, Vector2 firePos, const UnitInstance* firer,
                   const WeaponDefinition& weapon, std::span<UnitInstance* const> units);

#endif // DISRUPTOR_H
