#include "BotPersonality.h"

namespace cr::ai {

PersonalityWeights weightsFor(BotPersonality p) {
    // Field order matches the struct (top-to-bottom). Each row mirrors a one-line
    // intent: "I am the ___ bot."
    switch (p) {
    case BotPersonality::Greedy:
        // Long food vision, ignores threats until close, strong pull toward big food.
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
                /*responsiveness*/  0.25f,
                /*food_value_w*/    2.0f,  // mass 12 weighs 23x vs mass 1 at equal dist
                /*prey_lead_sec*/   0.0f};
    case BotPersonality::Cautious:
        // Long flee range, never splits, neutral food preference, smooth movement, low cap.
        return {2000.0f, 20.0f, 0.0f, 0.0f, 0.0f, 6.0f, 0.0f, true, 0.6f, 1.0f, 0.12f, 0.5f, 0.0f};
    case BotPersonality::Hunter:
        // The apex predator. Long chase range, near-always-splits in pounce range,
        // strongly prefers human cells, leads prey velocity, scales above the player.
        return {4500.0f, 4.0f, 35.0f, 0.95f, 0.45f, 2.0f, 0.0f, true, 1.3f, 5.0f, 0.32f, 0.3f, 0.7f};
    case BotPersonality::Hoarder:
        // Camps a corner; opportunistically snaps at prey/food within bite range; loves
        // valuable food above all else.
        return {1100.0f, 6.0f, 4.0f, 0.0f, 0.0f, 8.0f, 0.92f, true, 1.0f, 1.0f, 0.10f, 4.0f, 0.0f};
    case BotPersonality::Reckless:
        // Ignores viruses, dashes and splits constantly, scales just above the player.
        return {2200.0f, 1.5f, 20.0f, 1.0f, 1.00f, 1.0f, 0.0f, false, 1.2f, 1.0f, 0.55f, 0.8f, 0.2f};
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
