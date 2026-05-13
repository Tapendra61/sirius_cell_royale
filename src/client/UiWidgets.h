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

} // namespace cr
