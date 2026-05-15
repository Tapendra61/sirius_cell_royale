#pragma once

#include "core/Types.h"
#include "meta/Missions.h"
#include "sim/World.h"
#include "raylib.h"

#include <cstdint>

namespace cr {

// Player-facing game state machine. Lives on Client; Hud reads it to decide what to
// render (combo vs summary panel vs nothing).
enum class GamePhase : uint8_t {
    Playing,
    DeathCam,    // 1.5s slow-mo camera zoom on the killer
    Summary,     // 3s match-end panel with stats + respawn countdown
    Respawning,  // 1-tick state: main consumes the request and spawns a new cell
};

// Snapshot of stats shown at the end of a run. Built by Client at death-cam exit.
struct MatchSummary {
    int   final_mass            = 0;
    int   best_combo            = 0;
    float time_alive_sec        = 0.0f;
    int   xp_earned             = 0;
    int   level_before          = 1;
    int   level_after           = 1;
    int   total_xp              = 0;
    int   xp_for_current_level  = 0;
    int   xp_for_next_level     = 100;
    float remaining_sec         = 0.0f;

    // Daily missions snapshot, copied from Client at Summary entry.
    Mission missions[kMissionCount]{};
};

// Returned by Hud::render() when the user clicks a button on an overlay panel
// (Summary or Pause). Client converts these into actual state transitions.
enum class SummaryAction : uint8_t {
    None,
    PlayAgainNow,
    ReturnToMenu,
    ResumeFromPause,
};

// Killfeed line. Lives in Hud's recent_kills_ list and fades out after a few seconds.
// Names are pre-rendered to a small buffer at push time so the Hud doesn't need to look
// anything up later (predator/prey cells may be long gone by the time the entry fades).
struct KillfeedEntry {
    char     predator[24] = {0};  // e.g. "P1", "Hunter#7"
    char     prey[24]     = {0};
    Color    pred_color   = {255, 255, 255, 255};
    Color    prey_color   = {255, 255, 255, 255};
    double   spawned_sec  = 0.0;
    bool     involves_player = false; // bolded + slightly larger
};

class Hud {
public:
    void onPlayerAbsorb(float mass_gained, double now_sec, float combo_window_sec);
    void onPlayerDeath();
    void onPlayerRespawn();
    void onPlayerNearMissAttacker();
    void onPlayerNearMissPrey();
    void onCrit();
    // Push a kill into the killfeed. Older entries fall off automatically once we hit
    // the visible limit; everything fades after ~4s.
    void pushKillfeed(const KillfeedEntry& entry);

    void update(float frame_dt, double now_sec, float combo_window_sec);

    // Returns the action the user triggered this frame on the summary panel
    // (PlayAgainNow / ReturnToMenu). SummaryAction::None when no panel is shown
    // or no button was clicked.
    SummaryAction render(int screen_w, int screen_h, const Cell* watched, Tick tick,
                         int fps, float zoom, float dt_mult, bool paused, bool touch,
                         GamePhase phase, const MatchSummary* summary);

    int  combo() const { return combo_count_; }
    int  bestCombo() const { return best_combo_; }
    bool playerDead() const { return player_dead_; }

private:
    SummaryAction renderSummary(int screen_w, int screen_h, const MatchSummary& s);
    SummaryAction renderPauseOverlay(int screen_w, int screen_h);
    void          renderKillfeed(int screen_w, int screen_h, double now_sec) const;

    int    combo_count_         = 0;
    int    best_combo_          = 0;
    double last_absorb_sec_     = -1.0;
    float  combo_flash_         = 0.0f;
    float  near_miss_red_       = 0.0f;
    float  near_miss_gold_      = 0.0f;
    float  crit_flash_          = 0.0f;
    bool   player_dead_         = false;

    static constexpr int kKillfeedMax = 5;
    KillfeedEntry recent_kills_[kKillfeedMax] = {};
    int           recent_kills_count_         = 0; // active entries in [0, kKillfeedMax)
};

} // namespace cr
