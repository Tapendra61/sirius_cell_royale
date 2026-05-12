#pragma once

#include "core/Types.h"
#include "sim/World.h"

#include <cstdint>

namespace cr {

// Foreground UI overlay: combo counter, near-miss vignette, death banner, debug stats.
// Time-driven via real seconds (GetTime()) since these are presentation-only and don't
// need to be deterministic.
class Hud {
public:
    void onPlayerAbsorb(float mass_gained, double now_sec, float combo_window_sec);
    void onPlayerDeath();
    void onPlayerRespawn();
    void onPlayerNearMissAttacker();   // player's predator JUST missed eating them
    void onPlayerNearMissPrey();       // player JUST missed eating prey

    void update(float frame_dt, double now_sec, float combo_window_sec);

    void render(int screen_w, int screen_h, const Cell* watched, Tick tick,
                int fps, float zoom, float dt_mult, bool paused, bool touch) const;

    int  combo() const { return combo_count_; }
    int  bestCombo() const { return best_combo_; }
    bool playerDead() const { return player_dead_; }

private:
    int    combo_count_         = 0;
    int    best_combo_          = 0;
    double last_absorb_sec_     = -1.0;
    float  combo_flash_         = 0.0f; // brief size pulse on increment
    float  near_miss_red_       = 0.0f; // 0..1, fades over ~0.6s when predator just missed
    float  near_miss_gold_      = 0.0f; // 0..1, fades when player just missed prey
    bool   player_dead_         = false;
};

} // namespace cr
