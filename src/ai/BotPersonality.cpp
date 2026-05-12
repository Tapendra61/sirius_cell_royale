#include "BotPersonality.h"

namespace cr::ai {

PersonalityWeights weightsFor(BotPersonality p) {
    switch (p) {
    case BotPersonality::Greedy:
        // Long food vision, ignores threats until close. Easy to bait into walls of food.
        return {/*view*/         3000.0f,
                /*flee_mult*/       2.5f,
                /*chase_mult*/      0.0f,
                /*split_aggro*/     0.0f,
                /*dash_eager*/      0.05f,
                /*wander_period*/   4.0f,
                /*corner_pull*/     0.0f,
                /*fears_viruses*/   true,
                /*max_mass_factor*/ 0.9f,
                /*human_bias*/      1.0f,
                /*responsiveness*/  0.25f};
    case BotPersonality::Cautious:
        // Sees threats from far, flees at long range, never splits. Very smooth movement.
        return {2000.0f, 20.0f, 0.0f, 0.0f, 0.0f,  6.0f, 0.0f, true,  0.6f, 1.0f, 0.12f};
    case BotPersonality::Hunter:
        // Aggressive, long chase range, strongly prefers the human player, splits often.
        return {3500.0f,  5.0f, 25.0f, 0.8f, 0.30f, 2.0f, 0.0f, true,  1.2f, 3.5f, 0.32f};
    case BotPersonality::Hoarder:
        // Camps the corner: huge pull, slow re-roll, near-zero responsiveness. Hard to budge.
        return {1100.0f,  6.0f,  0.0f, 0.0f, 0.0f,  8.0f, 0.92f, true, 1.0f, 1.0f, 0.10f};
    case BotPersonality::Reckless:
        // Comic relief: snappy/jittery target, ignores viruses, splits constantly, dashes a lot.
        return {2200.0f,  1.5f, 20.0f, 1.0f, 1.00f, 1.0f, 0.0f, false, 1.1f, 1.0f, 0.55f};
    case BotPersonality::Count:
        break;
    }
    return {};
}

const char* nameOf(BotPersonality p) {
    switch (p) {
    case BotPersonality::Greedy:   return "Greedy";
    case BotPersonality::Cautious: return "Cautious";
    case BotPersonality::Hunter:   return "Hunter";
    case BotPersonality::Hoarder:  return "Hoarder";
    case BotPersonality::Reckless: return "Reckless";
    case BotPersonality::Count:    break;
    }
    return "?";
}

char letterOf(BotPersonality p) {
    switch (p) {
    case BotPersonality::Greedy:   return 'G';
    case BotPersonality::Cautious: return 'C';
    case BotPersonality::Hunter:   return 'H';
    case BotPersonality::Hoarder:  return 'h';
    case BotPersonality::Reckless: return 'R';
    case BotPersonality::Count:    break;
    }
    return '?';
}

} // namespace cr::ai
