#pragma once

#include <cstdint>

namespace cr {

// Daily mission system. Three missions are rolled at app start (or first launch of the
// day) from a small pool; completing one awards bonus XP. Completion persists for the
// rest of the day; progress is per-match (so "Reach mass 5000" tests a single run, not
// a cumulative total across runs).
enum class MissionKind : uint8_t {
    None       = 0,
    EatFood    = 1,  // target = food pellets absorbed (single match)
    ReachMass  = 2,  // target = peak mass in match
    LandCrits  = 3,  // target = crits landed (single match)
    SurviveSec = 4,  // target = seconds alive (single match)
};

constexpr int kMissionCount     = 3;
constexpr int kMissionXpReward  = 100; // bonus XP per mission completed

struct Mission {
    MissionKind kind      = MissionKind::None;
    int32_t     target    = 0;
    int32_t     progress  = 0;     // transient: per-match, not serialized
    bool        completed = false; // persists across matches until daily reset
};

// Days since 1970-01-01 UTC. Used for daily-reset detection. Returns 0 on time failure.
uint32_t currentDay();

// Roll 3 distinct missions for the given day. Same day -> same set (cute property and
// makes it possible for two people to compare missions if they ever played together).
void rollDailyMissions(uint32_t day, Mission out[kMissionCount]);

// Pretty-print a mission's goal text ("Eat 200 food") into `buf`. Returns `buf`.
const char* missionGoalText(const Mission& m, char* buf, int buflen);

// One-line status for a partially-complete mission ("Eat food   34/200").
const char* missionStatusText(const Mission& m, char* buf, int buflen);

} // namespace cr
