#include "Renderer.h"

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cr {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Globally-shared accessibility state. setPaletteMode/setHighContrast write; the
// drawing helpers read. Process-lifetime; not thread-safe (drawing is single-threaded).
PaletteMode s_palette_mode  = PaletteMode::Default;
bool        s_high_contrast = false;

// Default vibrant palette (used everywhere outside cell-color identification).
const Color kPaletteDefault[] = {
    Color{ 64, 156, 255, 255}, // blue
    Color{255, 120,  80, 255}, // orange
    Color{120, 220, 120, 255}, // green
    Color{255, 200,  60, 255}, // yellow
    Color{200, 120, 255, 255}, // purple
    Color{ 80, 220, 220, 255}, // cyan
};

// Deuteranopia: green-deficient. Replace conflicting green/red pairs with blue/yellow.
// Aim for distinguishable luminance + hue along the blue/yellow axis.
const Color kPaletteDeut[] = {
    Color{ 64, 156, 255, 255}, // bright blue
    Color{255, 180,  40, 255}, // gold (replaces orange/red)
    Color{200, 200, 230, 255}, // pale blue (replaces green)
    Color{255, 235, 130, 255}, // pale yellow
    Color{120,  80, 200, 255}, // indigo
    Color{120, 200, 255, 255}, // sky
};

// Protanopia: red-deficient. Similar mapping to Deut but shifted slightly cooler.
const Color kPaletteProt[] = {
    Color{ 80, 170, 255, 255},
    Color{240, 200,  80, 255},
    Color{210, 215, 230, 255},
    Color{255, 230, 110, 255},
    Color{100,  90, 200, 255},
    Color{ 90, 200, 230, 255},
};

// Tritanopia: blue-deficient. Use the red/green axis for distinction instead.
const Color kPaletteTrit[] = {
    Color{220,  90,  90, 255},
    Color{255, 150,  60, 255},
    Color{120, 200, 120, 255},
    Color{220, 200, 100, 255},
    Color{210, 110, 160, 255},
    Color{170, 220,  90, 255},
};

const Color* selectPalette(PaletteMode m) {
    switch (m) {
        case PaletteMode::Deuteranopia: return kPaletteDeut;
        case PaletteMode::Protanopia:   return kPaletteProt;
        case PaletteMode::Tritanopia:   return kPaletteTrit;
        case PaletteMode::Default:
        default:                        return kPaletteDefault;
    }
}

} // namespace (anonymous) -- pop briefly to define public color helpers

void setPaletteMode(PaletteMode m)  { s_palette_mode  = m; }
PaletteMode currentPaletteMode()    { return s_palette_mode; }
void setHighContrast(bool on)       { s_high_contrast = on; }
bool currentHighContrast()          { return s_high_contrast; }

Color colorForPlayer(PlayerId p) {
    if (p == INVALID_PLAYER) return Color{180, 180, 180, 255};
    const Color* pal = selectPalette(s_palette_mode);
    constexpr int kPaletteSize = 6;
    return pal[(p - 1) % kPaletteSize];
}

namespace { // re-open anonymous namespace for the remaining helpers

Color outlineFor(Color c) {
    return Color{
        static_cast<unsigned char>(c.r * 0.5f),
        static_cast<unsigned char>(c.g * 0.5f),
        static_cast<unsigned char>(c.b * 0.5f),
        255,
    };
}

Color foodColor(const FoodSnap& f) {
    // In-flight ejected pellet: bright yellow so it reads as a thrown thing.
    if (lengthSq(f.vel) > 50.0f * 50.0f) {
        return Color{255, 220, 120, 255};
    }
    // Tier colors (higher mass first; ejected pellets settle at mass 18 -> dim yellow).
    if (f.mass >= 30.0f) return Color{220,  60, 140, 255}; // legendary base
                                                            // (renderer pulses this)
    if (f.mass >= 15.0f) return Color{220, 200, 140, 255}; // settled pellet
    if (f.mass >= 10.0f) return Color{255, 210,  90, 255}; // epic   (mass 12)
    if (f.mass >= 5.0f)  return Color{120, 230, 220, 255}; // rare   (mass 6)
    if (f.mass >= 2.0f)  return Color{180, 240, 110, 255}; // uncommon (mass 3)
    return Color{120, 220, 130, 255};                       // common (mass 1)
}

// Background grid clamped to the visible AABB. Saves drawing thousands of pixels of line
// that the GPU would clip anyway, and shortens the world border to just the visible edges
// where applicable.
void drawWorldGrid(int world_w, int world_h, Vec2 view_min, Vec2 view_max) {
    const int step = 400;
    const Color line = {40, 44, 52, 255};
    const float xmin = std::max(view_min.x, 0.0f);
    const float xmax = std::min(view_max.x, static_cast<float>(world_w));
    const float ymin = std::max(view_min.y, 0.0f);
    const float ymax = std::min(view_max.y, static_cast<float>(world_h));
    if (xmin > xmax || ymin > ymax) {
        // View is entirely outside the world; still draw the border below.
    } else {
        int xs = static_cast<int>(std::floor(xmin / step)) * step;
        int xe = static_cast<int>(std::ceil(xmax  / step)) * step;
        int ys = static_cast<int>(std::floor(ymin / step)) * step;
        int ye = static_cast<int>(std::ceil(ymax  / step)) * step;
        for (int x = xs; x <= xe; x += step) {
            if (x < 0 || x > world_w) continue;
            DrawLine(x, static_cast<int>(ymin),
                     x, static_cast<int>(ymax), line);
        }
        for (int y = ys; y <= ye; y += step) {
            if (y < 0 || y > world_h) continue;
            DrawLine(static_cast<int>(xmin), y,
                     static_cast<int>(xmax), y, line);
        }
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
        DrawTriangle(p0, p2, p1, spike);
    }
    DrawCircleLinesV(Vector2{v.pos.x, v.pos.y}, r, Color{20, 80, 30, 255});
}

void drawCell(Vec2 pos, const CellSnap& c, bool watched, double now_sec, bool flair_star) {
    float r       = cellRadius(c.mass);
    Color fill    = colorForPlayer(c.owner);
    Color outline = outlineFor(fill);

    if (c.invuln) {
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

    if (c.dash_telegraph_norm > 0.0f) {
        float t   = c.dash_telegraph_norm;
        float a   = t * t;
        float bump = a * 70.0f;
        fill.r = static_cast<unsigned char>(std::min(255, static_cast<int>(fill.r) + static_cast<int>(bump)));
        fill.g = static_cast<unsigned char>(std::min(255, static_cast<int>(fill.g) + static_cast<int>(bump)));
    }

    DrawCircleV(Vector2{pos.x, pos.y}, r, fill);
    if (s_high_contrast) {
        // 3-pass thick white outline. Reads as a hard border against busy backgrounds.
        DrawCircleLinesV(Vector2{pos.x, pos.y}, r,        Color{255, 255, 255, 255});
        DrawCircleLinesV(Vector2{pos.x, pos.y}, r + 1.0f, Color{255, 255, 255, 220});
        DrawCircleLinesV(Vector2{pos.x, pos.y}, r - 1.0f, Color{255, 255, 255, 180});
    } else {
        DrawCircleLinesV(Vector2{pos.x, pos.y}, r, outline);
    }

    if (c.is_elite) {
        float phase  = static_cast<float>(c.id % 64) * 0.1f;
        float pulse  = 0.5f + 0.5f * std::sin(static_cast<float>(now_sec) * 2.4f + phase);
        float outset = std::max(10.0f, r * 0.22f);
        float halo_r = r + outset + pulse * outset * 0.55f;
        unsigned char a_bright = static_cast<unsigned char>(70.0f + pulse * 185.0f);
        unsigned char a_dim    = static_cast<unsigned char>(a_bright * 0.55f);
        DrawCircleLinesV(Vector2{pos.x, pos.y}, halo_r,
                         Color{240, 220, 255, a_bright});
        DrawCircleLinesV(Vector2{pos.x, pos.y}, halo_r + 1.5f,
                         Color{220, 195, 255, a_bright});
        DrawCircleLinesV(Vector2{pos.x, pos.y}, halo_r + 3.0f,
                         Color{195, 165, 250, a_dim});
        DrawCircleLinesV(Vector2{pos.x, pos.y}, halo_r + 5.0f,
                         Color{170, 140, 240,
                               static_cast<unsigned char>(a_dim * 0.5f)});
    }

    if (c.dash_telegraph_norm > 0.0f) {
        float t   = c.dash_telegraph_norm;
        float a   = t * t;
        float outset_min = 8.0f;
        float outset = std::max(outset_min, r * 0.15f);
        float ring_r = r + outset + a * outset;
        unsigned char alpha = static_cast<unsigned char>(80.0f + a * 170.0f);
        DrawCircleLinesV(Vector2{pos.x, pos.y}, ring_r,
                         Color{255, 230, 80, alpha});
        DrawCircleLinesV(Vector2{pos.x, pos.y}, ring_r * 1.05f,
                         Color{255, 180, 30, static_cast<unsigned char>(alpha * 0.70f)});
        DrawCircleLinesV(Vector2{pos.x, pos.y}, ring_r * 1.10f,
                         Color{255, 130, 0, static_cast<unsigned char>(alpha * 0.45f)});
    }

    if (c.dashing) {
        DrawCircleLinesV(Vector2{pos.x, pos.y}, r + 4.0f,
                         Color{255, 255, 255, 200});
    }

    static const char kPersonalityLetters[] = {'P', 'G', 'C', 'H', 'h', 'R'};
    char letter = (c.personality_tag < sizeof(kPersonalityLetters))
                      ? kPersonalityLetters[c.personality_tag]
                      : '?';
    char buf[32];
    // Phase 8 cosmetic unlock: a star prefix on the watched player's name at L20+.
    // (Other cosmetic tiers -- trail colors, skins -- deferred to a focused pass.)
    if (flair_star) {
        std::snprintf(buf, sizeof(buf), "*%c%u", letter, static_cast<unsigned>(c.owner));
    } else {
        std::snprintf(buf, sizeof(buf), "%c%u", letter, static_cast<unsigned>(c.owner));
    }
    int font_size = std::max(12, static_cast<int>(r * 0.5f));
    int text_w    = MeasureText(buf, font_size);
    Color text_color = flair_star ? Color{255, 230, 130, 255} : WHITE;
    DrawText(buf,
             static_cast<int>(pos.x) - text_w / 2,
             static_cast<int>(pos.y) - font_size / 2,
             font_size, text_color);

    if (watched) {
        float ring_r = r + 8.0f;
        DrawCircleLinesV(Vector2{pos.x, pos.y}, ring_r, Color{255, 255, 255, 60});
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

// Cull-test helpers. AABB-vs-point with margin embedded in caller-side bounds.
inline bool pointInView(Vec2 p, Vec2 vmin, Vec2 vmax) {
    return p.x >= vmin.x && p.x <= vmax.x && p.y >= vmin.y && p.y <= vmax.y;
}
inline bool circleInView(Vec2 p, float r, Vec2 vmin, Vec2 vmax) {
    return p.x + r >= vmin.x && p.x - r <= vmax.x
        && p.y + r >= vmin.y && p.y - r <= vmax.y;
}

} // namespace

void Renderer::drawWorld(const Interpolator& interp,
                         const Tuning&       tuning,
                         float               alpha,
                         Vec2                view_min,
                         Vec2                view_max,
                         EntityId            watched_cell,
                         PlayerId            watched_player,
                         int                 watched_player_level) const {
    if (!interp.hasCurr()) return;

    const Snapshot& curr     = interp.curr();
    const bool      have_prev = interp.hasPrev();
    const Snapshot& prev     = interp.prev();

    drawWorldGrid(tuning.world_width, tuning.world_height, view_min, view_max);

    // Single GetTime() call for the whole frame; used by halo pulses, invuln flashes, etc.
    const double now_sec = GetTime();

    // Food: frustum-cull first (3600 entries × 16k² world means typically <1% on-screen).
    // High-tier ambient food gets a pulsing halo so rare drops stand out from far away.
    for (const auto& f : curr.food) {
        if (!pointInView(f.pos, view_min, view_max)) continue;
        Vec2 pos = f.pos;
        if (have_prev && lengthSq(f.vel) > 1.0f) {
            for (const auto& fp : prev.food) {
                if (fp.id == f.id) {
                    pos = lerp(fp.pos, f.pos, alpha);
                    break;
                }
            }
        }
        Color c = foodColor(f);
        float r = foodRadius(f.mass);
        const bool stationary = lengthSq(f.vel) < 50.0f * 50.0f;

        // Legendary food (mass 36): pulse the body colour between deep purple and
        // bright red so it visibly throbs in the world. A faster phase than the
        // regular halo pulse separates it from the gold-tier "halo only" pulse.
        const bool legendary = stationary && f.mass >= 30.0f;
        if (legendary) {
            float phase = static_cast<float>(f.id % 64) * 0.13f;
            float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(now_sec) * 5.5f + phase);
            // cool = deep magenta-purple, hot = saturated red
            const Color cool{170,  50, 200, 255};
            const Color hot {255,  80,  90, 255};
            c.r = static_cast<unsigned char>(cool.r + (hot.r - cool.r) * pulse);
            c.g = static_cast<unsigned char>(cool.g + (hot.g - cool.g) * pulse);
            c.b = static_cast<unsigned char>(cool.b + (hot.b - cool.b) * pulse);
        }

        if (stationary && f.mass >= 5.0f) {
            float phase = static_cast<float>(f.id % 64) * 0.1f;
            float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(now_sec) * 4.0f + phase);
            // Legendary halo is brighter and bigger so the drop reads as "rare" from
            // far across the world.
            float halo_strength = legendary ? 1.9f
                                : (f.mass >= 10.0f ? 1.0f : 0.55f);
            float halo_r_mult   = legendary ? 2.5f : 1.9f;
            unsigned char glow_a = static_cast<unsigned char>(
                std::min(255.0f, (35.0f + pulse * 50.0f) * halo_strength));
            DrawCircleV(Vector2{pos.x, pos.y}, r * halo_r_mult,
                        Color{c.r, c.g, c.b, glow_a});
        }
        DrawCircleV(Vector2{pos.x, pos.y}, r, c);
    }

    // Viruses (drift if pushed; interpolate to smooth that motion).
    for (const auto& v : curr.viruses) {
        float vr = cellRadius(v.mass);
        if (!circleInView(v.pos, vr * 1.2f, view_min, view_max)) continue;
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

    // Cells with interpolation. Largest first so smaller pieces overlap on top. The sort
    // vector is a renderer member so we don't allocate per frame.
    sort_order_.clear();
    sort_order_.reserve(curr.cells.size());
    for (const auto& c : curr.cells) sort_order_.push_back(&c);
    std::sort(sort_order_.begin(), sort_order_.end(),
              [](const CellSnap* a, const CellSnap* b) { return a->mass > b->mass; });

    for (const CellSnap* cp : sort_order_) {
        const CellSnap& c = *cp;
        float r = cellRadius(c.mass);
        // Cull by interpolated position (use curr; close enough -- entity won't move more
        // than a few px during interp). Generous radius margin because of halos.
        if (!circleInView(c.pos, r + 24.0f, view_min, view_max)) continue;
        Vec2 pos = c.pos;
        if (have_prev) {
            for (const auto& prev_c : prev.cells) {
                if (prev_c.id == c.id) {
                    pos = lerp(prev_c.pos, c.pos, alpha);
                    break;
                }
            }
        }
        const bool flair = (watched_player != INVALID_PLAYER)
                          && (c.owner == watched_player)
                          && (watched_player_level >= 20);
        drawCell(pos, c, /*watched=*/c.id == watched_cell, now_sec, flair);
    }
}

} // namespace cr
