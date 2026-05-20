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
// All settings widgets draw a small text label *above* their rect (at rect.y - 22).
// We track a vertical cursor and place each row so:
//   row top    = cursor (label position, 16pt text ~ 14px ink height)
//   widget top = cursor + 22
//   row bottom = cursor + kRowH
// kRowH must be > 22 + widget_h + ~10px to keep next label legibly separate. The
// tallest widget is the choice row (36px), so 22 + 36 + 10 = 68 is the minimum.
constexpr int kRowH         = 68;
constexpr int kHeaderH      = 28;
constexpr int kSectionGap   = 18;
constexpr int kSliderTrackH = 14;
constexpr int kToggleH      = 32;
constexpr int kChoiceH      = 36;

struct Cursor {
    int x;
    int y;
    int col_w;
};

void sectionHeader(Cursor& c, const char* text) {
    DrawText(text, c.x, c.y, 18, Color{255, 220, 120, 230});
    c.y += kHeaderH;
}

void sectionGap(Cursor& c) {
    c.y += kSectionGap;
}

// Returns true if value changed.
bool sliderRow(Cursor& c, const char* label, const char* value_text,
               float* value, float lo, float hi) {
    Rectangle r{static_cast<float>(c.x), static_cast<float>(c.y + 22),
                static_cast<float>(c.col_w), static_cast<float>(kSliderTrackH)};
    bool changed = drawSlider(r, label, value_text, value, lo, hi);
    c.y += kRowH;
    return changed;
}

bool toggleRow(Cursor& c, const char* label, bool* value, int width = 110) {
    Rectangle r{static_cast<float>(c.x), static_cast<float>(c.y + 22),
                static_cast<float>(width), static_cast<float>(kToggleH)};
    bool changed = drawToggle(r, label, value);
    c.y += kRowH;
    return changed;
}

bool choiceRow(Cursor& c, const char* label,
               const char* const* options, int count, int* index) {
    Rectangle r{static_cast<float>(c.x), static_cast<float>(c.y + 22),
                static_cast<float>(c.col_w), static_cast<float>(kChoiceH)};
    bool changed = drawChoice(r, label, options, count, index);
    c.y += kRowH;
    return changed;
}

} // namespace

SettingsAction SettingsScreen::render(int sw, int sh, SaveData& save) {
    // Background: match the rest of the UI.
    DrawRectangle(0, 0, sw, sh, Color{18, 22, 30, 255});
    DrawRectangle(0, 0, sw, sh, Color{0, 0, 0, 60});

    SettingsAction action = SettingsAction::None;

    // ---- Top-left BACK button (always visible) ----
    // Mirror of the bottom BACK button. The bottom one anchors below all
    // settings rows and can scroll off-screen on tall content / small
    // windows; this one is hard-anchored at the top-left corner so the
    // player always has a way out without scrolling.
    {
        const Rectangle r{16, 16, 90, 36};
        if (drawButton(r, "< BACK", 18,
                       Color{55, 145, 95, 255}, Color{255, 255, 255, 255})) {
            action = SettingsAction::BackToMenu;
        }
    }

    // ---- Title ----
    const char* title    = "SETTINGS";
    constexpr int t_size = 44;
    int tw = MeasureText(title, t_size);
    int ty = 22;
    DrawText(title, (sw - tw) / 2 + 3, ty + 3, t_size, Color{0, 0, 0, 180});
    DrawText(title, (sw - tw) / 2,     ty,     t_size, Color{255, 215, 130, 255});

    // ---- Two-column grid centered horizontally ----
    constexpr int col_w  = 380;
    constexpr int gutter = 90;
    const int total_w    = col_w * 2 + gutter;
    const int columns_x  = (sw - total_w) / 2;
    const int columns_y  = ty + t_size + 24; // 24px below the title baseline

    Cursor L{columns_x,                        columns_y, col_w};
    Cursor R{columns_x + col_w + gutter,       columns_y, col_w};

    // ===== Left column =====
    // Identity comes first -- the player should see their own name in the
    // multiplayer killfeed / leaderboard / nameplate, and an empty default
    // here is what triggers the generic `P<id>` fallback.
    sectionHeader(L, "IDENTITY");
    {
        const int  label_fs = 14;
        const int  field_h  = 32;
        const int  pad      = 4;
        const int  field_w  = L.col_w;
        const int  field_y  = L.y + label_fs + pad;
        DrawText("Player name (16 chars max)", L.x, L.y, label_fs,
                 Color{200, 215, 240, 220});
        Rectangle box{(float)L.x, (float)field_y, (float)field_w, (float)field_h};
        DrawRectangleRec(box, Color{30, 38, 58, 230});
        // Focus toggle: clicking inside the box focuses it, clicking outside
        // unfocuses. raylib's mouse-just-pressed event fires once per click so
        // we don't have to debounce.
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mp = GetMousePosition();
            name_field_focused_ = CheckCollisionPointRec(mp, box);
        }
        // Enter also unfocuses (matches the convention of "commit" inputs).
        if (name_field_focused_ && IsKeyPressed(KEY_ENTER)) {
            name_field_focused_ = false;
        }
        // Border is highlighted while focused so it's obvious typing will
        // land here.
        DrawRectangleLinesEx(box, 2.0f,
            name_field_focused_ ? Color{180, 200, 240, 230}
                                : Color{100, 120, 160, 180});
        // Display: either the typed name, or "click to set" placeholder when
        // empty + unfocused (so brand-new players see what to do).
        const bool show_placeholder = save.player_name.empty()
                                    && !name_field_focused_;
        const char* text = show_placeholder ? "click to set"
                                            : save.player_name.c_str();
        Color text_col = show_placeholder ? Color{140, 150, 170, 220}
                                          : Color{230, 240, 250, 240};
        DrawText(text, (int)box.x + 10, (int)box.y + 8, 18, text_col);

        // Text edit -- ASCII printable range only (32..126), capped at
        // kMaxPlayerNameLen. Strips whitespace-only inputs because a name
        // of just spaces would render invisibly.
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
        L.y = field_y + field_h + 14;
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
        // Live preview also takes effect on the inline sample below.
        setHudTextScale(save.hud_text_scale);
    }
    // Inline preview row: an "x5 COMBO" sample drawn at the current scale so the user
    // can actually see the change without leaving settings. Base size 24 is chosen so
    // even at 1.20x the preview stays small enough to keep the BACK button on-screen
    // on a 720-tall window.
    {
        const int preview_size = static_cast<int>(24 * save.hud_text_scale + 0.5f);
        const int preview_h    = preview_size + 12;
        const char* sample = "PREVIEW   x5 COMBO";
        int tw = MeasureText(sample, preview_size);
        int tx = L.x + (col_w - tw) / 2;
        int ty = L.y;
        DrawText(sample, tx + 2, ty + 2, preview_size, Color{0, 0, 0, 160});
        DrawText(sample, tx,     ty,     preview_size,
                 Color{255, 215, 130, 240});
        L.y += preview_h + 4;
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
    // Fullscreen toggle. raylib's ToggleFullscreen() switches between
    // borderless-fullscreen-on-current-monitor and the original windowed
    // size. We compare against the current state so a single check-flip
    // works whether the user came from F11 or the settings checkbox.
    {
        bool wanted = save.fullscreen;
        if (toggleRow(R, "Fullscreen", &wanted)) {
            save.fullscreen = wanted;
            const bool currently_fs = IsWindowFullscreen();
            if (wanted != currently_fs) ToggleFullscreen();
        }
    }
    // VSync. FLAG_VSYNC_HINT is a hint to GLFW so the actual behavior
    // depends on the driver, but where honored it pins the frame rate to
    // the monitor refresh and removes tearing. Off by default because
    // most players prefer the lower input latency for twitchy gameplay.
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
    const int back_w = 240;
    const int back_h = 56;
    const int back_y = content_bottom + 28;
    const int back_x = (sw - back_w) / 2;

    const int reset_w = 200;
    const int reset_h = 56;
    const int reset_y = back_y;
    const int reset_x = back_x - reset_w - 30;  // 30 px gap to the left of BACK

    if (drawButton({(float)reset_x, (float)reset_y, (float)reset_w, (float)reset_h},
                   "RESET", 22,
                   Color{140, 80, 65, 255}, Color{255, 240, 230, 255})) {
        // Pre-reset values we need to KEEP so live-apply works correctly
        // after the reset (and so the SaveData reference passed in still
        // reflects the apply).
        resetSettingsToDefaults(save);
        // Live-apply each system that watches its setting -- otherwise the
        // user would have to leave Settings + come back for the reset to
        // take visible effect.
        if (IsAudioDeviceReady()) SetMasterVolume(save.master_volume);
        setPaletteMode(static_cast<PaletteMode>(save.colorblind_mode));
        setHighContrast(save.high_contrast);
        setHudTextScale(save.hud_text_scale);
        if (save.fps_cap == 0) SetTargetFPS(0);
        else                    SetTargetFPS(static_cast<int>(save.fps_cap));
        // Display reset: drop back out of fullscreen if we were in it; clear
        // the VSync hint. ToggleFullscreen is the only public API for the
        // window-mode change on raylib 5.5; check against current state so
        // we don't double-flip.
        if (IsWindowFullscreen()) ToggleFullscreen();
        ClearWindowState(FLAG_VSYNC_HINT);
    }

    if (drawButton({(float)back_x, (float)back_y, (float)back_w, (float)back_h},
                   "BACK", 26,
                   Color{55, 145, 95, 255}, Color{255, 255, 255, 255})) {
        action = SettingsAction::BackToMenu;
    }

    if (IsKeyPressed(KEY_ESCAPE)) action = SettingsAction::BackToMenu;

    return action;
}

} // namespace cr
