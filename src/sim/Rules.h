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
void respawnViruses(World& world, const Tuning& t);

// Power-up pickups. Collection applies a timed effect to the cell that overlapped.
// Magnet pulls run as a forces step before stepFood so it can integrate the velocity.
void processPickupCollection(World& world, const Tuning& t,
                             std::vector<GameEvent>& events);
void applyMagnetPulls(World& world, const Tuning& t);
void respawnPickups(World& world, const Tuning& t);

// Black holes. One per-tick step that handles all three subphases together:
//   1. Pull: cells in pull-radius (and stamina above the entry floor) are pulled
//      toward the centre by an acceleration scaled by distance.
//   2. Entry: cells touching the inner radius set their `hiding_in` to that hole.
//   3. Stamina + exit: hidden cells drain; player exits when their move target
//      leaves the hole's disc; everyone exits when stamina hits zero. Cells outside
//      refill their stamina if it's not already full.
void processBlackHoles(World& world, const Tuning& t, float dt);

// Stamina floor below which a cell can't be re-pulled (prevents a thrashing loop
// where an auto-ejected cell instantly gets sucked back in).
constexpr float kBlackHoleEntryFloor = 0.20f;

// Triggered by commands
void doSplit(World& world, PlayerId player, const Tuning& t, std::vector<GameEvent>& events);
void doEject(World& world, PlayerId player, const Tuning& t);
void doDash(World& world, PlayerId player, const Tuning& t);
// 4th ability. The largest player cell spends a fraction of its mass to push every
// enemy cell (and food, weaker) within `blast_radius` outward. Emits a BlastEvent
// for client-side feel. Respects min-mass + cooldown.
void doBlast(World& world, PlayerId player, const Tuning& t, std::vector<GameEvent>& events);

// Weighted food-tier roll. Used by both Simulation::ctor (initial pile) and respawnFood().
// Returns one of {1, 3, 6, 12} -- common / uncommon / rare / epic. Consumes one rng.nextFloat().
float rollFoodMass(Rng& rng);

} // namespace cr::rules
