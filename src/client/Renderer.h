#pragma once

#include "Camera.h"
#include "Interpolator.h"
#include "core/Tuning.h"
#include "raylib.h"

#include <vector>

namespace cr {

// Accessibility: which color palette to use for cells. 0 = default, 1..3 are
// colorblind-friendly variants tuned for Deuteranopia / Protanopia / Tritanopia.
// Globally read by colorForPlayer; set from Client::applyLoadedSave at match start.
enum class PaletteMode : uint8_t {
    Default       = 0,
    Deuteranopia  = 1, // green-red weakness (most common)
    Protanopia    = 2, // red weakness
    Tritanopia    = 3, // blue-yellow weakness
};

// Set globally (process-wide). Cheap; intended to be called rarely (settings change).
void setPaletteMode(PaletteMode m);
PaletteMode currentPaletteMode();

// High-contrast cell outlines (accessibility). When on, cell outlines are drawn with
// a multi-pass thicker white stroke so cell boundaries pop against busy backgrounds.
void setHighContrast(bool on);
bool currentHighContrast();

// Per-player palette. Exposed so the feel layer (particles, popups) can match cell colors.
// The actual table is selected by the current PaletteMode.
Color colorForPlayer(PlayerId p);

// Free GPU resources owned by the renderer (the black-hole shader + 1x1 texture).
// Must be called before raylib's CloseWindow() -- otherwise the shader / texture leak
// at shutdown and (under some drivers) raylib logs a "trying to delete after context
// destroyed" warning. Safe to call multiple times; safe to call without ever drawing
// a black hole (lazy-init means resources are only allocated on first use).
void unloadRendererGpuResources();

// Renderer. Caller wraps drawWorld() in BeginMode2D/EndMode2D so it can share the camera
// with the feel layer (particles, popups) and apply screen shake. Takes a view AABB so it
// can frustum-cull entities; with a 16k x 16k world and a typical 1280 x 720 viewport at
// zoom 1, only ~0.4% of the food is on-screen.
class Renderer {
public:
    void drawWorld(const Interpolator&     interp,
                   const Tuning&           tuning,
                   float                   alpha,
                   Vec2                    view_min,
                   Vec2                    view_max,
                   EntityId                watched_cell = INVALID_ENTITY,
                   PlayerId                watched_player = INVALID_PLAYER,
                   int                     watched_player_level = 1) const;

private:
    // Scratch vectors reused across frames so drawWorld doesn't allocate per call.
    mutable std::vector<const CellSnap*> sort_order_;
};

} // namespace cr
