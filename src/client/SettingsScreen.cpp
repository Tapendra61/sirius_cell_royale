#include "SettingsScreen.h"

#include "Renderer.h"
#include "UiWidgets.h"

#include <algorithm>
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
