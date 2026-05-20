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

    // ---- Build a Cursor template with screen-scaled layout values ----
    // Every constant here was originally a constexpr int sized for the
    // 1280 x 720 reference window. uiPx() scales each to the current
    // screen so the entire form grows together when the player goes
    // fullscreen or shrinks an 800 x 600 window.
    Cursor proto;
    proto.row_h        = uiPx(sw, sh, 68);
    proto.header_h     = uiPx(sw, sh, 28);
    proto.section_gap  = uiPx(sw, sh, 18);
    proto.label_off    = uiPx(sw, sh, 22);
    proto.slider_h     = uiPx(sw, sh, 14);
    proto.toggle_h     = uiPx(sw, sh, 32);
    proto.toggle_w     = uiPx(sw, sh, 110);
    proto.choice_h    = uiPx(sw, sh, 36);
    proto.font_header = uiPx(sw, sh, 18);
    proto.font_label  = uiPx(sw, sh, 14);

    // ---- Title ----
    const char* title = "SETTINGS";
    const int t_size  = uiPx(sw, sh, 44);
    int tw            = MeasureText(title, t_size);
    int ty            = uiPx(sw, sh, 22);
    DrawText(title, (sw - tw) / 2 + 3, ty + 3, t_size, Color{0, 0, 0, 180});
    DrawText(title, (sw - tw) / 2,     ty,     t_size, Color{255, 215, 130, 255});

    // ---- Two-column grid centered horizontally ----
    const int col_w     = uiPx(sw, sh, 380);
    const int gutter    = uiPx(sw, sh, 90);
    const int total_w   = col_w * 2 + gutter;
    const int columns_x = (sw - total_w) / 2;
    const int columns_y = ty + t_size + uiPx(sw, sh, 24);

    Cursor L = proto; L.x = columns_x;                  L.y = columns_y; L.col_w = col_w;
    Cursor R = proto; R.x = columns_x + col_w + gutter; R.y = columns_y; R.col_w = col_w;

    // ===== Left column =====
    // Identity comes first -- the player should see their own name in the
    // multiplayer killfeed / leaderboard / nameplate, and an empty default
    // here is what triggers the generic `P<id>` fallback.
    sectionHeader(L, "IDENTITY");
    {
        const int label_fs  = L.font_label;
        const int field_h   = uiPx(sw, sh, 32);
        const int pad       = uiPx(sw, sh, 4);
        const int field_w   = L.col_w;
        const int field_y   = L.y + label_fs + pad;
        const int text_fs   = uiPx(sw, sh, 18);
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
        DrawText(text, (int)box.x + uiPx(sw, sh, 10),
                       (int)box.y + uiPx(sw, sh,  8),
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
        L.y = field_y + field_h + uiPx(sw, sh, 14);
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
    // can actually see the change without leaving settings. Scaled by uiPx so the
    // preview itself respects the screen size, then multiplied by hud_text_scale
    // for the accessibility-multiplier preview component.
    {
        const int base_size    = uiPx(sw, sh, 24);
        const int preview_size = static_cast<int>(base_size * save.hud_text_scale + 0.5f);
        const int preview_h    = preview_size + uiPx(sw, sh, 12);
        const char* sample = "PREVIEW   x5 COMBO";
        int ptw = MeasureText(sample, preview_size);
        int tx  = L.x + (col_w - ptw) / 2;
        int pty = L.y;
        DrawText(sample, tx + 2, pty + 2, preview_size, Color{0, 0, 0, 160});
        DrawText(sample, tx,     pty,     preview_size,
                 Color{255, 215, 130, 240});
        L.y += preview_h + uiPx(sw, sh, 4);
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
    // Anchored to the tallest column with a comfortable gap. Reset sits to
    // the left of the BACK button so the player has to pass over BACK to
    // reach it -- mild "are you sure" affordance without an actual modal.
    const int content_bottom = std::max(L.y, R.y);
    const int back_w  = uiPx(sw, sh, 240);
    const int back_h  = uiPx(sw, sh,  56);
    const int back_y  = content_bottom + uiPx(sw, sh, 28);
    const int back_x  = (sw - back_w) / 2;

    const int reset_w = uiPx(sw, sh, 200);
    const int reset_h = back_h;
    const int reset_y = back_y;
    const int reset_x = back_x - reset_w - uiPx(sw, sh, 30);

    const int back_fs  = uiPx(sw, sh, 26);
    const int reset_fs = uiPx(sw, sh, 22);

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
