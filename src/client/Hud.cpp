#include "Hud.h"

#include "UiWidgets.h"
#include "raylib.h"

#include <algorithm>
#include <cmath>
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
    best_combo_      = 0; // each run gets its own best
}

void Hud::onPlayerNearMissAttacker() { near_miss_red_  = std::max(near_miss_red_,  1.0f); }
void Hud::onPlayerNearMissPrey()     { near_miss_gold_ = std::max(near_miss_gold_, 1.0f); }
void Hud::onCrit()                   { crit_flash_     = std::max(crit_flash_,    1.0f); }

void Hud::update(float frame_dt, double now_sec, float combo_window_sec) {
    combo_flash_    = std::max(0.0f, combo_flash_    - frame_dt * 4.0f);
    near_miss_red_  = std::max(0.0f, near_miss_red_  - frame_dt * 1.8f);
    near_miss_gold_ = std::max(0.0f, near_miss_gold_ - frame_dt * 1.8f);
    crit_flash_     = std::max(0.0f, crit_flash_     - frame_dt * 3.5f);

    if (combo_count_ > 0 && last_absorb_sec_ > 0.0
        && now_sec - last_absorb_sec_ > combo_window_sec) {
        combo_count_ = 0;
    }
}

SummaryAction Hud::render(int screen_w, int screen_h, const Cell* watched, Tick tick,
                          int fps, float zoom, float dt_mult, bool paused, bool touch,
                          GamePhase phase, const MatchSummary* summary) {
    // ----- Crit flash (briefer, brighter than near-miss vignettes) -----
    if (crit_flash_ > 0.0f) {
        unsigned char a = static_cast<unsigned char>(crit_flash_ * 110.0f);
        DrawRectangle(0, 0, screen_w, screen_h, Color{255, 240, 200, a});
    }
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

    // ----- Combo counter (only when alive / playing) -----
    if (phase == GamePhase::Playing && combo_count_ >= 2) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "x%d", combo_count_);
        int base_size = 36;
        int extra     = std::min(28, combo_count_ * 2);
        int fs        = base_size + extra + static_cast<int>(combo_flash_ * 12.0f);
        int tw        = MeasureText(buf, fs);
        Color shadow{0, 0, 0, 180};
        DrawText(buf, screen_w - tw - 22 + 2, 22 + 2, fs, shadow);
        unsigned char g = static_cast<unsigned char>(std::max(80, 220 - combo_count_ * 10));
        DrawText(buf, screen_w - tw - 22, 22, fs, Color{255, g, 60, 255});
    }

    // ----- Summary panel (Phase 8) -----
    SummaryAction summary_action = SummaryAction::None;
    if (phase == GamePhase::Summary && summary) {
        summary_action = renderSummary(screen_w, screen_h, *summary);
    } else if (paused && phase == GamePhase::Playing) {
        // Pause overlay -- only during active play. DeathCam/Summary/Respawning have
        // their own UI; layering pause on top of those would be confusing.
        summary_action = renderPauseOverlay(screen_w, screen_h);
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

    return summary_action;
}

SummaryAction Hud::renderSummary(int sw, int sh, const MatchSummary& s) {
    // Dim background
    DrawRectangle(0, 0, sw, sh, Color{0, 0, 0, 130});

    const int box_w = 560;
    const int box_h = 600;
    const int box_x = (sw - box_w) / 2;
    const int box_y = (sh - box_h) / 2;

    DrawRectangle(box_x, box_y, box_w, box_h, Color{22, 28, 48, 240});
    DrawRectangleLines(box_x, box_y, box_w, box_h, Color{160, 170, 220, 220});

    // Title
    const char* title = "YOU DIED";
    int title_size = 38;
    int title_w = MeasureText(title, title_size);
    DrawText(title, box_x + (box_w - title_w) / 2, box_y + 26, title_size,
             Color{255, 100, 100, 255});

    // Stats block
    int y = box_y + 95;
    char buf[128];

    std::snprintf(buf, sizeof(buf), "Final mass    %d", s.final_mass);
    DrawText(buf, box_x + 60, y, 22, RAYWHITE);
    y += 32;

    std::snprintf(buf, sizeof(buf), "Best combo    x%d", s.best_combo);
    DrawText(buf, box_x + 60, y, 22, RAYWHITE);
    y += 32;

    std::snprintf(buf, sizeof(buf), "Time alive    %.1fs", s.time_alive_sec);
    DrawText(buf, box_x + 60, y, 22, RAYWHITE);
    y += 46;

    // XP gain
    std::snprintf(buf, sizeof(buf), "+%d XP", s.xp_earned);
    int xp_size = 30;
    int xp_w = MeasureText(buf, xp_size);
    DrawText(buf, box_x + (box_w - xp_w) / 2, y, xp_size,
             Color{255, 220, 80, 255});
    y += 42;

    // Level
    if (s.level_after > s.level_before) {
        std::snprintf(buf, sizeof(buf), "LEVEL UP!  %d -> %d", s.level_before, s.level_after);
        int lw = MeasureText(buf, 26);
        DrawText(buf, box_x + (box_w - lw) / 2, y, 26, Color{120, 255, 180, 255});
    } else {
        std::snprintf(buf, sizeof(buf), "Level %d", s.level_after);
        int lw = MeasureText(buf, 22);
        DrawText(buf, box_x + (box_w - lw) / 2, y, 22, RAYWHITE);
    }
    y += 38;

    // XP bar -- deliberately partial so the bar always shows progress toward "next"
    const int bar_x = box_x + 70;
    const int bar_y = y;
    const int bar_w = box_w - 140;
    const int bar_h = 14;
    int xp_this_level = s.total_xp - s.xp_for_current_level;
    int xp_needed     = std::max(1, s.xp_for_next_level - s.xp_for_current_level);
    float frac        = std::clamp(static_cast<float>(xp_this_level) / static_cast<float>(xp_needed),
                                   0.0f, 1.0f);
    DrawRectangle(bar_x, bar_y, bar_w, bar_h, Color{40, 50, 70, 255});
    DrawRectangle(bar_x, bar_y, static_cast<int>(bar_w * frac), bar_h,
                  Color{255, 220, 80, 255});
    DrawRectangleLines(bar_x, bar_y, bar_w, bar_h, Color{180, 180, 200, 220});

    std::snprintf(buf, sizeof(buf), "%d / %d to next level", xp_this_level, xp_needed);
    int sub_w = MeasureText(buf, 14);
    DrawText(buf, box_x + (box_w - sub_w) / 2, bar_y + bar_h + 6, 14,
             Color{200, 200, 200, 200});

    // ---- Daily Missions block ----
    {
        int my = bar_y + bar_h + 32;
        const char* h = "DAILY MISSIONS";
        int hw_ = MeasureText(h, 16);
        DrawText(h, box_x + (box_w - hw_) / 2, my, 16, Color{255, 220, 120, 220});
        my += 22;

        for (int i = 0; i < kMissionCount; ++i) {
            const Mission& m = s.missions[i];
            if (m.kind == MissionKind::None) continue;

            const int row_h    = 22;
            const int row_pad  = 30;
            const int row_w    = box_w - row_pad * 2;
            const int row_x    = box_x + row_pad;

            char status[96];
            missionStatusText(m, status, sizeof(status));

            // Progress bar (filled, with completed missions filled to 100%)
            int p = std::min(m.progress, m.target);
            float frac = m.target > 0
                ? static_cast<float>(p) / static_cast<float>(m.target)
                : 0.0f;
            if (m.completed) frac = 1.0f;

            DrawRectangle(row_x, my + 14, row_w, 4, Color{40, 50, 70, 255});
            Color fill = m.completed
                ? Color{120, 220, 140, 230}
                : Color{220, 190, 100, 230};
            DrawRectangle(row_x, my + 14,
                          static_cast<int>(row_w * frac), 4, fill);

            Color text_c = m.completed
                ? Color{170, 240, 180, 230}
                : Color{220, 220, 230, 220};
            DrawText(status, row_x, my, 14, text_c);

            my += row_h;
        }
    }

    // ---- Buttons: PLAY AGAIN (primary, pulsing) + MAIN MENU (secondary) ----
    SummaryAction action = SummaryAction::None;

    const int btn_h    = 52;
    const int play_w   = 220;
    const int menu_w   = 160;
    const int btn_gap  = 18;
    const int total_w  = play_w + btn_gap + menu_w;
    const int btn_y    = box_y + box_h - btn_h - 50;
    const int play_x   = box_x + (box_w - total_w) / 2;
    const int menu_x   = play_x + play_w + btn_gap;

    // Slow pulse on Play Again to draw the eye -- the roadmap "big, pulsing button".
    float    t           = static_cast<float>(GetTime());
    float    pulse       = 0.5f + 0.5f * std::sin(t * 3.4f); // 0..1
    Color    play_fill   = Color{
        static_cast<unsigned char>(50  + pulse * 30),
        static_cast<unsigned char>(155 + pulse * 25),
        static_cast<unsigned char>(95  + pulse * 25),
        255
    };
    if (drawButton(Rectangle{(float)play_x, (float)btn_y, (float)play_w, (float)btn_h},
                   "PLAY AGAIN", 26,
                   play_fill, Color{255, 255, 255, 255})) {
        action = SummaryAction::PlayAgainNow;
    }

    if (drawButton(Rectangle{(float)menu_x, (float)btn_y, (float)menu_w, (float)btn_h},
                   "MAIN MENU", 20,
                   Color{60, 70, 100, 255}, Color{220, 225, 245, 255})) {
        action = SummaryAction::ReturnToMenu;
    }

    // Countdown hint -- if the player ignores the buttons, auto-queue still fires.
    std::snprintf(buf, sizeof(buf),
                  "auto-respawning in %.1fs   (SPACE to play again now)",
                  std::max(0.0f, s.remaining_sec));
    int hw = MeasureText(buf, 14);
    DrawText(buf, sw / 2 - hw / 2, box_y + box_h - 26, 14,
             Color{180, 190, 215, 200});

    return action;
}

SummaryAction Hud::renderPauseOverlay(int sw, int sh) {
    // Dim the world. Lighter than the death-summary dim so it still reads as
    // "temporarily suspended" rather than "game over".
    DrawRectangle(0, 0, sw, sh, Color{0, 0, 0, 150});

    const int box_w = 380;
    const int box_h = 280;
    const int box_x = (sw - box_w) / 2;
    const int box_y = (sh - box_h) / 2;

    DrawRectangleRounded(
        Rectangle{(float)box_x, (float)box_y, (float)box_w, (float)box_h},
        0.12f, 8, Color{22, 28, 48, 240});
    DrawRectangleRoundedLines(
        Rectangle{(float)box_x, (float)box_y, (float)box_w, (float)box_h},
        0.12f, 8, Color{160, 170, 220, 200});

    // Title -- gentle pulse so the screen doesn't feel frozen-dead.
    float t          = static_cast<float>(GetTime());
    float pulse      = 0.5f + 0.5f * std::sin(t * 2.2f);
    int   title_size = 44;
    const char* title = "PAUSED";
    int title_w = MeasureText(title, title_size);
    unsigned char a = static_cast<unsigned char>(200 + pulse * 55);
    DrawText(title, box_x + (box_w - title_w) / 2 + 2,
             box_y + 32 + 2, title_size, Color{0, 0, 0, 180});
    DrawText(title, box_x + (box_w - title_w) / 2,
             box_y + 32,     title_size, Color{255, 220, 130, a});

    // Buttons stack
    const int btn_w = 260;
    const int btn_h = 54;
    const int btn_x = box_x + (box_w - btn_w) / 2;
    int       btn_y = box_y + 110;

    SummaryAction action = SummaryAction::None;

    if (drawButton(Rectangle{(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h},
                   "RESUME", 26,
                   Color{55, 145, 95, 255}, Color{255, 255, 255, 255})) {
        action = SummaryAction::ResumeFromPause;
    }
    btn_y += btn_h + 14;

    if (drawButton(Rectangle{(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h},
                   "MAIN MENU", 22,
                   Color{60, 70, 100, 255}, Color{220, 225, 245, 255})) {
        action = SummaryAction::ReturnToMenu;
    }

    // Footer hint
    const char* hint = "ESC also resumes";
    int hint_w = MeasureText(hint, 14);
    DrawText(hint, box_x + (box_w - hint_w) / 2, box_y + box_h - 32, 14,
             Color{160, 175, 200, 200});

    return action;
}

} // namespace cr
