#include "Hud.h"

#include "raylib.h"

#include <algorithm>
#include <cstdio>

namespace cr {

void Hud::onPlayerAbsorb(float /*mass_gained*/, double now_sec, float combo_window_sec) {
    if (last_absorb_sec_ < 0.0 || now_sec - last_absorb_sec_ > combo_window_sec) {
        combo_count_ = 1;
    } else {
        ++combo_count_;
    }
    last_absorb_sec_ = now_sec;
    best_combo_      = std::max(best_combo_, combo_count_);
    combo_flash_     = 1.0f;
}

void Hud::onPlayerDeath() {
    player_dead_ = true;
    combo_count_ = 0;
}

void Hud::onPlayerRespawn() {
    player_dead_     = false;
    last_absorb_sec_ = -1.0;
}

void Hud::onPlayerNearMissAttacker() {
    near_miss_red_ = std::max(near_miss_red_, 1.0f);
}

void Hud::onPlayerNearMissPrey() {
    near_miss_gold_ = std::max(near_miss_gold_, 1.0f);
}

void Hud::update(float frame_dt, double now_sec, float combo_window_sec) {
    combo_flash_    = std::max(0.0f, combo_flash_   - frame_dt * 4.0f);
    near_miss_red_  = std::max(0.0f, near_miss_red_ - frame_dt * 1.8f);
    near_miss_gold_ = std::max(0.0f, near_miss_gold_ - frame_dt * 1.8f);

    // Combo expires if no absorbs within window.
    if (combo_count_ > 0 && last_absorb_sec_ > 0.0
        && now_sec - last_absorb_sec_ > combo_window_sec) {
        combo_count_ = 0;
    }
}

void Hud::render(int screen_w, int screen_h, const Cell* watched, Tick tick,
                 int fps, float zoom, float dt_mult, bool paused, bool touch) const {
    // ----- Near-miss vignettes -----
    if (near_miss_red_ > 0.0f) {
        unsigned char a = static_cast<unsigned char>(near_miss_red_ * 100.0f);
        DrawRectangle(0, 0, screen_w, screen_h, Color{220, 40, 40, a});
        const char* msg = "CLOSE ONE";
        int fs = 36;
        int tw = MeasureText(msg, fs);
        DrawText(msg, screen_w / 2 - tw / 2, 80, fs,
                 Color{255, 60, 60, static_cast<unsigned char>(near_miss_red_ * 230.0f)});
    }
    if (near_miss_gold_ > 0.0f) {
        unsigned char a = static_cast<unsigned char>(near_miss_gold_ * 70.0f);
        DrawRectangle(0, 0, screen_w, screen_h, Color{220, 180, 40, a});
    }

    // ----- Combo counter, top-right -----
    if (combo_count_ >= 2) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "x%d", combo_count_);
        int base_size = 36;
        int extra     = std::min(28, combo_count_ * 2);
        int fs        = base_size + extra + static_cast<int>(combo_flash_ * 12.0f);
        int tw        = MeasureText(buf, fs);
        // Outline.
        Color shadow{0, 0, 0, 180};
        DrawText(buf, screen_w - tw - 22 + 2, 22 + 2, fs, shadow);
        // Hot color scales with streak.
        unsigned char g = static_cast<unsigned char>(std::max(80, 220 - combo_count_ * 10));
        DrawText(buf, screen_w - tw - 22, 22, fs, Color{255, g, 60, 255});
    }
    if (best_combo_ >= 2) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "best: x%d", best_combo_);
        DrawText(buf, screen_w - MeasureText(buf, 16) - 24, 100, 16,
                 Color{220, 200, 140, 200});
    }

    // ----- "YOU DIED" banner -----
    if (player_dead_) {
        DrawRectangle(0, 0, screen_w, screen_h, Color{0, 0, 0, 80});
        const char* msg = "YOU DIED";
        int fs = 64;
        int tw = MeasureText(msg, fs);
        DrawText(msg, screen_w / 2 - tw / 2, screen_h / 2 - 40, fs, Color{255, 70, 70, 240});
        const char* hint = "respawn arrives in Phase 8 - press ESC to pause / close window to exit";
        int hs = 16;
        int htw = MeasureText(hint, hs);
        DrawText(hint, screen_w / 2 - htw / 2, screen_h / 2 + 32, hs,
                 Color{220, 220, 220, 200});
    }

    // ----- Debug stats overlay (bottom-left) -----
    char buf[160];
    std::snprintf(buf, sizeof(buf), "FPS %d  Tick %u", fps, static_cast<unsigned>(tick));
    DrawText(buf, 12, screen_h - 22, 16, Color{200, 200, 200, 220});
    if (watched) {
        const char* mode = touch ? "touch" : "desktop";
        std::snprintf(buf, sizeof(buf),
                      "mass %.1f  pos (%.0f, %.0f)  zoom %.2f  dt_x%.2f  %s%s",
                      watched->mass, watched->pos.x, watched->pos.y, zoom, dt_mult, mode,
                      paused ? "  [PAUSED]" : "");
        DrawText(buf, 12, screen_h - 40, 16, Color{200, 200, 200, 220});
    }
}

} // namespace cr
