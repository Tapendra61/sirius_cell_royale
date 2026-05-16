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
// Merges same-owner cells whose recombine cooldowns have elapsed and that
// are touching. Fires a RecombineEvent into `events` for each merge so the
// client can play feel + audio + particles.
void processRecombine(World& world, const Tuning& t, std::vector<GameEvent>& events);
// Pushes overlapping same-owner cells apart while their recombine cooldown is
// still active. Without this, all pieces of a split player seek the same
// cursor and stack on top of each other; with it they keep just-touching
// contact along their circumferences (and merge naturally once
// `recombine_at` expires via processRecombine). Standard Agar.io behaviour.
void resolveOwnerOverlaps(World& world);
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

// Periodic crashing-comet world event. Spawns based on internal cadence (see
// Tuning::comet_event_interval_sec). The function:
//   1. Steps existing comets forward and despawns any that left the world.
//   2. Applies kill-on-touch to cells inside an active comet's radius. The cell is
//      marked dead via DeathEvent with `predator_player = INVALID_PLAYER` so the
//      killfeed can label it "COMET" instead of looking up a player.
//   3. Spawns a new comet when the cadence timer fires.
// Caller supplies `next_spawn_tick_inout` (-1 sentinel = "not initialised yet") so the
// schedule is owned by Simulation, not Rules (keeps Rules stateless).
void processComets(World& world, const Tuning& t, float dt,
                   Tick& next_spawn_tick_inout,
                   std::vector<GameEvent>& events);

// Comet shower world event: spawns a "main" Orange comet at the existing
// single-comet size band + 4..9 satellites scattered around its path in Red
// and Blue variants. All comets share the same world-edge entry vector (same
// velocity direction) but with perpendicular + longitudinal jitter so the
// formation reads as a swarm rather than a single wall.
//
// The shower runs on its own cadence (next_spawn_tick_inout) -- independent
// of the single-comet schedule, so the two events can overlap. Once the
// shower is spawned, the individual comets are handled by processComets()
// (motion, kill checks, despawn) -- this function ONLY emits new comets on
// the cadence; it doesn't tick existing ones. Keeps state ownership clean.
void spawnCometShowerNow(World& world, const Tuning& t,
                         std::vector<GameEvent>& events);

// Cadence + jitter; calls spawnCometShowerNow() when the schedule fires.
// Caller supplies `next_spawn_tick_inout` so the schedule is owned by
// Simulation, not Rules.
void processCometShowers(World& world, const Tuning& t,
                         Tick& next_spawn_tick_inout,
                         std::vector<GameEvent>& events);

// Static directional flow fields. For each cell inside a current's radius,
// bias velocity in the current's direction by `strength * dt / massFactor`
// where massFactor grows with cell mass so small cells are swept more
// strongly than mega-cells. Stateless; safe to call every tick. Runs before
// stepCells so the bias is integrated into position alongside the cell's own
// velocity (no double-step).
void applyTidalCurrents(World& world, const Tuning& t, float dt);

// Wormhole teleports. For each cell whose centre falls inside a wormhole's
// radius (and whose per-cell cooldown has elapsed), warp it to the partner
// endpoint with momentum preserved. Sets the cooldown so the cell doesn't
// immediately re-enter the partner's radius and pong back. Hiding cells are
// skipped (they're already pinned inside a black hole).
void processWormholes(World& world, const Tuning& t);

// Periodic geyser eruptions. Each geyser cycles Idle -> Telegraph -> Erupt.
// State transitions are driven by `next_event_tick`. On Erupt this function
// spawns a burst of food pellets in a radial pattern with outward velocity.
// Stateless w.r.t. global storage; per-geyser state lives on the entity.
void processGeysers(World& world, const Tuning& t);

// Triggered by commands
void doSplit(World& world, PlayerId player, const Tuning& t, std::vector<GameEvent>& events);
void doEject(World& world, PlayerId player, const Tuning& t);
void doDash(World& world, PlayerId player, const Tuning& t);
// 4th ability. The largest player cell spends a fraction of its mass to push every
// enemy cell (and food, weaker) within `blast_radius` outward. Emits a BlastEvent
// for client-side feel. Respects min-mass + cooldown.
void doBlast(World& world, PlayerId player, const Tuning& t, std::vector<GameEvent>& events);
// Spawns a fresh cell for `player` at a randomly-chosen clear position. Uses the
// world RNG so the spawn is deterministic. Skips if `player` already has any cells
// in the world (idempotent against double-respawn-clicks from a peer). Used by the
// multiplayer respawn path: a dead client sends a RespawnCmd, the host queues it,
// and the sim calls doRespawn during applyCommand.
void doRespawn(World& world, PlayerId player, const Tuning& t);

// Weighted food-tier roll. Used by both Simulation::ctor (initial pile) and respawnFood().
// Returns one of {1, 3, 6, 12} -- common / uncommon / rare / epic. Consumes one rng.nextFloat().
float rollFoodMass(Rng& rng);

} // namespace cr::rules
