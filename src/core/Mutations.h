#pragma once

#include "Tuning.h"

#include <cstdint>

namespace cr {

// Per-match world Mutations: a randomized "world trait" rolled at match start
// (host-authoritative) and applied to the active Tuning before the Simulation is
// constructed. Examples: viruses spawn 3x as often, food grants 2x population,
// comets storm the map. Mutations are intentionally bold -- the goal is for
// every match to feel mechanically distinct, not just a tuning nudge.
//
// Determinism contract: the host picks the kind from a seed (deterministic), then
// applies it to its Tuning copy BEFORE constructing the Simulation. The kind is
// wired into the host->client Welcome handshake so each joining peer applies the
// same modifications to its local Tuning -- so client-side prediction, HUD
// banner, and renderer all see consistent values. The Simulation itself never
// sees a MutationKind; it just sees a Tuning struct, same as before.
//
// Wire format: 1 byte on the wire (uint8_t). New variants append to the end of
// the enum; old clients that receive an unknown kind treat it as None (no
// modification + a warning log). Don't reorder existing variants.

enum class MutationKind : uint8_t {
    None           = 0,  // Vanilla match, no overrides. Fallback / explicit-disable.

    Feast          = 1,  // Food + pickups everywhere. Easy growth, fast escalation.
    Famine         = 2,  // Sparse food + pickups. Scavenger-hunt feel.
    Outbreak       = 3,  // ~3x virus population. Map is a minefield.
    SpeedDemon     = 4,  // +50% base speed. Twitch reflex match.
    Heavy          = 5,  // Slower base speed, less mass-induced falloff. Sluggish brawler.
    CometStorm     = 6,  // Comets fire ~4x as often, first comet hits fast.
    BlackholeFever = 7,  // 3x black-hole density. The map turns into Swiss cheese.
    GeyserRush     = 8,  // ~3x geysers, fast eruption cycle. Food fountains.
    PickupFrenzy   = 9,  // 4x power-up population. Shields / magnets / stealth everywhere.
    TidalSurge     = 10, // Tidal currents 2.5x stronger. Map drags you sideways.
    GlassCannon    = 11, // Cheaper splits / blasts / dashes. Higher skill ceiling.
    Bloom          = 12, // 2x food population (gentler than Feast; no pickup boost).

    COUNT          // sentinel; not a valid value
};

// Human-readable metadata for HUD display + logs.
struct MutationInfo {
    const char* name;        // short title shown in the banner ("Outbreak")
    const char* description; // 1-line tagline ("Viruses spawn everywhere")
};

// Lookup. Unknown kinds (forward-compat) map to a synthesized "Unknown" entry
// so the HUD has SOMETHING to show.
const MutationInfo& mutationInfoFor(MutationKind k);

// Deterministic picker. Same input seed -> same output. Picks uniformly from
// MutationKind values 1..COUNT-1 (excluding None so every match feels mutated).
MutationKind pickRandomMutation(uint64_t seed);

// In-place modification of `t` based on `k`. No-op for None / unknown.
// Idempotent only if the caller passes a fresh / un-mutated tuning -- chaining
// applyMutation calls on the same tuning stacks the effects, which is not
// intended. Mutations are picked + applied exactly once per match.
void applyMutation(Tuning& t, MutationKind k);

} // namespace cr
