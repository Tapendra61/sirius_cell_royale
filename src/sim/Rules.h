#pragma once

#include "World.h"
#include "core/Events.h"
#include "core/Tuning.h"

#include <vector>

// Mechanics from Phase 4. All functions are pure on (World, Tuning) -> mutations on World
// and appended events. No clocks, no I/O. Iteration order is deterministic; same call
// sequence with the same World state produces byte-identical output.
namespace cr::rules {

// Per-tick physics
void stepCells(World& world, const Tuning& t, float dt);
void stepFood(World& world, const Tuning& t, float dt);
void applySoftBounds(World& world, const Tuning& t);

// Per-tick interactions
void processEating(World& world, const Tuning& t, std::vector<GameEvent>& events);
void processVirusPushes(World& world, const Tuning& t);
void processRecombine(World& world, const Tuning& t);
void respawnFood(World& world, const Tuning& t);

// Triggered by commands
void doSplit(World& world, PlayerId player, const Tuning& t, std::vector<GameEvent>& events);
void doEject(World& world, PlayerId player, const Tuning& t);
void doDash(World& world, PlayerId player, const Tuning& t);

} // namespace cr::rules
