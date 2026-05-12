#include "Renderer.h"

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cr {

namespace {

constexpr float kPi = 3.14159265358979323846f;

Color colorForPlayer(PlayerId p) {
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

Color foodColor(const FoodSnap& f) {
    if (lengthSq(f.vel) > 100.0f * 100.0f) {
        // In-flight ejected pellet: brighter.
        return Color{230, 230, 120, 255};
    }
    if (f.mass > 5.0f) {
        // Settled pellet (post-flight); slightly different from ambient food.
        return Color{200, 200, 130, 255};
    }
    return Color{120, 220, 130, 255};
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

void drawVirus(const VirusSnap& v) {
    const float r       = cellRadius(v.mass);
    const float inner_r = r * 0.85f;
    const Color fill    = Color{60, 175, 80, 255};
    const Color spike   = Color{30, 120, 50, 255};

    DrawCircleV(Vector2{v.pos.x, v.pos.y}, r, fill);
    // Spikes: 14 thin triangles around the rim.
    const int spikes = 14;
    for (int i = 0; i < spikes; ++i) {
        float a0 = (i        ) * (2.0f * kPi / spikes);
        float a1 = (i + 0.45f) * (2.0f * kPi / spikes);
        float a2 = (i + 1.0f ) * (2.0f * kPi / spikes);
        Vector2 p0{v.pos.x + std::cos(a0) * inner_r,
                   v.pos.y + std::sin(a0) * inner_r};
        Vector2 p1{v.pos.x + std::cos(a1) * (r * 1.15f),
                   v.pos.y + std::sin(a1) * (r * 1.15f)};
        Vector2 p2{v.pos.x + std::cos(a2) * inner_r,
                   v.pos.y + std::sin(a2) * inner_r};
        // raylib's DrawTriangle expects CCW winding; flip if necessary.
        DrawTriangle(p0, p2, p1, spike);
    }
    DrawCircleLinesV(Vector2{v.pos.x, v.pos.y}, r, Color{20, 80, 30, 255});
}

void drawCell(Vec2 pos, const CellSnap& c, bool watched, double now_sec) {
    float r       = cellRadius(c.mass);
    Color fill    = colorForPlayer(c.owner);
    Color outline = outlineFor(fill);

    if (c.invuln) {
        // White flash overlay; pulse alpha so it reads as "right now."
        float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(now_sec * 18.0));
        fill = Color{
            static_cast<unsigned char>(std::min(255.0f, fill.r + 120.0f * pulse)),
            static_cast<unsigned char>(std::min(255.0f, fill.g + 120.0f * pulse)),
            static_cast<unsigned char>(std::min(255.0f, fill.b + 120.0f * pulse)),
            255,
        };
    }
    if (c.god) {
        outline = Color{255, 220, 60, 255};
    }

    DrawCircleV(Vector2{pos.x, pos.y}, r, fill);
    DrawCircleLinesV(Vector2{pos.x, pos.y}, r, outline);
    if (c.dashing) {
        DrawCircleLinesV(Vector2{pos.x, pos.y}, r + 4.0f,
                         Color{255, 255, 255, 200});
    }

    // Phase 5: bot cells display a personality letter + id; the human player gets "P<id>".
    static const char kPersonalityLetters[] = {'P', 'G', 'C', 'H', 'h', 'R'};
    char letter = (c.personality_tag < sizeof(kPersonalityLetters))
                      ? kPersonalityLetters[c.personality_tag]
                      : '?';
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%c%u", letter, static_cast<unsigned>(c.owner));
    int font_size = std::max(12, static_cast<int>(r * 0.5f));
    int text_w    = MeasureText(buf, font_size);
    DrawText(buf,
             static_cast<int>(pos.x) - text_w / 2,
             static_cast<int>(pos.y) - font_size / 2,
             font_size, WHITE);

    // Dash cooldown ring (watched cell only -- keeps the scene quiet for bots).
    if (watched) {
        float ring_r = r + 8.0f;
        DrawCircleLinesV(Vector2{pos.x, pos.y}, ring_r, Color{255, 255, 255, 60});
        // Filled arc from 0 to cooldown_norm * 2*pi by sampling segments.
        const int seg = 32;
        float a_end   = c.dash_cooldown_norm * 2.0f * kPi - kPi * 0.5f;
        float a_start = -kPi * 0.5f;
        for (int i = 0; i < seg; ++i) {
            float t0 = static_cast<float>(i)     / seg;
            float t1 = static_cast<float>(i + 1) / seg;
            float a0 = lerp(a_start, a_end, t0);
            float a1 = lerp(a_start, a_end, t1);
            Vector2 q0{pos.x + std::cos(a0) * ring_r, pos.y + std::sin(a0) * ring_r};
            Vector2 q1{pos.x + std::cos(a1) * ring_r, pos.y + std::sin(a1) * ring_r};
            DrawLineEx(q0, q1, 3.0f, Color{255, 255, 255, 230});
        }
    }
}

} // namespace

void Renderer::drawWorld(const Interpolator&     interp,
                         const CameraController& camera,
                         const Tuning&           tuning,
                         int                     screen_w,
                         int                     screen_h,
                         float                   alpha,
                         EntityId                watched_cell) const {
    if (!interp.hasCurr()) return;

    const Snapshot& curr     = interp.curr();
    const bool      have_prev = interp.hasPrev();
    const Snapshot& prev     = interp.prev();

    Camera2D cam = camera.toCamera2D(screen_w, screen_h);
    BeginMode2D(cam);

    drawWorldGrid(tuning.world_width, tuning.world_height);

    // Food (interpolate in-flight pellets so they don't visibly jitter at sim rate).
    for (const auto& f : curr.food) {
        Vec2 pos = f.pos;
        if (have_prev && lengthSq(f.vel) > 1.0f) {
            for (const auto& fp : prev.food) {
                if (fp.id == f.id) {
                    pos = lerp(fp.pos, f.pos, alpha);
                    break;
                }
            }
        }
        DrawCircleV(Vector2{pos.x, pos.y}, foodRadius(f.mass), foodColor(f));
    }

    // Viruses (drift if pushed; interpolate to smooth that motion).
    for (const auto& v : curr.viruses) {
        VirusSnap drawn = v;
        if (have_prev) {
            for (const auto& vp : prev.viruses) {
                if (vp.id == v.id) {
                    drawn.pos = lerp(vp.pos, v.pos, alpha);
                    break;
                }
            }
        }
        drawVirus(drawn);
    }

    // Cells with interpolation. Largest first so smaller pieces overlap on top.
    std::vector<const CellSnap*> order;
    order.reserve(curr.cells.size());
    for (const auto& c : curr.cells) order.push_back(&c);
    std::sort(order.begin(), order.end(),
              [](const CellSnap* a, const CellSnap* b) { return a->mass > b->mass; });

    double now_sec = GetTime();
    for (const CellSnap* cp : order) {
        const CellSnap& c = *cp;
        Vec2 pos = c.pos;
        if (have_prev) {
            for (const auto& prev_c : prev.cells) {
                if (prev_c.id == c.id) {
                    pos = lerp(prev_c.pos, c.pos, alpha);
                    break;
                }
            }
        }
        drawCell(pos, c, /*watched=*/c.id == watched_cell, now_sec);
    }

    EndMode2D();
}

} // namespace cr
