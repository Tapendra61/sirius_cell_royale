#include "RoyaleMenu.h"

#include "UiWidgets.h"

#include <algorithm>
#include <cmath>

namespace cr {

namespace {

// Smooth triangle-wave 0..1 for breathing animations. Same easing curve as the
// main menu's title halo so the two screens feel like one design language.
float easeOutCubic(float t) {
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}
float breathe(float t, float period) {
    float p = std::fmod(t, period) / period;
    float tri = p < 0.5f ? (p * 2.0f) : (2.0f - p * 2.0f);
    return easeOutCubic(tri);
}

// Cool blue accent (vs. the main menu's warm gold). Royale is the multiplayer
// branch -- the palette shift signals "you've stepped into a different mode".
constexpr Color kAccent     = Color{ 90, 170, 240, 255};
constexpr Color kAccentSoft = Color{120, 195, 255, 255};

} // namespace

void RoyaleMenu::update(float frame_dt, int /*sw*/, int /*sh*/) {
    anim_time_ += frame_dt;
    if (global_toast_remaining_ > 0.0f) {
        global_toast_remaining_ -= frame_dt;
    }
}

RoyaleMenuAction RoyaleMenu::render(int sw, int sh) {
    const float ui = uiScale(sw, sh);

    // ---------- Backdrop ----------
    DrawRectangleGradientV(0, 0, sw, sh,
                           Color{ 8, 12, 22, 255},
                           Color{18, 26, 42, 255});

    // Side accent stripe (left-edge fade) tinted blue.
    DrawRectangleGradientH(0, 0, uiPx(sw, sh, 220), sh,
                           Color{kAccent.r, kAccent.g, kAccent.b, 35},
                           Color{kAccent.r, kAccent.g, kAccent.b, 0});

    // Top accent line.
    {
        const int edge = std::max(2, uiPx(sw, sh, 3));
        DrawRectangleGradientH(0, 0, sw, edge,
                               Color{kAccent.r, kAccent.g, kAccent.b, 0},
                               Color{kAccent.r, kAccent.g, kAccent.b, 90});
        DrawRectangleGradientH(0, 0, sw, edge,
                               Color{kAccent.r, kAccent.g, kAccent.b, 90},
                               Color{kAccent.r, kAccent.g, kAccent.b, 0});
    }

    // Slow-pulsing corner orbs (echo main menu's microscope mood).
    {
        float p = (std::sin(anim_time_ * 0.7f) + 1.0f) * 0.5f;
        const int corner = uiPx(sw, sh, 60);
        const float r0   = 4.0f * ui;
        const float rp   = 2.0f * ui;
        DrawCircle(sw - corner, corner, r0 + p * rp,
                   Color{kAccent.r, kAccent.g, kAccent.b,
                         static_cast<unsigned char>(60 + p * 90)});
        DrawCircle(uiPx(sw, sh, 40), sh - corner, r0 + (1.0f - p) * rp,
                   Color{kAccent.r, kAccent.g, kAccent.b,
                         static_cast<unsigned char>(60 + (1.0f - p) * 90)});
    }

    // Vignette (top + bottom + sides).
    {
        const int vt = uiPx(sw, sh, 200);
        const int vb = uiPx(sw, sh, 180);
        const int vs = uiPx(sw, sh, 150);
        DrawRectangleGradientV(0, 0, sw, vt,
                               Color{0, 0, 0, 160}, Color{0, 0, 0, 0});
        DrawRectangleGradientV(0, sh - vb, sw, vb,
                               Color{0, 0, 0, 0}, Color{0, 0, 0, 180});
        DrawRectangleGradientH(0, 0, vs, sh,
                               Color{0, 0, 0, 110}, Color{0, 0, 0, 0});
        DrawRectangleGradientH(sw - vs, 0, vs, sh,
                               Color{0, 0, 0, 0}, Color{0, 0, 0, 110});
    }

    // ---------- Eyebrow tag ----------
    {
        const char* eyebrow = "MULTIPLAYER  --  CHOOSE  YOUR  ARENA";
        const int e_size = uiPx(sw, sh, 12);
        const int ew     = MeasureText(eyebrow, e_size);
        const int ey     = static_cast<int>(sh * 0.10f);
        const int line_w = uiPx(sw, sh, 60);
        const int line_g = uiPx(sw, sh, 10);
        const int line_o = uiPx(sw, sh, 70);
        DrawRectangle((sw - ew) / 2 - line_o, ey + e_size / 2, line_w, 1,
                      Color{180, 210, 240, 180});
        DrawRectangle((sw + ew) / 2 + line_g, ey + e_size / 2, line_w, 1,
                      Color{180, 210, 240, 180});
        DrawText(eyebrow, (sw - ew) / 2, ey, e_size,
                 Color{180, 210, 240, 220});
    }

    // ---------- Title halo (breathing alpha, no size pulse) ----------
    {
        const float cx     = sw * 0.5f;
        const float cy     = sh * 0.21f;
        const float p      = breathe(anim_time_, 5.5f);
        const float halo_a = 35.0f + 25.0f * p;
        const Color halo   = Color{kAccentSoft.r, kAccentSoft.g, kAccentSoft.b,
                                   static_cast<unsigned char>(halo_a)};
        DrawCircle(static_cast<int>(cx), static_cast<int>(cy), 380.0f * ui,
                   Color{halo.r, halo.g, halo.b,
                         static_cast<unsigned char>(halo.a * 0.35f)});
        DrawCircle(static_cast<int>(cx), static_cast<int>(cy), 280.0f * ui,
                   Color{halo.r, halo.g, halo.b,
                         static_cast<unsigned char>(halo.a * 0.55f)});
        DrawCircle(static_cast<int>(cx), static_cast<int>(cy), 180.0f * ui,
                   Color{halo.r, halo.g, halo.b,
                         static_cast<unsigned char>(halo.a * 0.85f)});
        DrawCircle(static_cast<int>(cx), static_cast<int>(cy), 110.0f * ui, halo);
    }

    // ---------- Title ----------
    const char* title = "ROYALE";
    const int   t_size = uiPx(sw, sh, 84);
    const int   tw     = MeasureText(title, t_size);
    const int   ty     = static_cast<int>(sh * 0.15f);
    const int   sh_a   = std::max(1, uiPx(sw, sh, 7));
    const int   sh_b   = std::max(1, uiPx(sw, sh, 9));
    const int   sh_c   = std::max(1, uiPx(sw, sh, 3));
    const int   sh_d   = std::max(1, uiPx(sw, sh, 4));
    DrawText(title, (sw - tw) / 2 + sh_a, ty + sh_b, t_size, Color{0, 0, 0, 110});
    DrawText(title, (sw - tw) / 2 + sh_c, ty + sh_d, t_size, Color{0, 0, 0, 200});
    DrawText(title, (sw - tw) / 2,        ty,        t_size, Color{210, 230, 255, 255});

    // Breathing accent underline.
    {
        float p = breathe(anim_time_, 5.5f);
        unsigned char a = static_cast<unsigned char>(120 + p * 70);
        const int ul_inset = uiPx(sw, sh, 20);
        const int ul_y     = ty + t_size + uiPx(sw, sh, 4);
        const int ul_h     = std::max(2, uiPx(sw, sh, 2));
        DrawRectangleGradientH((sw - tw) / 2 + ul_inset, ul_y,
                               tw - 2 * ul_inset, ul_h,
                               Color{kAccentSoft.r, kAccentSoft.g, kAccentSoft.b, 0},
                               Color{kAccentSoft.r, kAccentSoft.g, kAccentSoft.b, a});
        DrawRectangleGradientH((sw - tw) / 2 + ul_inset, ul_y,
                               tw - 2 * ul_inset, ul_h,
                               Color{kAccentSoft.r, kAccentSoft.g, kAccentSoft.b, a},
                               Color{kAccentSoft.r, kAccentSoft.g, kAccentSoft.b, 0});
    }

    // Tagline.
    {
        const char* tagline = "two flavours of arena combat";
        int g_size = uiPx(sw, sh, 17);
        int gw     = MeasureText(tagline, g_size);
        int gy     = ty + t_size + uiPx(sw, sh, 24);
        DrawText(tagline, (sw - gw) / 2 + 1, gy + 1, g_size, Color{0, 0, 0, 150});
        DrawText(tagline, (sw - gw) / 2,     gy,     g_size, Color{185, 205, 230, 220});
    }

    // ---------- Mode cards ----------
    RoyaleMenuAction action = RoyaleMenuAction::None;
    const int card_w   = uiPx(sw, sh, 320);
    const int card_h   = uiPx(sw, sh, 200);
    const int card_gap = uiPx(sw, sh,  30);
    const int card_y   = static_cast<int>(sh * 0.43f);
    const int total_w  = card_w * 2 + card_gap;
    const int left_x   = (sw - total_w) / 2;
    const int right_x  = left_x + card_w + card_gap;

    // Common scaled paddings.
    const int pad_x      = uiPx(sw, sh, 24);
    const int pad_top    = uiPx(sw, sh, 24);
    const int header_fs  = uiPx(sw, sh, 30);
    const int badge_fs   = uiPx(sw, sh, 12);
    const int desc_fs_a  = uiPx(sw, sh, 15);
    const int desc_fs_b  = uiPx(sw, sh, 13);
    const int row1_y     = uiPx(sw, sh, 76);
    const int row2_y     = uiPx(sw, sh, 98);
    const int row3_y     = uiPx(sw, sh, 118);
    const int btn_fs     = uiPx(sw, sh, 18);
    const int btn_h      = uiPx(sw, sh, 40);
    const int btn_bot    = uiPx(sw, sh, 56);
    const int pip_off_r  = uiPx(sw, sh, 26);
    const int pip_off_y  = uiPx(sw, sh, 36);
    const float pip_r    = std::max(3.0f, 5.0f * ui);

    // --- LOCAL card (primary) ---
    {
        Rectangle r{(float)left_x, (float)card_y, (float)card_w, (float)card_h};
        DrawRectangleGradientV(left_x, card_y, card_w, card_h,
                               Color{30, 60, 80, 240},
                               Color{18, 36, 56, 240});
        DrawRectangleRoundedLines(r, 0.06f, 8, Color{120, 180, 220, 130});

        DrawRectangle(left_x + uiPx(sw, sh, 14), card_y + uiPx(sw, sh, 14),
                      card_w - uiPx(sw, sh, 28), 1, Color{120, 180, 220, 90});

        DrawText("LOCAL", left_x + pad_x, card_y + pad_top, header_fs,
                 Color{220, 240, 250, 240});
        DrawCircle(left_x + card_w - pip_off_r, card_y + pip_off_y, pip_r,
                   Color{120, 220, 150, 230});
        DrawText("LIVE", left_x + card_w - uiPx(sw, sh, 64),
                 card_y + uiPx(sw, sh, 28), badge_fs,
                 Color{170, 220, 180, 230});

        DrawText("Host or join a LAN match.",
                 left_x + pad_x, card_y + row1_y, desc_fs_a,
                 Color{200, 215, 230, 230});
        DrawText("Same-network friends, up to 16 players.",
                 left_x + pad_x, card_y + row2_y, desc_fs_b,
                 Color{170, 190, 210, 210});
        DrawText("No login, no servers, no fuss.",
                 left_x + pad_x, card_y + row3_y, desc_fs_b,
                 Color{170, 190, 210, 210});

        Rectangle btn_r{(float)(left_x + pad_x),
                        (float)(card_y + card_h - btn_bot),
                        (float)(card_w - 2 * pad_x), (float)btn_h};
        if (drawButton(btn_r, "OPEN  LOBBY  >", btn_fs,
                       Color{55, 130, 110, 255},
                       Color{240, 250, 245, 255})) {
            action = RoyaleMenuAction::ShowLocalLobby;
        }
    }

    // --- GLOBAL card (placeholder) ---
    {
        Rectangle r{(float)right_x, (float)card_y, (float)card_w, (float)card_h};
        DrawRectangleGradientV(right_x, card_y, card_w, card_h,
                               Color{22, 28, 48, 230},
                               Color{14, 18, 32, 230});
        DrawRectangleRoundedLines(r, 0.06f, 8, Color{100, 120, 160, 80});

        DrawRectangle(right_x + uiPx(sw, sh, 14), card_y + uiPx(sw, sh, 14),
                      card_w - uiPx(sw, sh, 28), 1, Color{100, 120, 160, 70});

        DrawText("GLOBAL", right_x + pad_x, card_y + pad_top, header_fs,
                 Color{180, 195, 220, 200});
        DrawCircle(right_x + card_w - pip_off_r, card_y + pip_off_y, pip_r,
                   Color{220, 180, 90, 220});
        DrawText("SOON", right_x + card_w - uiPx(sw, sh, 64),
                 card_y + uiPx(sw, sh, 28), badge_fs,
                 Color{220, 195, 130, 220});

        DrawText("Matchmaking across the internet.",
                 right_x + pad_x, card_y + row1_y, desc_fs_a,
                 Color{180, 195, 215, 200});
        DrawText("Ranked play, persistent profiles.",
                 right_x + pad_x, card_y + row2_y, desc_fs_b,
                 Color{150, 170, 200, 180});
        DrawText("Lands with Phase 10.",
                 right_x + pad_x, card_y + row3_y, desc_fs_b,
                 Color{150, 170, 200, 180});

        Rectangle btn_r{(float)(right_x + pad_x),
                        (float)(card_y + card_h - btn_bot),
                        (float)(card_w - 2 * pad_x), (float)btn_h};
        if (drawButton(btn_r, "PREVIEW  >", btn_fs,
                       Color{52, 62, 92, 255},
                       Color{200, 210, 235, 230})) {
            global_toast_remaining_ = 2.8f;
        }
    }

    // ---------- BACK button ----------
    {
        const int back_w  = uiPx(sw, sh, 200);
        const int back_h  = uiPx(sw, sh, 50);
        const int back_fs = uiPx(sw, sh, 22);
        Rectangle bb{(float)(sw / 2 - back_w / 2),
                     (float)(card_y + card_h + uiPx(sw, sh, 30)),
                     (float)back_w, (float)back_h};
        if (drawButton(bb, "BACK", back_fs,
                       Color{42, 50, 72, 255}, Color{220, 225, 245, 230})) {
            action = RoyaleMenuAction::BackToMainMenu;
        }
    }

    // ---------- "Coming soon" toast (Global) ----------
    if (global_toast_remaining_ > 0.0f) {
        const char* msg =
            "Global matchmaking ships with Phase 10. Use LOCAL for LAN games now.";
        int m_fs = uiPx(sw, sh, 16);
        int mw   = MeasureText(msg, m_fs);
        float k  = std::clamp(global_toast_remaining_ / 2.8f, 0.0f, 1.0f);
        float fade = std::min(k * 2.0f, (1.0f - k) * 3.0f + 0.5f);
        fade = std::clamp(fade, 0.0f, 1.0f);
        unsigned char a = static_cast<unsigned char>(fade * 230);
        int pad   = uiPx(sw, sh, 18);
        int box_x = sw / 2 - mw / 2 - pad;
        int box_y = sh - uiPx(sw, sh, 110);
        int box_w = mw + pad * 2;
        int box_h = m_fs + pad + uiPx(sw, sh, 4);
        Rectangle box{(float)box_x, (float)box_y, (float)box_w, (float)box_h};
        DrawRectangleGradientV(box_x, box_y, box_w, box_h,
                               Color{40, 55, 85, a},
                               Color{24, 32, 56, a});
        DrawRectangleRoundedLines(box, 0.4f, 6,
                                  Color{180, 200, 240, (unsigned char)(a * 0.85f)});
        DrawCircle(box_x + uiPx(sw, sh, 16), box_y + box_h / 2,
                   std::max(3.0f, 4.0f * ui),
                   Color{220, 180, 90, a});
        DrawText(msg, box_x + pad + uiPx(sw, sh, 14),
                 box_y + pad / 2 + uiPx(sw, sh, 4), m_fs,
                 Color{225, 235, 250, a});
    }

    // ---------- Footer ----------
    {
        const char* foot = "press  ESC  to  go  back";
        int fs = uiPx(sw, sh, 13);
        int fw = MeasureText(foot, fs);
        DrawText(foot, (sw - fw) / 2, sh - uiPx(sw, sh, 30), fs,
                 Color{140, 155, 180, 180});
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        action = RoyaleMenuAction::BackToMainMenu;
    }

    return action;
}

} // namespace cr
