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
    // ---------- Backdrop ----------
    // Vertical gradient base in the same mood as the main menu but cooler --
    // blue Royale palette signals "you've crossed into multiplayer territory".
    DrawRectangleGradientV(0, 0, sw, sh,
                           Color{ 8, 12, 22, 255},
                           Color{18, 26, 42, 255});

    // Side accent stripe (left-edge fade) tinted blue.
    DrawRectangleGradientH(0, 0, 220, sh,
                           Color{kAccent.r, kAccent.g, kAccent.b, 35},
                           Color{kAccent.r, kAccent.g, kAccent.b, 0});

    // Top accent line.
    DrawRectangleGradientH(0, 0, sw, 3,
                           Color{kAccent.r, kAccent.g, kAccent.b, 0},
                           Color{kAccent.r, kAccent.g, kAccent.b, 90});
    DrawRectangleGradientH(0, 0, sw, 3,
                           Color{kAccent.r, kAccent.g, kAccent.b, 90},
                           Color{kAccent.r, kAccent.g, kAccent.b, 0});

    // Slow-pulsing corner orbs (echo main menu's microscope mood).
    {
        float p = (std::sin(anim_time_ * 0.7f) + 1.0f) * 0.5f;
        DrawCircle(sw - 60, 60, 4.0f + p * 2.0f,
                   Color{kAccent.r, kAccent.g, kAccent.b,
                         static_cast<unsigned char>(60 + p * 90)});
        DrawCircle(40, sh - 60, 4.0f + (1.0f - p) * 2.0f,
                   Color{kAccent.r, kAccent.g, kAccent.b,
                         static_cast<unsigned char>(60 + (1.0f - p) * 90)});
    }

    // Vignette (top + bottom + sides).
    DrawRectangleGradientV(0, 0, sw, 200,
                           Color{0, 0, 0, 160}, Color{0, 0, 0, 0});
    DrawRectangleGradientV(0, sh - 180, sw, 180,
                           Color{0, 0, 0, 0}, Color{0, 0, 0, 180});
    DrawRectangleGradientH(0, 0, 150, sh,
                           Color{0, 0, 0, 110}, Color{0, 0, 0, 0});
    DrawRectangleGradientH(sw - 150, 0, 150, sh,
                           Color{0, 0, 0, 0}, Color{0, 0, 0, 110});

    // ---------- Eyebrow tag ----------
    {
        const char* eyebrow = "MULTIPLAYER  --  CHOOSE  YOUR  ARENA";
        int e_size = 12;
        int ew = MeasureText(eyebrow, e_size);
        int ey = static_cast<int>(sh * 0.10f);
        DrawRectangle((sw - ew) / 2 - 70, ey + e_size / 2, 60, 1,
                      Color{180, 210, 240, 180});
        DrawRectangle((sw + ew) / 2 + 10, ey + e_size / 2, 60, 1,
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
        DrawCircle(static_cast<int>(cx), static_cast<int>(cy),
                   380.0f,
                   Color{halo.r, halo.g, halo.b,
                         static_cast<unsigned char>(halo.a * 0.35f)});
        DrawCircle(static_cast<int>(cx), static_cast<int>(cy),
                   280.0f,
                   Color{halo.r, halo.g, halo.b,
                         static_cast<unsigned char>(halo.a * 0.55f)});
        DrawCircle(static_cast<int>(cx), static_cast<int>(cy),
                   180.0f,
                   Color{halo.r, halo.g, halo.b,
                         static_cast<unsigned char>(halo.a * 0.85f)});
        DrawCircle(static_cast<int>(cx), static_cast<int>(cy), 110.0f, halo);
    }

    // ---------- Title ----------
    // Fixed-size (no per-frame integer-pixel jitter).
    const char* title = "ROYALE";
    const int   t_size = 84;
    const int   tw     = MeasureText(title, t_size);
    const int   ty     = static_cast<int>(sh * 0.15f);
    DrawText(title, (sw - tw) / 2 + 7, ty + 9, t_size, Color{0, 0, 0, 110});
    DrawText(title, (sw - tw) / 2 + 3, ty + 4, t_size, Color{0, 0, 0, 200});
    DrawText(title, (sw - tw) / 2,     ty,     t_size, Color{210, 230, 255, 255});

    // Breathing accent underline (same period as halo so they're synced).
    {
        float p = breathe(anim_time_, 5.5f);
        unsigned char a = static_cast<unsigned char>(120 + p * 70);
        DrawRectangleGradientH((sw - tw) / 2 + 20, ty + t_size + 4,
                               tw - 40, 2,
                               Color{kAccentSoft.r, kAccentSoft.g, kAccentSoft.b, 0},
                               Color{kAccentSoft.r, kAccentSoft.g, kAccentSoft.b, a});
        DrawRectangleGradientH((sw - tw) / 2 + 20, ty + t_size + 4,
                               tw - 40, 2,
                               Color{kAccentSoft.r, kAccentSoft.g, kAccentSoft.b, a},
                               Color{kAccentSoft.r, kAccentSoft.g, kAccentSoft.b, 0});
    }

    // Tagline.
    {
        const char* tagline = "two flavours of arena combat";
        int g_size = 17;
        int gw = MeasureText(tagline, g_size);
        DrawText(tagline, (sw - gw) / 2 + 1, ty + t_size + 24 + 1, g_size,
                 Color{0, 0, 0, 150});
        DrawText(tagline, (sw - gw) / 2,     ty + t_size + 24,     g_size,
                 Color{185, 205, 230, 220});
    }

    // ---------- Mode cards ----------
    // Two large card-style buttons (LOCAL + GLOBAL) side-by-side with a flavor
    // line above each. Replaces the old narrow column so the screen breathes
    // and the GLOBAL placeholder reads as "second equal option, just dimmed".
    RoyaleMenuAction action = RoyaleMenuAction::None;
    const int card_w   = 320;
    const int card_h   = 200;
    const int card_gap = 30;
    const int card_y   = static_cast<int>(sh * 0.43f);
    const int total_w  = card_w * 2 + card_gap;
    const int left_x   = (sw - total_w) / 2;
    const int right_x  = left_x + card_w + card_gap;

    // --- LOCAL card (primary) ---
    {
        // Card backdrop -- gradient + accent ring + a small "live" status pip
        // so it visually beats the GLOBAL placeholder.
        Rectangle r{(float)left_x, (float)card_y, (float)card_w, (float)card_h};
        DrawRectangleGradientV(left_x, card_y, card_w, card_h,
                               Color{30, 60, 80, 240},
                               Color{18, 36, 56, 240});
        DrawRectangleRoundedLines(r, 0.06f, 8, Color{120, 180, 220, 130});

        // Inner glow ring near the top so the card has a clear "header strip".
        DrawRectangle(left_x + 14, card_y + 14, card_w - 28, 1,
                      Color{120, 180, 220, 90});

        // Header label + status pip (green = live).
        DrawText("LOCAL", left_x + 24, card_y + 24, 30,
                 Color{220, 240, 250, 240});
        DrawCircle(left_x + card_w - 26, card_y + 36, 5.0f,
                   Color{120, 220, 150, 230});
        // "LIVE" hint next to the pip.
        DrawText("LIVE", left_x + card_w - 64, card_y + 28, 12,
                 Color{170, 220, 180, 230});

        // Description text -- 2-3 short lines so the card has substance.
        DrawText("Host or join a LAN match.",
                 left_x + 24, card_y + 76, 15, Color{200, 215, 230, 230});
        DrawText("Same-network friends, up to 16 players.",
                 left_x + 24, card_y + 98, 13, Color{170, 190, 210, 210});
        DrawText("No login, no servers, no fuss.",
                 left_x + 24, card_y + 118, 13, Color{170, 190, 210, 210});

        // Action button at the bottom of the card.
        Rectangle btn_r{(float)(left_x + 24), (float)(card_y + card_h - 56),
                        (float)(card_w - 48), 40.0f};
        if (drawButton(btn_r, "OPEN  LOBBY  >", 18,
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

        DrawRectangle(right_x + 14, card_y + 14, card_w - 28, 1,
                      Color{100, 120, 160, 70});

        DrawText("GLOBAL", right_x + 24, card_y + 24, 30,
                 Color{180, 195, 220, 200});
        // Amber pip = "later".
        DrawCircle(right_x + card_w - 26, card_y + 36, 5.0f,
                   Color{220, 180, 90, 220});
        DrawText("SOON", right_x + card_w - 64, card_y + 28, 12,
                 Color{220, 195, 130, 220});

        DrawText("Matchmaking across the internet.",
                 right_x + 24, card_y + 76, 15, Color{180, 195, 215, 200});
        DrawText("Ranked play, persistent profiles.",
                 right_x + 24, card_y + 98, 13, Color{150, 170, 200, 180});
        DrawText("Lands with Phase 10.",
                 right_x + 24, card_y + 118, 13, Color{150, 170, 200, 180});

        Rectangle btn_r{(float)(right_x + 24), (float)(card_y + card_h - 56),
                        (float)(card_w - 48), 40.0f};
        if (drawButton(btn_r, "PREVIEW  >", 18,
                       Color{52, 62, 92, 255},
                       Color{200, 210, 235, 230})) {
            global_toast_remaining_ = 2.8f;
        }
    }

    // ---------- BACK button ----------
    {
        const int back_w = 200;
        const int back_h = 50;
        Rectangle bb{(float)(sw / 2 - back_w / 2),
                     (float)(card_y + card_h + 30),
                     (float)back_w, (float)back_h};
        if (drawButton(bb, "BACK", 22,
                       Color{42, 50, 72, 255}, Color{220, 225, 245, 230})) {
            action = RoyaleMenuAction::BackToMainMenu;
        }
    }

    // ---------- "Coming soon" toast (Global) ----------
    if (global_toast_remaining_ > 0.0f) {
        const char* msg =
            "Global matchmaking ships with Phase 10. Use LOCAL for LAN games now.";
        int m_fs = 16;
        int mw   = MeasureText(msg, m_fs);
        float k  = std::clamp(global_toast_remaining_ / 2.8f, 0.0f, 1.0f);
        float fade = std::min(k * 2.0f, (1.0f - k) * 3.0f + 0.5f);
        fade = std::clamp(fade, 0.0f, 1.0f);
        unsigned char a = static_cast<unsigned char>(fade * 230);
        int pad = 18;
        int box_x = sw / 2 - mw / 2 - pad;
        int box_y = sh - 110;
        Rectangle box{(float)box_x, (float)box_y,
                      (float)(mw + pad * 2), (float)(m_fs + pad + 4)};
        // Two-tone gradient backdrop + accent ring.
        DrawRectangleGradientV(box_x, box_y, mw + pad * 2, m_fs + pad + 4,
                               Color{40, 55, 85, a},
                               Color{24, 32, 56, a});
        DrawRectangleRoundedLines(box, 0.4f, 6,
                                  Color{180, 200, 240, (unsigned char)(a * 0.85f)});
        // Tiny amber pip to mirror the GLOBAL card's "SOON" badge.
        DrawCircle(box_x + 16, box_y + (m_fs + pad + 4) / 2, 4.0f,
                   Color{220, 180, 90, a});
        DrawText(msg, box_x + pad + 14, box_y + pad / 2 + 4, m_fs,
                 Color{225, 235, 250, a});
    }

    // ---------- Footer ----------
    {
        const char* foot = "press  ESC  to  go  back";
        int fs = 13;
        int fw = MeasureText(foot, fs);
        DrawText(foot, (sw - fw) / 2, sh - 30, fs,
                 Color{140, 155, 180, 180});
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        action = RoyaleMenuAction::BackToMainMenu;
    }

    return action;
}

} // namespace cr
