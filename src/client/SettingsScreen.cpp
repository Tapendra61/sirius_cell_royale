#include "SettingsScreen.h"

#include "Renderer.h"
#include "UiWidgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cr {

namespace {

// Apply every setting (NOT progression!) field back to its SaveData default.
// Progression -- total_xp, level, games_played, best_mass, best_combo,
// daily_missions, last_mission_reset_day, first_run_complete -- and identity
// (player_name) are left alone so the player doesn't accidentally wipe their
// stats by clicking Reset. The defaults here mirror the inline defaults
// declared in SaveFile.h's SaveData struct; if you change a default there,
// change it here too.
void resetSettingsToDefaults(SaveData& d) {
    // Audio
    d.master_volume       = 1.0f;
    d.sfx_volume          = 0.85f;
    d.music_volume        = 0.5f;
    d.music_enabled       = true;
    // Input
    d.hold_to_move        = false;
    d.invert_thumbs       = false;
    // Accessibility / visual
    d.screen_shake_scale  = 1.0f;
    d.colorblind_mode     = 0;
    d.high_contrast       = false;
    d.hud_text_scale      = 1.0f;
    // Performance
    d.fps_cap             = 60;
    // Display
    d.fullscreen          = false;
    d.vsync               = false;
}

const char* kColorblindOptions[] = {"Off", "Deuteranopia", "Protanopia", "Tritanopia"};
constexpr int kColorblindCount   = 4;

const char* kFpsCapOptions[] = {"60", "120", "144", "Uncapped"};
constexpr uint16_t kFpsCapValues[] = {60, 120, 144, 0};
constexpr int kFpsCapCount = 4;

int fpsCapIndex(uint16_t v) {
    for (int i = 0; i < kFpsCapCount; ++i) {
        if (kFpsCapValues[i] == v) return i;
    }
    return 0;
}

// HUD text size choices. Capped at 1.20 -- the summary panel layout was designed for
// 1.0 and gets uncomfortably tight past that. The slider's underlying clamp in
// UiWidgets allows up to 1.30 for power users via direct save edit.
const char* kHudTextOptions[]    = {"Small", "Normal", "Large", "X-Large"};
constexpr float kHudTextValues[] = {0.90f,  1.00f,    1.10f,   1.20f};
constexpr int   kHudTextCount    = 4;

int hudTextIndex(float v) {
    int best = 1; // default to Normal
    float best_diff = 1e9f;
    for (int i = 0; i < kHudTextCount; ++i) {
        float d = std::fabs(v - kHudTextValues[i]);
        if (d < best_diff) { best_diff = d; best = i; }
    }
    return best;
}

// ---- Layout primitives ----
//
// All dimensions are stored at the REFERENCE resolution (1280 x 720) and
// scaled by uiScale(sw, sh) when the screen is constructed each frame.
// The Cursor carries the current x/y/width AND the resolved scaled
// dimensions so row helpers stay parameterless w.r.t. layout.
//
// A row's vertical layout (constant ratios, all scaled):
//   row top    = cursor.y         (label position)
//   widget top = cursor.y + label_off
//   row bottom = cursor.y + row_h
struct Cursor {
    int x = 0, y = 0, col_w = 0;
    int row_h         = 68;
    int header_h      = 28;
    int section_gap   = 18;
    int label_off     = 22; // y-distance from row-top to widget-top
    int slider_h      = 14;
    int toggle_h      = 32;
    int toggle_w      = 110;
    int choice_h      = 36;
    int font_header   = 18;
    int font_label    = 14;
};

void sectionHeader(Cursor& c, const char* text) {
    DrawText(text, c.x, c.y, c.font_header, Color{255, 220, 120, 230});
    c.y += c.header_h;
}

void sectionGap(Cursor& c) {
    c.y += c.section_gap;
}

bool sliderRow(Cursor& c, const char* label, const char* value_text,
               float* value, float lo, float hi) {
    Rectangle r{static_cast<float>(c.x),
                static_cast<float>(c.y + c.label_off),
                static_cast<float>(c.col_w),
                static_cast<float>(c.slider_h)};
    bool changed = drawSlider(r, label, value_text, value, lo, hi);
    c.y += c.row_h;
    return changed;
}

bool toggleRow(Cursor& c, const char* label, bool* value) {
    Rectangle r{static_cast<float>(c.x),
                static_cast<float>(c.y + c.label_off),
                static_cast<float>(c.toggle_w),
                static_cast<float>(c.toggle_h)};
    bool changed = drawToggle(r, label, value);
    c.y += c.row_h;
    return changed;
}

bool choiceRow(Cursor& c, const char* label,
               const char* const* options, int count, int* index) {
    Rectangle r{static_cast<float>(c.x),
                static_cast<float>(c.y + c.label_off),
                static_cast<float>(c.col_w),
                static_cast<float>(c.choice_h)};
    bool changed = drawChoice(r, label, options, count, index);
    c.y += c.row_h;
    return changed;
}

} // namespace

SettingsAction SettingsScreen::render(int sw, int sh, SaveData& save) {
    // Background: match the rest of the UI.
    DrawRectangle(0, 0, sw, sh, Color{18, 22, 30, 255});
    DrawRectangle(0, 0, sw, sh, Color{0, 0, 0, 60});

    SettingsAction action = SettingsAction::None;

    // ---- Settings-page scale ----
    // The form is naturally ~830 px tall in the 1280x720 design space, which
    // means the BACK + RESET buttons fall below the visible 720-px bottom
    // even at 1.0x window scale. Two fixes layered together:
    //
    //   (a) Tightened row layout below (proto values 50/22/10/etc. instead
    //       of the original 68/28/18) so the form is ~620 px tall at
    //       design-space, comfortably fitting in 720 with room for buttons.
    //
    //   (b) Additionally compute a settings-specific scale `sui` that's the
    //       MIN of the regular uiScale AND the screen-height ratio against
    //       a known-tall content estimate (700 px). Means: on a small
    //       window where the regular uiScale would make the form taller
    //       than the screen, we shrink the form instead so the buttons
    //       stay visible. On a fullscreen 4K window the regular uiScale
    //       wins (the form has room to grow).
    const float reg_ui  = uiScale(sw, sh);
    const float h_ui    = static_cast<float>(sh) / 700.0f; // 700 = est. form height at 1.0x
    const float sui     = std::min(reg_ui, h_ui);
    auto px = [sui](int base) {
        return static_cast<int>(base * sui + 0.5f);
    };

    Cursor proto;
    proto.row_h        = px(50);
    proto.header_h     = px(22);
    proto.section_gap  = px(10);
    proto.label_off    = px(18);
    proto.slider_h     = px(14);
    proto.toggle_h     = px(28);
    proto.toggle_w     = px(110);
    proto.choice_h     = px(32);
    proto.font_header  = px(16);
    proto.font_label   = px(13);

    // ---- Title ----
    const char* title = "SETTINGS";
    const int t_size  = px(34);
    int tw            = MeasureText(title, t_size);
    int ty            = px(14);
    DrawText(title, (sw - tw) / 2 + 3, ty + 3, t_size, Color{0, 0, 0, 180});
    DrawText(title, (sw - tw) / 2,     ty,     t_size, Color{255, 215, 130, 255});

    // ---- Two-column grid centered horizontally ----
    // col_w scales with the same `sui` factor so the columns grow/shrink
    // together with the rest of the form. On wide windows there's extra
    // horizontal slack on either side of the form -- that's fine, settings
    // shouldn't sprawl across an ultrawide monitor.
    const int col_w     = px(360);
    const int gutter    = px( 60);
    const int total_w   = col_w * 2 + gutter;
    const int columns_x = (sw - total_w) / 2;
    const int columns_y = ty + t_size + px(16);

    Cursor L = proto; L.x = columns_x;                  L.y = columns_y; L.col_w = col_w;
    Cursor R = proto; R.x = columns_x + col_w + gutter; R.y = columns_y; R.col_w = col_w;

    // ===== Left column =====
    sectionHeader(L, "IDENTITY");
    {
        const int label_fs  = L.font_label;
        const int field_h   = px(28);
        const int pad       = px(4);
        const int field_w   = L.col_w;
        const int field_y   = L.y + label_fs + pad;
        const int text_fs   = px(16);
        DrawText("Player name (16 chars max)", L.x, L.y, label_fs,
                 Color{200, 215, 240, 220});
        Rectangle box{(float)L.x, (float)field_y, (float)field_w, (float)field_h};
        DrawRectangleRec(box, Color{30, 38, 58, 230});
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mp = GetMousePosition();
            name_field_focused_ = CheckCollisionPointRec(mp, box);
        }
        if (name_field_focused_ && IsKeyPressed(KEY_ENTER)) {
            name_field_focused_ = false;
        }
        DrawRectangleLinesEx(box, 2.0f,
            name_field_focused_ ? Color{180, 200, 240, 230}
                                : Color{100, 120, 160, 180});
        const bool show_placeholder = save.player_name.empty()
                                    && !name_field_focused_;
        const char* text = show_placeholder ? "click to set"
                                            : save.player_name.c_str();
        Color text_col = show_placeholder ? Color{140, 150, 170, 220}
                                          : Color{230, 240, 250, 240};
        DrawText(text, (int)box.x + px(10),
                       (int)box.y + px(6),
                       text_fs, text_col);

        if (name_field_focused_) {
            int ch = GetCharPressed();
            while (ch > 0) {
                if (save.player_name.size() < kMaxPlayerNameLen
                    && ch >= 32 && ch <= 126) {
                    save.player_name.push_back(static_cast<char>(ch));
                }
                ch = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE) && !save.player_name.empty()) {
                save.player_name.pop_back();
            }
        }
        L.y = field_y + field_h + px(10);
    }

    sectionGap(L);
    sectionHeader(L, "AUDIO");
    if (sliderRow(L, "Master volume",
                  TextFormat("%d%%", (int)(save.master_volume * 100.0f + 0.5f)),
                  &save.master_volume, 0.0f, 1.0f)) {
        if (IsAudioDeviceReady()) SetMasterVolume(save.master_volume);
    }
    sliderRow(L, "SFX volume",
              TextFormat("%d%%", (int)(save.sfx_volume * 100.0f + 0.5f)),
              &save.sfx_volume, 0.0f, 1.0f);
    sliderRow(L, "Music volume",
              TextFormat("%d%%", (int)(save.music_volume * 100.0f + 0.5f)),
              &save.music_volume, 0.0f, 1.0f);
    toggleRow(L, "Music enabled", &save.music_enabled);

    sectionGap(L);
    sectionHeader(L, "VISUAL");
    sliderRow(L, "Screen shake",
              TextFormat("%d%%", (int)(save.screen_shake_scale * 100.0f + 0.5f)),
              &save.screen_shake_scale, 0.0f, 1.5f);

    int hud_idx = hudTextIndex(save.hud_text_scale);
    if (choiceRow(L, "HUD text size",
                  kHudTextOptions, kHudTextCount, &hud_idx)) {
        save.hud_text_scale = kHudTextValues[hud_idx];
        setHudTextScale(save.hud_text_scale);
    }
    // Inline preview row: an "x5 COMBO" sample drawn at the current scale so the user
    // can actually see the change without leaving settings.
    {
        const int base_size    = px(20);
        const int preview_size = static_cast<int>(base_size * save.hud_text_scale + 0.5f);
        const int preview_h    = preview_size + px(8);
        const char* sample = "PREVIEW   x5 COMBO";
        int ptw = MeasureText(sample, preview_size);
        int tx  = L.x + (col_w - ptw) / 2;
        int pty = L.y;
        DrawText(sample, tx + 2, pty + 2, preview_size, Color{0, 0, 0, 160});
        DrawText(sample, tx,     pty,     preview_size,
                 Color{255, 215, 130, 240});
        L.y += preview_h + px(4);
    }

    // ===== Right column =====
    sectionHeader(R, "ACCESSIBILITY");
    int cb_idx = std::clamp<int>(save.colorblind_mode, 0, kColorblindCount - 1);
    if (choiceRow(R, "Colorblind palette",
                  kColorblindOptions, kColorblindCount, &cb_idx)) {
        save.colorblind_mode = static_cast<uint8_t>(cb_idx);
        setPaletteMode(static_cast<PaletteMode>(cb_idx));
    }
    if (toggleRow(R, "High-contrast outlines", &save.high_contrast)) {
        setHighContrast(save.high_contrast);
    }

    sectionGap(R);
    sectionHeader(R, "CONTROLS");
    toggleRow(R, "Hold mouse to move",    &save.hold_to_move);
    toggleRow(R, "Invert touch thumbs",   &save.invert_thumbs);

    sectionGap(R);
    sectionHeader(R, "DISPLAY");
    {
        bool wanted = save.fullscreen;
        if (toggleRow(R, "Fullscreen", &wanted)) {
            save.fullscreen = wanted;
            const bool currently_fs = IsWindowFullscreen();
            if (wanted != currently_fs) ToggleFullscreen();
        }
    }
    {
        bool wanted = save.vsync;
        if (toggleRow(R, "VSync", &wanted)) {
            save.vsync = wanted;
            if (wanted) SetWindowState(FLAG_VSYNC_HINT);
            else        ClearWindowState(FLAG_VSYNC_HINT);
        }
    }

    sectionGap(R);
    sectionHeader(R, "PERFORMANCE");
    int fps_idx = fpsCapIndex(save.fps_cap);
    if (choiceRow(R, "FPS cap",
                  kFpsCapOptions, kFpsCapCount, &fps_idx)) {
        save.fps_cap = kFpsCapValues[fps_idx];
        if (save.fps_cap == 0) SetTargetFPS(0);
        else                    SetTargetFPS(static_cast<int>(save.fps_cap));
    }

    // ---- Bottom row: RESET DEFAULTS (left) + BACK (right) ----
    // Floor-anchored to the bottom of the screen. Natural position is "below
    // content_bottom with a 22 px gap"; floor is "20 px above the screen
    // bottom". We take min(natural, floor) so if the form is short the
    // buttons sit naturally below it, and if the form is tall (or the
    // window is short) the buttons stay pinned above the bottom edge
    // instead of sliding off-screen.
    const int back_w  = px(220);
    const int back_h  = px(46);
    const int reset_w = px(180);
    const int reset_h = back_h;

    const int content_bottom = std::max(L.y, R.y);
    const int natural_y      = content_bottom + px(22);
    const int floor_y        = sh - back_h - px(18);
    const int back_y         = std::min(natural_y, floor_y);
    const int reset_y        = back_y;

    const int back_x  = (sw - back_w) / 2;
    const int reset_x = back_x - reset_w - px(20);

    const int back_fs  = px(22);
    const int reset_fs = px(20);

    if (drawButton({(float)reset_x, (float)reset_y, (float)reset_w, (float)reset_h},
                   "RESET", reset_fs,
                   Color{140, 80, 65, 255}, Color{255, 240, 230, 255})) {
        resetSettingsToDefaults(save);
        if (IsAudioDeviceReady()) SetMasterVolume(save.master_volume);
        setPaletteMode(static_cast<PaletteMode>(save.colorblind_mode));
        setHighContrast(save.high_contrast);
        setHudTextScale(save.hud_text_scale);
        if (save.fps_cap == 0) SetTargetFPS(0);
        else                    SetTargetFPS(static_cast<int>(save.fps_cap));
        if (IsWindowFullscreen()) ToggleFullscreen();
        ClearWindowState(FLAG_VSYNC_HINT);
    }

    if (drawButton({(float)back_x, (float)back_y, (float)back_w, (float)back_h},
                   "BACK", back_fs,
                   Color{55, 145, 95, 255}, Color{255, 255, 255, 255})) {
        action = SettingsAction::BackToMenu;
    }

    if (IsKeyPressed(KEY_ESCAPE)) action = SettingsAction::BackToMenu;

    return action;
}

} // namespace cr
