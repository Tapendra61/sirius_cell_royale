#include "Missions.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace cr {

uint32_t currentDay() {
    std::time_t t = std::time(nullptr);
    if (t == static_cast<std::time_t>(-1)) return 0;
    return static_cast<uint32_t>(t / 86400);
}

namespace {

struct MissionTemplate {
    MissionKind kind;
    int32_t     target_min;
    int32_t     target_max;
    int32_t     round_to;  // round selected target down to a multiple of this
};

// Tunable pool. Targets are deliberately reachable but not trivial.
constexpr MissionTemplate kPool[] = {
    {MissionKind::EatFood,    150, 600,   50},
    {MissionKind::ReachMass, 2000, 7500, 500},
    {MissionKind::LandCrits,    3,  12,    1},
    {MissionKind::SurviveSec,  60, 240,   30},
};
constexpr int kPoolSize = static_cast<int>(sizeof(kPool) / sizeof(kPool[0]));

// xorshift seeded by the day -- guarantees stable rolls for a given day.
uint32_t nextRoll(uint32_t& s) {
    if (s == 0) s = 0x9E3779B9u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

} // namespace

void rollDailyMissions(uint32_t day, Mission out[kMissionCount]) {
    uint32_t rng = day ? day : 0x9E3779B9u;

    // Shuffle pool indices (Fisher-Yates) using the seeded RNG.
    int idx[kPoolSize];
    for (int i = 0; i < kPoolSize; ++i) idx[i] = i;
    for (int i = kPoolSize - 1; i > 0; --i) {
        int j = static_cast<int>(nextRoll(rng) % (i + 1));
        std::swap(idx[i], idx[j]);
    }

    const int count = std::min(kMissionCount, kPoolSize);
    for (int i = 0; i < count; ++i) {
        const MissionTemplate& t = kPool[idx[i]];
        int span = t.target_max - t.target_min;
        int pick = t.target_min + static_cast<int>(nextRoll(rng) % (span + 1));
        if (t.round_to > 1) {
            pick = (pick / t.round_to) * t.round_to;
            if (pick < t.target_min) pick = t.target_min;
        }
        out[i] = Mission{t.kind, pick, 0, false};
    }
    for (int i = count; i < kMissionCount; ++i) {
        out[i] = Mission{};
    }
}

const char* missionGoalText(const Mission& m, char* buf, int buflen) {
    switch (m.kind) {
        case MissionKind::EatFood:
            std::snprintf(buf, buflen, "Eat %d food", m.target);
            break;
        case MissionKind::ReachMass:
            std::snprintf(buf, buflen, "Reach mass %d", m.target);
            break;
        case MissionKind::LandCrits:
            std::snprintf(buf, buflen, "Land %d crits", m.target);
            break;
        case MissionKind::SurviveSec:
            std::snprintf(buf, buflen, "Survive %ds", m.target);
            break;
        default:
            std::snprintf(buf, buflen, "(no mission)");
            break;
    }
    return buf;
}

const char* missionStatusText(const Mission& m, char* buf, int buflen) {
    if (m.kind == MissionKind::None) {
        std::snprintf(buf, buflen, "(no mission)");
        return buf;
    }
    int p = std::min(m.progress, m.target);
    if (m.completed) {
        char goal[64];
        std::snprintf(buf, buflen, "DONE   %s",
                      missionGoalText(m, goal, sizeof(goal)));
    } else {
        char goal[64];
        std::snprintf(buf, buflen, "%s   %d/%d",
                      missionGoalText(m, goal, sizeof(goal)), p, m.target);
    }
    return buf;
}

} // namespace cr
