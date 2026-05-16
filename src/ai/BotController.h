#pragma once

#include "BotPersonality.h"
#include "core/Rng.h"
#include "core/Tuning.h"
#include "core/Types.h"
#include "sim/World.h"

namespace cr::ai {

enum class BotState : uint8_t {
    Wander       = 0,
    SeekFood     = 1,
    FleePredator = 2,
    ChasePrey    = 3,
    SplitToKill  = 4,
};

// Per-bot mind: persists between ticks. Stored by BotDirector.
struct BotMind {
    PlayerId       player           = INVALID_PLAYER;
    BotPersonality personality      = BotPersonality::Greedy;
    BotState       state            = BotState::Wander;
    Vec2           wander_target{};
    Tick           wander_set_at    = 0;
    bool           is_elite         = false; // spawned scaled to player mass; gets a halo

    // EMA-smoothed move target so per-tick scan choices don't make the cell visibly jitter.
    Vec2           smoothed_target{};
    bool           smoothed_init    = false;

    // Flee hysteresis: once we're fleeing a specific threat, commit to it for a short window
    // and use a wider drop-off radius so a single tick of "threat barely out of range" doesn't
    // flip us back into seeking food.
    EntityId       fled_threat_id   = INVALID_ENTITY;
    Tick           flee_until       = 0;

    // Virus avoidance: when close to a virus, the bot orbits it (tangent direction picked at
    // entry so the orbit takes it in the direction it wanted to go). avoid_intent is the
    // bot's original destination at avoidance entry; the orbit only exits once the bot is
    // on the same hemisphere of the virus as that intent (so the resumed straight-line path
    // doesn't take it back through). virus_avoid_until is a hard fail-safe.
    EntityId       avoiding_virus_id  = INVALID_ENTITY;
    Tick           virus_avoid_until  = 0;
    Vec2           avoid_intent{};
    int8_t         avoid_tangent_sign = 0; // +1 or -1; chosen at entry

    // Sticky prey lock-on. Once a bot (typically Hunter) commits to a target it tracks
    // them with view-range hysteresis for chase_until ticks. Multiple bots locking onto
    // the player independently creates emergent pack hunting.
    EntityId       chasing_id          = INVALID_ENTITY;
    Tick           chase_committed_until = 0;

    // Dash windup (Hunter only): queue a future dash instead of firing it instantly so
    // the player gets a brief visual tell before the strike. Randomised window keeps
    // it from being perfectly predictable.
    Tick           dash_windup_started = 0;
    Tick           dash_windup_until   = 0;

    // Pack-flank offset (Hunter / Apex). When chasing the human player, the bot aims
    // not at the player's centre but at a point offset perpendicularly from the
    // chase line by `sin(flank_radians) * my_r * kFlankScale`. Each bot gets a
    // random flank at spawn -- with multiple Hunters in pursuit they end up
    // approaching from different sides instead of stacking on top of each other,
    // which produces emergent "surround the prey" behaviour without explicit
    // inter-bot communication.
    float          flank_radians = 0.0f;
};

// What the bot decided to do this tick. Caller turns these into Commands.
struct BotDecision {
    Vec2     move_target{};
    bool     split        = false;
    bool     eject        = false;
    bool     dash         = false;
    bool     blast        = false; // Q-blast (Mass Blast); BotDirector emits BlastCmd
    BotState chosen_state = BotState::Wander;
};

// Reads `self` and `world` (read-only); mutates `mind` (wander state, smoothing, flee
// commitment); consumes from `rng` (deterministic). Pure on those inputs.
// player_max_mass: the human player's tracked peak mass (BotDirector provides it). Used
// to scale this bot's mass cap so it remains a credible threat at any player size.
BotDecision decide(BotMind& mind, const Cell& self, const World& world,
                   const Tuning& t, Rng& rng, Tick now, float player_max_mass);

} // namespace cr::ai
