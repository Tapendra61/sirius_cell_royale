#include "BotPersonality.h"

namespace cr::ai {

PersonalityWeights weightsFor(BotPersonality p) {
    // Field order matches the struct (top-to-bottom). Each row mirrors a one-line
    // intent: "I am the ___ bot." Phase 6 tuning aims to give each personality a
    // visible niche; Hunter remains the apex predator but isn't the only threat.
    switch (p) {
    case BotPersonality::Greedy:
        // The food monarch: huge food vision, strongest value weighting, will snap at
        // anything that wanders very close. Caps slightly above player so they end up
        // as roaming fat targets that occasionally eat you back.
        return {/*view*/         3500.0f,
                /*flee_mult*/       2.5f,
                /*chase_mult*/      3.0f,  // tiny opportunistic chase (was 0)
                /*split_aggro*/     0.2f,  // occasional split at close prey (was 0)
                /*dash_eager*/      0.10f,
                /*wander_period*/   4.0f,
                /*corner_pull*/     0.0f,
                /*fears_viruses*/   true,
                /*max_mass_factor*/ 1.1f,  // can exceed player (was 0.9)
                /*human_bias*/      1.0f,
                /*responsiveness*/  0.25f,
                /*food_value_w*/    3.0f,  // even more value-focused (was 2.0)
                /*prey_lead_sec*/   0.0f};
    case BotPersonality::Cautious:
        // The unkillable survivor: longer vision, panicked flee range, panic dashes.
        // Mass cap raised so a Cautious that's been hiding all match is actually big.
        return {2500.0f, 25.0f, 0.0f, 0.0f, 0.20f, 6.0f, 0.0f, true, 0.85f, 1.0f, 0.12f, 1.0f, 0.0f};
    case BotPersonality::Hunter:
        // Still the apex predator -- lead-aim, sticky lock-on, dash telegraph. Mass cap
        // and chase range nudged down to make room for the rest of the cast.
        return {4500.0f, 4.0f, 32.0f, 0.95f, 0.45f, 2.0f, 0.0f, true, 1.25f, 5.0f, 0.32f, 0.3f, 0.7f};
    case BotPersonality::Hoarder:
        // The fortress: enormous mass cap, wide bite radius from its corner, will now
        // *split-launch* at close prey -- approaching their corner is a real trap.
        return {1500.0f, 6.0f, 10.0f, 0.5f, 0.20f, 8.0f, 0.92f, true, 1.4f, 1.0f, 0.10f, 5.0f, 0.0f};
    case BotPersonality::Reckless:
        // The chaos lord: now also targets the player (2x bias), gets sticky lock-on
        // (2s), longer chase, better prey-lead. Still no dash telegraph -- their dashes
        // are random by design.
        return {2800.0f, 1.5f, 28.0f, 1.0f, 1.0f, 1.0f, 0.0f, false, 1.35f, 2.0f, 0.55f, 0.8f, 0.4f};
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

char letterForTag(uint8_t tag) {
    // Tag 0 means the human player; non-zero tags are BotPersonality+1.
    if (tag == 0) return 'P';
    const uint8_t pi = static_cast<uint8_t>(tag - 1);
    if (pi >= static_cast<uint8_t>(BotPersonality::Count)) return '?';
    return letterOf(static_cast<BotPersonality>(pi));
}

} // namespace cr::ai
