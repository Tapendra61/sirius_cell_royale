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

// Per-match player-name registry. Cell nameplates prefer the registered name
// over the default "<letter><id>" fallback. Client owns the lifetime; it sets
// the watched player's name at match start and any peer names as they arrive
// over the wire. clearPlayerNames() is called at match start so the previous
// match's table doesn't leak in (renderer state is process-wide).
void setPlayerName(PlayerId p, const char* name);
void clearPlayerNames();

// Wipe the per-cell birth-grow-in-animation timestamps. Called at match
// start so a fresh match's cells don't carry "first seen at t=180s" stamps
// from the previous one (which would suppress the grow-in animation).
// Renderer state is process-wide, same lifecycle as clearPlayerNames.
void clearCellBirthAnimations();

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
    // Paints the screen-space backdrop (subtle vertical gradient + soft
    // corner vignette). Called by Client BEFORE BeginMode2D so it doesn't
    // scroll with the camera -- a fixed frame that makes the play area feel
    // more grounded than a flat clear color. Independent from drawWorld so
    // it can be re-used for the chroma render-target path (which also wants
    // the gradient baked in).
    void drawScreenBackdrop(int screen_w, int screen_h) const;

    void drawWorld(const Interpolator&     interp,
                   const Tuning&           tuning,
                   float                   alpha,
                   Vec2                    view_min,
                   Vec2                    view_max,
                   EntityId                watched_cell = INVALID_ENTITY,
                   PlayerId                watched_player = INVALID_PLAYER,
                   int                     watched_player_level = 1) const;

    // Bottom-right corner minimap. Drawn in SCREEN space (no BeginMode2D), so the
    // caller must NOT wrap this in the world camera. Reads the current snapshot for
    // cell / black-hole positions and draws a frustum rectangle from view_min/view_max
    // so the player can tell where they're looking on the full 16k world.
    //
    // Cells are dot-per-cell, scaled by mass so threats are obvious at a glance. Food
    // isn't drawn (3600 dots would just be noise). Pickups skipped for the same reason.
    void drawMinimap(const Interpolator& interp,
                     int                 world_w,
                     int                 world_h,
                     Vec2                view_min,
                     Vec2                view_max,
                     int                 screen_w,
                     int                 screen_h,
                     PlayerId            watched_player) const;

private:
    // Scratch vectors reused across frames so drawWorld doesn't allocate per call.
    mutable std::vector<const CellSnap*> sort_order_;
};

} // namespace cr
