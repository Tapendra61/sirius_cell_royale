#include "SettingsScreen.h"

#include "Renderer.h"
#include "UiWidgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cr {

namespace {

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
    sectionHeader(R, "PERFORMANCE");
    int fps_idx = fpsCapIndex(save.fps_cap);
    if (choiceRow(R, "FPS cap",
                  kFpsCapOptions, kFpsCapCount, &fps_idx)) {
        save.fps_cap = kFpsCapValues[fps_idx];
        if (save.fps_cap == 0) SetTargetFPS(0);
        else                    SetTargetFPS(static_cast<int>(save.fps_cap));
    }

    // ---- Back button: anchored to the tallest column, with a comfortable gap. ----
    const int content_bottom = std::max(L.y, R.y);
    const int back_w = 240;
    const int back_h = 56;
    const int back_y = content_bottom + 28;
    const int back_x = (sw - back_w) / 2;

    SettingsAction action = SettingsAction::None;
    if (drawButton({(float)back_x, (float)back_y, (float)back_w, (float)back_h},
                   "BACK", 26,
                   Color{55, 145, 95, 255}, Color{255, 255, 255, 255})) {
        action = SettingsAction::BackToMenu;
    }

    if (IsKeyPressed(KEY_ESCAPE)) action = SettingsAction::BackToMenu;

    return action;
}

} // namespace cr
