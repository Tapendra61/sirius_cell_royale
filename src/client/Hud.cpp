#include "Hud.h"

#include "UiWidgets.h"
#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cr {

namespace {

// Apply the global HUD text scale to a font size. Used by every DrawText /
// MeasureText call in this file. Keeping it inline + named short so the call
// sites stay readable.
inline int sc(int sz) {
    return static_cast<int>(sz * currentHudTextScale() + 0.5f);
}

} // namespace

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

void Hud::pushKillfeed(const KillfeedEntry& entry) {
    // Shift older entries down (index 0 = most recent). Drop the oldest if full.
    int keep = std::min(recent_kills_count_, kKillfeedMax - 1);
    for (int i = keep; i > 0; --i) {
        recent_kills_[i] = recent_kills_[i - 1];
    }
    recent_kills_[0] = entry;
    recent_kills_count_ = std::min(recent_kills_count_ + 1, kKillfeedMax);
}

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
        int fs = sc(36);
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
        int fs        = sc(base_size + extra + static_cast<int>(combo_flash_ * 12.0f));
        int tw        = MeasureText(buf, fs);
        Color shadow{0, 0, 0, 180};
        DrawText(buf, screen_w - tw - 22 + 2, 22 + 2, fs, shadow);
        unsigned char g = static_cast<unsigned char>(std::max(80, 220 - combo_count_ * 10));
        DrawText(buf, screen_w - tw - 22, 22, fs, Color{255, g, 60, 255});
    }

    // ----- Killfeed (top-right, under the combo counter) -----
    // Only rendered during play / death-cam, not under the summary or pause panels
    // (those have their own focus).
    if (phase == GamePhase::Playing || phase == GamePhase::DeathCam) {
        renderKillfeed(screen_w, screen_h, GetTime());
    }

    // ----- Black hole stamina bar -----
    // Shows only while the watched cell is hiding. Centered near the bottom so the
    // player's eyes aren't competing with the worldview. Bar drains to the left as
    // stamina runs out; auto-eject fires when it hits 0.
    if (watched && watched->mass > 0.0f && phase == GamePhase::Playing) {
        // We don't have direct access to CellSnap here, but the Cell struct in World
        // carries shield_until / hiding_in plus blackhole_stamina. Pull both off the
        // watched Cell pointer.
        // (watched is `const Cell*` from Client::render -> hud_.render.)
        if (watched->hiding_in != INVALID_ENTITY) {
            const int bar_w = sc(360);
            const int bar_h = sc(18);
            const int bar_x = screen_w / 2 - bar_w / 2;
            const int bar_y = screen_h - sc(72);
            float stamina = std::clamp(watched->blackhole_stamina, 0.0f, 1.0f);
            // Backdrop.
            DrawRectangle(bar_x - 2, bar_y - 2, bar_w + 4, bar_h + 4,
                          Color{0, 0, 0, 180});
            DrawRectangle(bar_x, bar_y, bar_w, bar_h, Color{30, 18, 50, 230});
            // Fill: shifts hue red as it nears empty.
            float k = stamina; // 1 = purple, 0 = red
            Color fill{
                static_cast<unsigned char>(200 - k * 60),
                static_cast<unsigned char>( 60 + k * 30),
                static_cast<unsigned char>( 90 + k * 130),
                255};
            DrawRectangle(bar_x, bar_y, static_cast<int>(bar_w * stamina), bar_h, fill);
            DrawRectangleLines(bar_x, bar_y, bar_w, bar_h, Color{220, 200, 240, 220});

            const char* label = "HIDING -- move cursor away to exit";
            int label_fs = sc(14);
            int lw = MeasureText(label, label_fs);
            DrawText(label, screen_w / 2 - lw / 2, bar_y - sc(20), label_fs,
                     Color{230, 210, 255, 230});
        }
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
    // Y offsets are also scaled so the bottom-anchored text doesn't slide off-screen
    // at large HUD scales.
    char buf[160];
    std::snprintf(buf, sizeof(buf), "FPS %d  Tick %u", fps, static_cast<unsigned>(tick));
    DrawText(buf, 12, screen_h - sc(22), sc(16), Color{200, 200, 200, 220});
    if (watched) {
        const char* mode = touch ? "touch" : "desktop";
        std::snprintf(buf, sizeof(buf),
                      "mass %.1f  pos (%.0f, %.0f)  zoom %.2f  dt_x%.2f  %s%s",
                      watched->mass, watched->pos.x, watched->pos.y, zoom, dt_mult, mode,
                      paused ? "  [PAUSED]" : "");
        DrawText(buf, 12, screen_h - sc(40), sc(16), Color{200, 200, 200, 220});
    }

    return summary_action;
}

SummaryAction Hud::renderSummary(int sw, int sh, const MatchSummary& s) {
    // Dim background
    DrawRectangle(0, 0, sw, sh, Color{0, 0, 0, 130});

    // The summary panel grows along with text scale so it keeps its proportions.
    // We scale every font size *and* every vertical advance/offset via sc(); the
    // box itself grows in height (not width, since width is dominated by row labels
    // that have plenty of horizontal breathing room).
    const int box_w = 560;
    const int box_h = sc(600);
    const int box_x = (sw - box_w) / 2;
    const int box_y = (sh - box_h) / 2;

    DrawRectangle(box_x, box_y, box_w, box_h, Color{22, 28, 48, 240});
    DrawRectangleLines(box_x, box_y, box_w, box_h, Color{160, 170, 220, 220});

    // Title
    const char* title = "YOU DIED";
    int title_size = sc(38);
    int title_w = MeasureText(title, title_size);
    DrawText(title, box_x + (box_w - title_w) / 2, box_y + sc(26), title_size,
             Color{255, 100, 100, 255});

    // Stats block
    int y = box_y + sc(95);
    char buf[128];
    const int stat_fs    = sc(22);
    const int stat_row_h = sc(32);

    std::snprintf(buf, sizeof(buf), "Peak mass     %d", s.final_mass);
    DrawText(buf, box_x + 60, y, stat_fs, RAYWHITE);
    y += stat_row_h;

    std::snprintf(buf, sizeof(buf), "Best combo    x%d", s.best_combo);
    DrawText(buf, box_x + 60, y, stat_fs, RAYWHITE);
    y += stat_row_h;

    std::snprintf(buf, sizeof(buf), "Time alive    %.1fs", s.time_alive_sec);
    DrawText(buf, box_x + 60, y, stat_fs, RAYWHITE);
    y += sc(46);

    // XP gain
    std::snprintf(buf, sizeof(buf), "+%d XP", s.xp_earned);
    int xp_size = sc(30);
    int xp_w = MeasureText(buf, xp_size);
    DrawText(buf, box_x + (box_w - xp_w) / 2, y, xp_size,
             Color{255, 220, 80, 255});
    y += sc(42);

    // Level
    if (s.level_after > s.level_before) {
        std::snprintf(buf, sizeof(buf), "LEVEL UP!  %d -> %d", s.level_before, s.level_after);
        int lvl_fs = sc(26);
        int lw = MeasureText(buf, lvl_fs);
        DrawText(buf, box_x + (box_w - lw) / 2, y, lvl_fs, Color{120, 255, 180, 255});
    } else {
        std::snprintf(buf, sizeof(buf), "Level %d", s.level_after);
        int lvl_fs = sc(22);
        int lw = MeasureText(buf, lvl_fs);
        DrawText(buf, box_x + (box_w - lw) / 2, y, lvl_fs, RAYWHITE);
    }
    y += sc(38);

    // XP bar -- deliberately partial so the bar always shows progress toward "next"
    const int bar_x = box_x + 70;
    const int bar_y = y;
    const int bar_w = box_w - 140;
    const int bar_h = sc(14);
    int xp_this_level = s.total_xp - s.xp_for_current_level;
    int xp_needed     = std::max(1, s.xp_for_next_level - s.xp_for_current_level);
    float frac        = std::clamp(static_cast<float>(xp_this_level) / static_cast<float>(xp_needed),
                                   0.0f, 1.0f);
    DrawRectangle(bar_x, bar_y, bar_w, bar_h, Color{40, 50, 70, 255});
    DrawRectangle(bar_x, bar_y, static_cast<int>(bar_w * frac), bar_h,
                  Color{255, 220, 80, 255});
    DrawRectangleLines(bar_x, bar_y, bar_w, bar_h, Color{180, 180, 200, 220});

    std::snprintf(buf, sizeof(buf), "%d / %d to next level", xp_this_level, xp_needed);
    int sub_fs = sc(14);
    int sub_w  = MeasureText(buf, sub_fs);
    DrawText(buf, box_x + (box_w - sub_w) / 2, bar_y + bar_h + sc(6), sub_fs,
             Color{200, 200, 200, 200});

    // ---- Daily Missions block ----
    {
        int my = bar_y + bar_h + sc(32);
        const char* h = "DAILY MISSIONS";
        int hdr_fs = sc(16);
        int hw_ = MeasureText(h, hdr_fs);
        DrawText(h, box_x + (box_w - hw_) / 2, my, hdr_fs, Color{255, 220, 120, 220});
        my += sc(22);

        const int row_h     = sc(22);
        const int row_pad   = 30;
        const int row_w     = box_w - row_pad * 2;
        const int row_x     = box_x + row_pad;
        const int row_fs    = sc(14);
        const int bar_y_off = sc(14);
        const int bar_thick = std::max(3, sc(4));

        for (int i = 0; i < kMissionCount; ++i) {
            const Mission& m = s.missions[i];
            if (m.kind == MissionKind::None) continue;

            char status[96];
            missionStatusText(m, status, sizeof(status));

            // Progress bar (filled, with completed missions filled to 100%)
            int p = std::min(m.progress, m.target);
            float frac = m.target > 0
                ? static_cast<float>(p) / static_cast<float>(m.target)
                : 0.0f;
            if (m.completed) frac = 1.0f;

            DrawRectangle(row_x, my + bar_y_off, row_w, bar_thick,
                          Color{40, 50, 70, 255});
            Color fill = m.completed
                ? Color{120, 220, 140, 230}
                : Color{220, 190, 100, 230};
            DrawRectangle(row_x, my + bar_y_off,
                          static_cast<int>(row_w * frac), bar_thick, fill);

            Color text_c = m.completed
                ? Color{170, 240, 180, 230}
                : Color{220, 220, 230, 220};
            DrawText(status, row_x, my, row_fs, text_c);

            my += row_h;
        }
    }

    // ---- Buttons: PLAY AGAIN (primary, pulsing) + MAIN MENU (secondary) ----
    // Buttons themselves are NOT scaled (their text would overflow the fixed widths);
    // we just scale the vertical gap so they don't collide with the missions block.
    SummaryAction action = SummaryAction::None;

    const int btn_h    = 52;
    const int play_w   = 220;
    const int menu_w   = 160;
    const int btn_gap  = 18;
    const int total_w  = play_w + btn_gap + menu_w;
    const int btn_y    = box_y + box_h - btn_h - sc(50);
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

    // Hint line under the buttons. Stats stay up until the player decides.
    const char* hint = "press SPACE to play again, or click above";
    int hint_fs = sc(14);
    int hw = MeasureText(hint, hint_fs);
    DrawText(hint, sw / 2 - hw / 2, box_y + box_h - sc(26), hint_fs,
             Color{180, 190, 215, 200});

    return action;
}

void Hud::renderKillfeed(int sw, int /*sh*/, double now_sec) const {
    if (recent_kills_count_ <= 0) return;

    // Layout below the combo counter top-right corner. Per-line height stays fixed
    // (not scaled by HUD text size) since this is more like a system feed than HUD
    // call-out -- consistent sizing across runs.
    constexpr int kLineHeight = 22;
    constexpr int kRowGap     = 4;
    constexpr int kMargin     = 22;
    constexpr int kArrowFontSize = 14;
    constexpr int kNameFontSize  = 16;
    // Top Y: clear the combo counter's max footprint at scale 1.30 (~y=22 + 83px
    // text) regardless of whether combo is showing -- the killfeed shouldn't shift
    // around when combo enters/leaves visibility, and the small slack keeps the two
    // from ever touching.
    int y = 120;

    for (int i = 0; i < recent_kills_count_; ++i) {
        const KillfeedEntry& e = recent_kills_[i];
        float age = static_cast<float>(now_sec - e.spawned_sec);
        // Entries live for 4s: full opacity for the first 3s, then fade out 1s.
        constexpr float kLifetime = 4.0f;
        constexpr float kFadeStart = 3.0f;
        if (age >= kLifetime) continue;
        float alpha = (age < kFadeStart) ? 1.0f : 1.0f - (age - kFadeStart);
        alpha = std::clamp(alpha, 0.0f, 1.0f);
        unsigned char a = static_cast<unsigned char>(alpha * 230.0f);

        // "PRED -> PREY" with the arrow in a neutral tint between the two names.
        const char* arrow = "ate";
        int arrow_w = MeasureText(arrow, kArrowFontSize);
        int pred_w  = MeasureText(e.predator, kNameFontSize);
        int prey_w  = MeasureText(e.prey,     kNameFontSize);
        int line_w  = pred_w + 10 + arrow_w + 10 + prey_w;

        int x = sw - line_w - kMargin;
        int row_y = y + (kLineHeight - kNameFontSize) / 2;

        // Slight dark backdrop so names read on busy worlds.
        DrawRectangle(x - 8, y, line_w + 16, kLineHeight,
                      Color{0, 0, 0, static_cast<unsigned char>(a * 0.35f)});

        Color pred_c = e.pred_color; pred_c.a = a;
        Color prey_c = e.prey_color; prey_c.a = a;
        Color mid_c{200, 200, 210, a};

        DrawText(e.predator, x,                       row_y,   kNameFontSize, pred_c);
        DrawText(arrow,      x + pred_w + 10,         row_y + 1, kArrowFontSize, mid_c);
        DrawText(e.prey,     x + pred_w + 10 + arrow_w + 10, row_y, kNameFontSize, prey_c);

        // Mild bracket bar for the player-involved row so it stands out.
        if (e.involves_player) {
            DrawRectangle(x - 12, y + 2, 3, kLineHeight - 4,
                          Color{255, 220, 130, a});
        }
        y += kLineHeight + kRowGap;
    }
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
    int   title_size = sc(44);
    const char* title = "PAUSED";
    int title_w = MeasureText(title, title_size);
    unsigned char a = static_cast<unsigned char>(200 + pulse * 55);
    DrawText(title, box_x + (box_w - title_w) / 2 + 2,
             box_y + sc(32) + 2, title_size, Color{0, 0, 0, 180});
    DrawText(title, box_x + (box_w - title_w) / 2,
             box_y + sc(32),     title_size, Color{255, 220, 130, a});

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
    int hint_fs = sc(14);
    int hint_w  = MeasureText(hint, hint_fs);
    DrawText(hint, box_x + (box_w - hint_w) / 2, box_y + box_h - sc(32), hint_fs,
             Color{160, 175, 200, 200});

    return action;
}

} // namespace cr
