#include "BotPersonality.h"

namespace cr::ai {

PersonalityWeights weightsFor(BotPersonality p) {
    // Field order matches the struct (top-to-bottom). Each row mirrors a one-line
    // intent: "I am the ___ bot." Last field on each row is blast_aggression.
    switch (p) {
    case BotPersonality::Greedy:
        // The food monarch: huge food vision, strongest value weighting, will snap at
        // anything that wanders very close. Caps slightly above player so they end up
        // as roaming fat targets that occasionally eat you back. Doesn't blast --
        // they're food-focused, not hunters.
        return {/*view*/         3500.0f,
                /*flee_mult*/       2.5f,
                /*chase_mult*/      3.0f,
                /*split_aggro*/     0.2f,
                /*dash_eager*/      0.10f,
                /*wander_period*/   4.0f,
                /*corner_pull*/     0.0f,
                /*fears_viruses*/   true,
                /*max_mass_factor*/ 1.1f,
                /*human_bias*/      1.0f,
                /*responsiveness*/  0.25f,
                /*food_value_w*/    3.0f,
                /*prey_lead_sec*/   0.0f,
                /*blast_aggro*/     0.0f};
    case BotPersonality::Cautious:
        // The unkillable survivor. Never blasts (defensive specialist).
        return {2500.0f, 25.0f, 0.0f, 0.0f, 0.20f, 6.0f, 0.0f, true, 0.85f, 1.0f, 0.12f, 1.0f, 0.0f, 0.0f};
    case BotPersonality::Hunter:
        // The apex predator. Lobs blasts at fleeing prey to disrupt escape splits.
        // Smart-blast heuristic gates the actual firing so they don't blow the
        // cooldown on a low-value target. Toned down from previous values:
        //   chase_range_mult: 32 -> 26 (gives up the chase sooner instead of
        //     committing across half the map every time)
        //   split_aggression: 0.95 -> 0.80 (fewer reckless split-bombs)
        //   human_target_bias: 5.0 -> 3.5 (still hard-locks the player but
        //     not laser-glued to them)
        // The intercept-lead-aim (0.7s) + dash telegraph + smart-blast gate
        // are unchanged -- those are what make Hunter a Hunter; the dials
        // we softened are pure aggression dials.
        return {4500.0f, 4.0f, 26.0f, 0.80f, 0.45f, 2.0f, 0.0f, true, 1.25f, 3.5f, 0.32f, 0.3f, 0.7f, 0.35f};
    case BotPersonality::Hoarder:
        // The fortress. Doesn't blast -- they camp the corner and let prey come
        // to them. Wasting blasts on flyby targets is off-brand.
        return {1500.0f, 6.0f, 10.0f, 0.5f, 0.20f, 8.0f, 0.92f, true, 1.4f, 1.0f, 0.10f, 5.0f, 0.0f, 0.0f};
    case BotPersonality::Reckless:
        // The chaos lord. Will blast occasionally but the smart-blast gate keeps
        // them from spamming -- they're impulsive, not stupid.
        return {2800.0f, 1.5f, 28.0f, 1.0f, 1.0f, 1.0f, 0.0f, false, 1.35f, 2.0f, 0.55f, 0.8f, 0.4f, 0.20f};
    case BotPersonality::Apex:
        // The late-game terror. Slow, fearless, sticky lock-on, blast aggression
        // gated by the smart heuristic so it doesn't pop on the first frame in
        // range.
        return {/*view*/         5500.0f,
                /*flee_mult*/       1.0f,
                /*chase_mult*/      45.0f,
                /*split_aggro*/     0.85f,
                /*dash_eager*/      0.30f,
                /*wander_period*/   8.0f,
                /*corner_pull*/     0.0f,
                /*fears_viruses*/   true,
                /*max_mass_factor*/ 1.7f,
                /*human_bias*/      8.0f,
                /*responsiveness*/  0.22f,
                /*food_value_w*/    1.2f,
                /*prey_lead_sec*/   0.60f,
                /*blast_aggro*/     0.55f};
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
    case BotPersonality::Apex:     return "Apex";
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
    case BotPersonality::Apex:     return 'A';
    case BotPersonality::Count:    break;
    }
    return '?';
}

char letterForTag(uint8_t tag) {
    // Tag 0 means the human player; non-zero tags are BotPersonality+1.
    if (tag == 0) return 'P';
    const uint8_t pi = static_cast<uint8_t>(tag - 1);
    if (pi >= static_cast<uint8_t>(BotPersonality::Count)) return '?';
    return letterOf(static_cast<BotPersonality>(pi));
}

} // namespace cr::ai
