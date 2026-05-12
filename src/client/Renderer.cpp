#include "Renderer.h"

#include "raylib.h"

#include <algorithm>
#include <cstdio>

namespace cr {

namespace {

Color colorForPlayer(PlayerId p) {
    // Deterministic color per player. Player 1 is blue; others pick from a small palette.
    static const Color palette[] = {
        Color{ 64, 156, 255, 255},
        Color{255, 120,  80, 255},
        Color{120, 220, 120, 255},
        Color{255, 200,  60, 255},
        Color{200, 120, 255, 255},
        Color{ 80, 220, 220, 255},
    };
    if (p == INVALID_PLAYER) return Color{180, 180, 180, 255};
    return palette[(p - 1) % (sizeof(palette) / sizeof(palette[0]))];
}

Color outlineFor(Color c) {
    return Color{
        static_cast<unsigned char>(c.r * 0.5f),
        static_cast<unsigned char>(c.g * 0.5f),
        static_cast<unsigned char>(c.b * 0.5f),
        255,
    };
}

void drawWorldGrid(int world_w, int world_h) {
    const int step = 400;
    const Color line = {40, 44, 52, 255};
    for (int x = 0; x <= world_w; x += step) {
        DrawLine(x, 0, x, world_h, line);
    }
    for (int y = 0; y <= world_h; y += step) {
        DrawLine(0, y, world_w, y, line);
    }
    DrawRectangleLinesEx(Rectangle{0.0f, 0.0f,
                                   static_cast<float>(world_w),
                                   static_cast<float>(world_h)},
                         4.0f, Color{90, 100, 115, 255});
}

} // namespace

void Renderer::drawWorld(const Interpolator&     interp,
                         const CameraController& camera,
                         const Tuning&           tuning,
                         int                     screen_w,
                         int                     screen_h,
                         float                   alpha) const {
    if (!interp.hasCurr()) return;

    const Snapshot& curr = interp.curr();
    const bool      have_prev = interp.hasPrev();
    const Snapshot& prev = interp.prev();

    Camera2D cam = camera.toCamera2D(screen_w, screen_h);
    BeginMode2D(cam);

    drawWorldGrid(tuning.world_width, tuning.world_height);

    // Food: no interpolation needed (static).
    for (const auto& f : curr.food) {
        DrawCircleV(Vector2{f.pos.x, f.pos.y}, 6.0f, Color{120, 220, 130, 255});
    }

    // Cells: interpolated by alpha against the previous snapshot if available.
    for (const auto& c : curr.cells) {
        Vec2 pos = c.pos;
        if (have_prev) {
            for (const auto& cp : prev.cells) {
                if (cp.id == c.id) {
                    pos = lerp(cp.pos, c.pos, alpha);
                    break;
                }
            }
        }
        float r       = cellRadius(c.mass);
        Color fill    = colorForPlayer(c.owner);
        Color outline = outlineFor(fill);
        DrawCircleV(Vector2{pos.x, pos.y}, r, fill);
        DrawCircleLinesV(Vector2{pos.x, pos.y}, r, outline);

        // Cell name. Phase 5 supplies real bot names; for now: "P<id>".
        char buf[32];
        std::snprintf(buf, sizeof(buf), "P%u", static_cast<unsigned>(c.owner));
        int font_size = std::max(12, static_cast<int>(r * 0.5f));
        int text_w    = MeasureText(buf, font_size);
        DrawText(buf,
                 static_cast<int>(pos.x) - text_w / 2,
                 static_cast<int>(pos.y) - font_size / 2,
                 font_size, WHITE);
    }

    EndMode2D();
}

} // namespace cr
