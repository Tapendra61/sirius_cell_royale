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

    // Secondary: Royale -- opens the Local / Global sub-menu. Local is the
    // shippable multiplayer mode; Global is still a placeholder (server-backed
    // matchmaking) shown inside the sub-menu.
    if (drawButtonWithSub(
            Rectangle{(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h},
            "ROYALE", 30,
            "multiplayer  --  local or global", 13,
            Color{75, 90, 160, 255},
            Color{225, 230, 250, 255})) {
        action = MenuAction::ShowRoyaleMenu;
    }
    btn_y += btn_h + 22;

    // Tertiary row: SETTINGS + TUTORIAL side-by-side, each half-width. Same total
    // width as the primary buttons above so the column reads as a single stack.
    {
        const int gap   = 12;
        const int sub_w = (btn_w - gap) / 2;
        if (drawButton(
                Rectangle{(float)btn_x, (float)btn_y, (float)sub_w, 52.0f},
                "SETTINGS", 22,
                Color{42, 50, 72, 255}, Color{220, 225, 245, 230})) {
            action = MenuAction::ShowSettings;
        }
        if (drawButton(
                Rectangle{(float)(btn_x + sub_w + gap), (float)btn_y,
                          (float)sub_w, 52.0f},
                "TUTORIAL", 22,
                Color{42, 50, 72, 255}, Color{220, 225, 245, 230})) {
            action = MenuAction::ReplayIntro;
        }
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
