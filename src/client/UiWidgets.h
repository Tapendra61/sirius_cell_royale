#pragma once

#include "raylib.h"

namespace cr {

// Mouse-aware rounded button with hover / press / disabled states.
// Returns true on the frame the user releases the mouse button while still hovering
// (i.e. a click completes inside the rect). Drawn entirely with primitives — no
// font/style dependencies on raygui so the visual matches the rest of our HUD.
bool drawButton(Rectangle r, const char* label, int font_size,
                Color fill, Color text_color, bool enabled = true);

// Same as drawButton but with an optional small sub-label centered below the main
// label (e.g., "Coming Soon" under "ROYALE"). Sub-label is rendered dim regardless
// of enabled state.
bool drawButtonWithSub(Rectangle r, const char* label, int font_size,
                       const char* sub_label, int sub_size,
                       Color fill, Color text_color, bool enabled = true);

// Horizontal slider for a settings row. `r` is the track rect; the label is drawn
// above it and the value (formatted by caller via `value_text`) is drawn at the
// right end. Returns true if the value was modified this frame. Drag-to-set works
// (mouse held inside track). value is clamped to [lo, hi].
bool drawSlider(Rectangle r, const char* label, const char* value_text,
                float* value, float lo, float hi);

// Left/Right cycle through a list of named options. Returns true if the selection
// changed. Used for discrete settings like FPS cap / colorblind palette.
bool drawChoice(Rectangle r, const char* label,
                const char* const* options, int option_count,
                int* index);

// On/Off toggle pill. Returns true if state flipped.
bool drawToggle(Rectangle r, const char* label, bool* value);

// Horizontal row of preset chips. Renders the label above the row, then
// `option_count` equally-sized buttons inside `r`. The button at `*index` is
// rendered with a highlighted fill so the user knows which preset is active.
// Returns true if the user clicked a different preset.
bool drawPresetRow(Rectangle r, const char* label,
                   const char* const* options, int option_count,
                   int* index);

// Accessibility: HUD-text size multiplier. Applied by Hud.cpp to in-match font
// sizes (combo counter, summary panel, pause overlay, debug stats). Main menu
// and settings screen are NOT affected -- those have tight layouts that would
// overflow at large scales, and they're not "HUD" in the gameplay sense.
//
// Clamped to [0.85, 1.30] internally. Set globally (process-wide) by Client at
// match start; settings UI updates it live for preview.
void  setHudTextScale(float s);
float currentHudTextScale();

// Suppresses the next mouse-button-released event from registering as a click on
// any drawButton-family widget. Use this whenever you switch UI phases on a click
// (Match summary MAIN MENU -> Menu, pause MAIN MENU -> Menu, Settings BACK ->
// Menu, etc.) -- raylib reports IsMouseButtonReleased() true on the frame AFTER
// the release, so if the new screen renders that same frame the click leaks
// through to whatever button sits under the mouse there.
void swallowNextClick();

// ---- Screen-size scaling ----
// All UI screens (menus, settings, lobby, HUD overlays) are LAID OUT against a
// reference resolution of 1280 x 720. uiScale() returns the factor relative to
// that reference for the current window size; widgets multiply font sizes and
// dimensions by this factor so a 2560x1440 fullscreen window renders the same
// content at 2x and a 1024x768 windowed instance at ~0.8x. min(sx, sy) is used
// (not the max) so an ultrawide aspect ratio doesn't stretch elements vertically.
//
// Clamped to a sensible band: at very small windows (< ~50% of reference) we
// stop shrinking so the text stays readable; at very large windows (> ~3x) we
// stop growing so menus don't dominate huge displays. The caller is welcome to
// multiply this by any local accessibility factor (e.g. currentHudTextScale()
// for HUD text) -- the two scales compose cleanly.
constexpr int   kUiReferenceW = 1280;
constexpr int   kUiReferenceH = 720;
float           uiScale(int screen_w, int screen_h);

// Convenience: scale a base-resolution pixel value to the current screen and
// round to int. Used by menus / HUD to size fonts + dimensions in one call.
//   uiPx(sw, sh, 44) == "44 pixels in the 1280x720 design space"
inline int      uiPx(int screen_w, int screen_h, int base_px) {
    return static_cast<int>(base_px * uiScale(screen_w, screen_h) + 0.5f);
}

} // namespace cr
