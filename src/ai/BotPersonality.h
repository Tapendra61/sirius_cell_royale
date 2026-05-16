#pragma once

#include "core/Types.h"

#include <cstdint>

namespace cr::ai {

// First PlayerId allocated to bots. The human player is below this; bots are at or above.
// Centralised here so prey-scoring code can tell "is this the human?" without a circular
// include of BotDirector.h.
inline constexpr PlayerId kFirstBotPlayerId = 100;

enum class BotPersonality : uint8_t {
    Greedy   = 0,
    Cautious = 1,
    Hunter   = 2,
    Hoarder  = 3,
    Reckless = 4,
    // Apex: the late-game terror. Spawns only as an elite once the player is big
    // (>= ~5000 mass). Huge, persistent lock-on, freely uses Q-blast to scatter
    // the player's split pieces. Designed to keep VS AI threatening at any size.
    Apex     = 5,
    Count    = 6,
};

// Weight vector that turns into FSM transition probabilities and ranges.
// Tuned by feel; tweak per personality to make them visibly distinct.
struct PersonalityWeights {
    float view_radius;          // world units the bot "sees"
    float flee_range_mult;      // flee when a threat is within this * my_radius
    float chase_range_mult;     // chase prey within this * my_radius; 0 = never chase
    float split_aggression;     // 0..1; chance to split-launch when in killing range
    float dash_eagerness;       // 0..1; how often to dash (fleeing or chasing)
    float wander_period_sec;    // how often to re-roll a wander target
    float corner_pull;          // 0..1; Hoarder bias toward the nearest world corner
    bool  fears_viruses;        // false = Reckless (drives near viruses)
    float max_mass_factor;      // mass cap = 500 * this; bots stop seeking food past it
    float human_target_bias;    // prey-score multiplier applied to cells owned by the human
                                // (Hunter is the obvious user; everyone else stays at 1.0)
    float target_responsiveness; // EMA alpha for move target. Low = smooth/sleepy (Cautious,
                                // Hoarder), high = snappy/jittery (Reckless).
    float food_value_weight;    // (mass - 1) multiplier in food scoring. 0 = pick nearest,
                                // higher = prefer epic/rare food over close common.
    float prey_lead_seconds;    // when chasing, aim at prey.pos + prey.vel * this. Hunter
                                // gets ~0.5 to intercept; others 0 (aim at current pos).
    float blast_aggression;     // 0..1; chance to fire Q-blast when in range with cooldown
                                // ready. 0 = never (Cautious / non-aggressors); high values
                                // turn the bot into a shockwave threat (Apex).
};

PersonalityWeights weightsFor(BotPersonality p);
const char*        nameOf(BotPersonality p);
char               letterOf(BotPersonality p);

// Convert the Cell::personality_tag encoding (0 = human player, N+1 = BotPersonality
// enum value) to the display letter used by the HUD glyph and the killfeed. Centralised
// so both renderer paths can't drift apart -- previously each had its own hard-coded
// {'P','G','C','H','h','R'} array.
char letterForTag(uint8_t tag);

} // namespace cr::ai
