#include "MainMenu.h"

#include "UiWidgets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace cr {

namespace {

constexpr int   kBgCellCount  = 26;
constexpr float kBgMinRadius  = 28.0f;
constexpr float kBgMaxRadius  = 130.0f;
constexpr float kBgSpeedRange = 24.0f;

// Local PRNG only for visual decoration -- no need to share the sim's RNG. Static state
// is fine because the background is purely cosmetic and not part of any save.
uint32_t s_menu_rng = 0x9E3779B9u;
uint32_t menuRand() {
    s_menu_rng ^= s_menu_rng << 13;
    s_menu_rng ^= s_menu_rng >> 17;
    s_menu_rng ^= s_menu_rng << 5;
    return s_menu_rng;
}
float menuRandRange(float lo, float hi) {
    return lo + (hi - lo) * (menuRand() & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

Color menuRandCellColor() {
    static const Color palette[] = {
        Color{ 80, 180, 240,  90},
        Color{220, 100, 140,  90},
        Color{240, 200,  90,  90},
        Color{140, 220, 140,  90},
        Color{200, 130, 230,  90},
        Color{255, 160,  90,  90},
        Color{120, 200, 220,  90},
    };
    return palette[menuRand() % (sizeof(palette) / sizeof(palette[0]))];
}

} // namespace

MainMenu::MainMenu() = default;

void MainMenu::ensureBgInit(int sw, int sh) {
    if (bg_inited_ && bg_init_w_ == sw && bg_init_h_ == sh) return;
    bg_cells_.clear();
    bg_cells_.reserve(kBgCellCount);
    for (int i = 0; i < kBgCellCount; ++i) {
        bg_cells_.push_back(BgCell{
            menuRandRange(0.0f, static_cast<float>(sw)),
            menuRandRange(0.0f, static_cast<float>(sh)),
            menuRandRange(-kBgSpeedRange, kBgSpeedRange),
            menuRandRange(-kBgSpeedRange, kBgSpeedRange),
            menuRandRange(kBgMinRadius, kBgMaxRadius),
            menuRandCellColor()
        });
    }
    bg_inited_ = true;
    bg_init_w_ = sw;
    bg_init_h_ = sh;
}

void MainMenu::update(float frame_dt, int sw, int sh) {
    ensureBgInit(sw, sh);
    anim_time_ += frame_dt;
    if (coming_soon_remaining_ > 0.0f) {
        coming_soon_remaining_ -= frame_dt;
    }

    // Drift the bg cells around. Wrap with margin so a cell never visually pops in.
    for (auto& c : bg_cells_) {
        c.x += c.vx * frame_dt;
        c.y += c.vy * frame_dt;
        if (c.x < -c.r)        c.x = sw + c.r;
        if (c.x >  sw + c.r)   c.x = -c.r;
        if (c.y < -c.r)        c.y = sh + c.r;
        if (c.y >  sh + c.r)   c.y = -c.r;
    }
}

MenuAction MainMenu::render(int sw, int sh, const SaveData& save) {
    // ---- Animated background ----
    for (const auto& c : bg_cells_) {
        DrawCircle(static_cast<int>(c.x), static_cast<int>(c.y), c.r, c.color);
        DrawCircleLines(static_cast<int>(c.x), static_cast<int>(c.y), c.r,
                        Color{c.color.r, c.color.g, c.color.b, 70});
    }
    // Subtle vignette so foreground text reads on busy bg.
    DrawRectangle(0, 0, sw, sh, Color{12, 16, 24, 130});

    // ---- Title ----
    const char* title = "CELL ROYALE";
    int   t_base = 92;
    float pulse  = 1.0f + 0.035f * std::sin(anim_time_ * 1.6f);
    int   t_size = static_cast<int>(t_base * pulse);
    int   tw     = MeasureText(title, t_size);
    int   ty     = static_cast<int>(sh * 0.16f);
    DrawText(title, (sw - tw) / 2 + 5, ty + 5, t_size, Color{0, 0, 0, 200});
    DrawText(title, (sw - tw) / 2,     ty,     t_size, Color{255, 215, 130, 255});

    // ---- Tagline ----
    const char* tagline = "eat.   grow.   survive.";
    int g_size = 22;
    int gw = MeasureText(tagline, g_size);
    DrawText(tagline, (sw - gw) / 2, ty + t_size + 14, g_size,
             Color{200, 205, 225, 210});

    // ---- Buttons ----
    const int btn_w = 340;
    const int btn_h = 76;
    const int btn_x = (sw - btn_w) / 2;
    int       btn_y = static_cast<int>(sh * 0.52f);

    MenuAction action = MenuAction::None;

    // Primary: VS AI (warm green-gold)
    if (drawButton(Rectangle{(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h},
                   "VS AI", 34,
                   Color{55, 145, 95, 255},
                   Color{255, 255, 255, 255})) {
        action = MenuAction::StartVsAI;
    }
    btn_y += btn_h + 22;

    // Secondary: Royale (placeholder, dim)
    if (drawButtonWithSub(
            Rectangle{(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h},
            "ROYALE", 30,
            "multiplayer  --  coming soon", 13,
            Color{52, 62, 92, 255},
            Color{220, 225, 245, 255})) {
        coming_soon_remaining_ = 2.4f;
    }
    btn_y += btn_h + 22;

    // Tertiary: Settings -- smaller height, neutral dark
    if (drawButton(
            Rectangle{(float)btn_x, (float)btn_y, (float)btn_w, 52.0f},
            "SETTINGS", 22,
            Color{42, 50, 72, 255}, Color{220, 225, 245, 230})) {
        action = MenuAction::ShowSettings;
    }

    // ---- "Coming soon" toast ----
    if (coming_soon_remaining_ > 0.0f) {
        const char* msg = "Multiplayer is a placeholder right now -- coming in Phase 10.";
        int m_fs = 18;
        int mw   = MeasureText(msg, m_fs);
        float k  = std::clamp(coming_soon_remaining_ / 2.4f, 0.0f, 1.0f);
        // Fade in/out: full opacity in middle, fade at start/end.
        float fade = std::min(k * 2.0f, (1.0f - k) * 3.0f + 0.5f);
        fade = std::clamp(fade, 0.0f, 1.0f);
        unsigned char a = static_cast<unsigned char>(fade * 230);
        int pad = 18;
        int box_x = sw / 2 - mw / 2 - pad;
        int box_y = sh - 130;
        Rectangle box{(float)box_x, (float)box_y, (float)(mw + pad * 2),
                      (float)(m_fs + pad)};
        DrawRectangleRounded(box, 0.4f, 6, Color{60, 30, 30, a});
        DrawRectangleRoundedLines(box, 0.4f, 6, Color{255, 180, 180, (unsigned char)(a * 0.7f)});
        DrawText(msg, box_x + pad, box_y + pad / 2 + 2, m_fs,
                 Color{255, 220, 220, a});
    }

    // ---- Lifetime stats panel (top-right) ----
    {
        char line1[96], line2[160];
        std::snprintf(line1, sizeof(line1), "Level %u    %u XP",
                      save.level, save.total_xp);
        std::snprintf(line2, sizeof(line2),
                      "Best mass %.0f    Best combo x%u    %u game%s",
                      save.best_mass, save.best_combo,
                      save.games_played, save.games_played == 1 ? "" : "s");
        int l1w = MeasureText(line1, 20);
        int l2w = MeasureText(line2, 14);
        int panel_w = std::max(l1w, l2w) + 28;
        int panel_h = save.games_played > 0 ? 62 : 36;
        int panel_x = sw - panel_w - 20;
        int panel_y = 20;
        DrawRectangleRounded(
            Rectangle{(float)panel_x, (float)panel_y,
                      (float)panel_w, (float)panel_h},
            0.25f, 6, Color{20, 26, 42, 200});
        DrawRectangleRoundedLines(
            Rectangle{(float)panel_x, (float)panel_y,
                      (float)panel_w, (float)panel_h},
            0.25f, 6, Color{255, 255, 255, 50});
        DrawText(line1, panel_x + 14, panel_y + 8, 20,
                 Color{255, 220, 130, 240});
        if (save.games_played > 0) {
            DrawText(line2, panel_x + 14, panel_y + 36, 14,
                     Color{200, 210, 230, 210});
        }
    }

    // ---- Footer hint ----
    {
        const char* foot = "press ESC to quit";
        int fs = 14;
        int fw = MeasureText(foot, fs);
        DrawText(foot, (sw - fw) / 2, sh - 30, fs,
                 Color{150, 160, 180, 200});
    }

    // ---- Keyboard escape ----
    if (IsKeyPressed(KEY_ESCAPE)) {
        action = MenuAction::Quit;
    }
    // Quick-start with Enter / Space for keyboard players.
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
        action = MenuAction::StartVsAI;
    }

    return action;
}

} // namespace cr
