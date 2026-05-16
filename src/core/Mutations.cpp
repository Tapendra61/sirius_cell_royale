#include "Mutations.h"

#include <algorithm>
#include <cstdio>

namespace cr {

namespace {

// Single source of truth for HUD names + flavor text. Order MUST match the
// MutationKind enum so indexing by static_cast<int>(kind) Just Works.
constexpr MutationInfo kTable[] = {
    /* None           */ {"Standard",        "No mutation -- vanilla match"},
    /* Feast          */ {"Feast",           "Food and power-ups everywhere"},
    /* Famine         */ {"Famine",          "Sparse food -- scavenge to survive"},
    /* Outbreak       */ {"Outbreak",        "Viruses spawn in plague numbers"},
    /* SpeedDemon     */ {"Speed Demon",     "Cells move 50% faster"},
    /* Heavy          */ {"Heavy",           "Slower base speed, less mass falloff"},
    /* CometStorm     */ {"Comet Storm",     "Comets streak the sky four times faster"},
    /* BlackholeFever */ {"Blackhole Fever", "The map is riddled with singularities"},
    /* GeyserRush     */ {"Geyser Rush",     "Food fountains erupt across the map"},
    /* PickupFrenzy   */ {"Pickup Frenzy",   "Power-ups carpet the arena"},
    /* TidalSurge     */ {"Tidal Surge",     "Ocean currents drag everything sideways"},
    /* GlassCannon    */ {"Glass Cannon",    "Cheaper splits, blasts, and dashes"},
    /* Bloom          */ {"Bloom",           "Food density doubled"},
};
static_assert(sizeof(kTable) / sizeof(kTable[0]) ==
                  static_cast<size_t>(MutationKind::COUNT),
              "kTable must have one entry per MutationKind");

// Forward-compat fallback. If a future host ships a new MutationKind we don't
// understand, we still want SOMETHING for the HUD to print rather than crashing.
constexpr MutationInfo kUnknown{"Unknown Mutation",
                                "An unknown world trait is active"};

// splitmix64 -- tiny, statistically clean, deterministic. We avoid bringing in
// std::mt19937 here so the picker is header-light and has zero runtime cost.
uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

} // namespace

const MutationInfo& mutationInfoFor(MutationKind k) {
    const auto i = static_cast<size_t>(k);
    if (i >= static_cast<size_t>(MutationKind::COUNT)) return kUnknown;
    return kTable[i];
}

MutationKind pickRandomMutation(uint64_t seed) {
    // Hash the seed once so callers can pass the raw match seed (which often
    // has low entropy in test setups) and still get a well-spread choice.
    const uint64_t h = splitmix64(seed ^ 0xCE11C0DEDABCDEFULL);
    const uint32_t span = static_cast<uint32_t>(MutationKind::COUNT) - 1u; // exclude None
    const uint32_t idx  = static_cast<uint32_t>(h % span) + 1u;            // 1..COUNT-1
    return static_cast<MutationKind>(idx);
}

void applyMutation(Tuning& t, MutationKind k) {
    // Bounds + None short-circuit. Logged at warning level for forward-compat
    // visibility (a remote host shipped a kind we don't know).
    switch (k) {
    case MutationKind::None:
        return;
    case MutationKind::Feast:
        // More food, more power-ups. Pure escalation match.
        t.food_target   = static_cast<int>(t.food_target   * 2);
        t.pickup_target = static_cast<int>(t.pickup_target * 2);
        break;
    case MutationKind::Famine:
        // Food crash. Tip: don't drop pickups to 0 -- the power-up cycle is too
        // fun to remove entirely, just cut it in half.
        t.food_target   = std::max(50, t.food_target / 3);
        t.pickup_target = std::max(2,  t.pickup_target / 2);
        break;
    case MutationKind::Outbreak:
        // 3x viruses. Map turns into a minefield; splits are dangerous.
        t.virus_count = t.virus_count * 3;
        break;
    case MutationKind::SpeedDemon:
        // +50% base speed across the board. Falloff unchanged so big cells
        // still slow down proportionally.
        t.base_speed = t.base_speed * 1.5f;
        break;
    case MutationKind::Heavy:
        // Slower base, but mass barely slows you -- big cells stay agile.
        // Inverts the usual "small=fast, big=slow" dynamic.
        t.base_speed    = t.base_speed * 0.6f;
        t.speed_falloff = t.speed_falloff * 0.5f;
        break;
    case MutationKind::CometStorm:
        // Comets become a constant threat. Telegraph time unchanged so the
        // player can still react -- just more frequently.
        t.comet_event_interval_sec = t.comet_event_interval_sec * 0.25f;
        t.comet_first_after_sec    = t.comet_first_after_sec    * 0.33f;
        break;
    case MutationKind::BlackholeFever:
        // 3x the holes. Halve the min separation so they can actually fit.
        // The hole-spawn algorithm needs the smaller separation to not give up
        // early on placements; without this it caps out at maybe 8 holes.
        t.blackhole_count          = t.blackhole_count * 3;
        t.blackhole_min_separation = t.blackhole_min_separation * 0.5f;
        break;
    case MutationKind::GeyserRush:
        // 3x geysers, fast eruption cycle. Food explodes outward constantly.
        t.geyser_count          = t.geyser_count * 3;
        t.geyser_interval_sec   = t.geyser_interval_sec * 0.33f;
        t.geyser_first_after_sec = t.geyser_first_after_sec * 0.33f;
        // Same separation trick as black holes -- spawner won't fit 15 geysers
        // at the default 3000.
        t.geyser_min_separation = t.geyser_min_separation * 0.5f;
        break;
    case MutationKind::PickupFrenzy:
        // 4x power-ups. Shield / magnet / stealth uptime feels permanent.
        t.pickup_target = t.pickup_target * 4;
        break;
    case MutationKind::TidalSurge:
        // 2.5x tidal current strength. Bands push cells faster than they
        // can move, so the player has to actively fight the drift.
        t.tidal_band_strength = t.tidal_band_strength * 2.5f;
        break;
    case MutationKind::GlassCannon:
        // Cheaper everything. Splits, blasts, dashes all become spammy
        // tactical tools instead of "save for the right moment" commitments.
        t.min_mass_to_split   = t.min_mass_to_split * 0.6f;
        t.blast_min_mass      = t.blast_min_mass    * 0.6f;
        t.blast_cost_percent  = t.blast_cost_percent * 0.7f;
        t.cooldown_sec        = t.cooldown_sec       * 0.5f;  // dash cooldown
        t.blast_cooldown_sec  = t.blast_cooldown_sec * 0.5f;
        break;
    case MutationKind::Bloom:
        // Gentler-than-Feast food bump. No pickup change. Tilts the run toward
        // a slow, deliberate growth match without the chaos of Feast.
        t.food_target = t.food_target * 2;
        break;
    case MutationKind::COUNT:
        // Unreachable in normal flow; treat as None.
        return;
    }
    // No default -- exhaustive switch means we'll get a compiler warning if a
    // new MutationKind variant is added without an apply branch.
}

} // namespace cr
