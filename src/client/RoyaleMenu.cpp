#include "RoyaleMenu.h"

#include "UiWidgets.h"

#include <algorithm>
#include <cmath>

namespace cr {

void RoyaleMenu::update(float frame_dt, int /*sw*/, int /*sh*/) {
    anim_time_ += frame_dt;
    if (global_toast_remaining_ > 0.0f) {
        global_toast_remaining_ -= frame_dt;
    }
}

RoyaleMenuAction RoyaleMenu::render(int sw, int sh) {
    // Matches the main menu's vignette so the transition between screens reads as
    // a continuation, not a hard cut.
    DrawRectangle(0, 0, sw, sh, Color{12, 16, 24, 230});

    // ---- Title ----
    const char* title = "ROYALE";
    int   t_base = 80;
    float pulse  = 1.0f + 0.03f * std::sin(anim_time_ * 1.6f);
    int   t_size = static_cast<int>(t_base * pulse);
    int   tw     = MeasureText(title, t_size);
    int   ty     = static_cast<int>(sh * 0.18f);
    DrawText(title, (sw - tw) / 2 + 4, ty + 4, t_size, Color{0, 0, 0, 200});
    DrawText(title, (sw - tw) / 2,     ty,     t_size, Color{200, 220, 255, 255});

    const char* tagline = "pick a multiplayer flavor";
    int g_size = 20;
    int gw = MeasureText(tagline, g_size);
    DrawText(tagline, (sw - gw) / 2, ty + t_size + 12, g_size,
             Color{180, 195, 220, 200});

    // ---- Buttons ----
    const int btn_w = 340;
    const int btn_h = 80;
    const int btn_x = (sw - btn_w) / 2;
    int       btn_y = static_cast<int>(sh * 0.46f);

    RoyaleMenuAction action = RoyaleMenuAction::None;

    // Primary: Local. Active button -- LAN host/join lives here.
    if (drawButtonWithSub(
            Rectangle{(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h},
            "LOCAL", 32,
            "host or join a game on your network", 13,
            Color{55, 130, 110, 255},
            Color{240, 250, 245, 255})) {
        action = RoyaleMenuAction::ShowLocalLobby;
    }
    btn_y += btn_h + 22;

    // Secondary: Global (placeholder). Dim, sub-label calls out the status; clicking
    // triggers the inline toast rather than navigating anywhere.
    if (drawButtonWithSub(
            Rectangle{(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h},
            "GLOBAL", 32,
            "matchmaking  --  coming soon", 13,
            Color{52, 62, 92, 255},
            Color{200, 210, 235, 255})) {
        global_toast_remaining_ = 2.4f;
    }
    btn_y += btn_h + 30;

    // BACK row -- half-width centered so it doesn't compete with the primary picks.
    {
        const int back_w = (btn_w - 12) / 2;
        const int back_x = (sw - back_w) / 2;
        if (drawButton(
                Rectangle{(float)back_x, (float)btn_y, (float)back_w, 52.0f},
                "BACK", 22,
                Color{42, 50, 72, 255}, Color{220, 225, 245, 230})) {
            action = RoyaleMenuAction::BackToMainMenu;
        }
    }

    // ---- "Coming soon" toast (Global) ----
    if (global_toast_remaining_ > 0.0f) {
        const char* msg =
            "Global matchmaking ships with Phase 10. Use LOCAL for LAN games now.";
        int m_fs = 16;
        int mw   = MeasureText(msg, m_fs);
        float k  = std::clamp(global_toast_remaining_ / 2.4f, 0.0f, 1.0f);
        float fade = std::min(k * 2.0f, (1.0f - k) * 3.0f + 0.5f);
        fade = std::clamp(fade, 0.0f, 1.0f);
        unsigned char a = static_cast<unsigned char>(fade * 230);
        int pad = 16;
        int box_x = sw / 2 - mw / 2 - pad;
        int box_y = sh - 130;
        Rectangle box{(float)box_x, (float)box_y,
                      (float)(mw + pad * 2), (float)(m_fs + pad)};
        DrawRectangleRounded(box, 0.4f, 6, Color{40, 50, 80, a});
        DrawRectangleRoundedLines(box, 0.4f, 6,
                                  Color{180, 200, 240, (unsigned char)(a * 0.7f)});
        DrawText(msg, box_x + pad, box_y + pad / 2 + 2, m_fs,
                 Color{220, 230, 250, a});
    }

    // ---- Footer ----
    {
        const char* foot = "press ESC to go back";
        int fs = 14;
        int fw = MeasureText(foot, fs);
        DrawText(foot, (sw - fw) / 2, sh - 30, fs,
                 Color{150, 160, 180, 200});
    }

    // ---- Keyboard shortcuts ----
    if (IsKeyPressed(KEY_ESCAPE)) {
        action = RoyaleMenuAction::BackToMainMenu;
    }

    return action;
}

} // namespace cr
