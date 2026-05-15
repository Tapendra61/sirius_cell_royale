#include "Renderer.h"

#include "ai/BotPersonality.h" // letterForTag

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

// Procedural black-hole shader. Drawn onto a 1x1 white texture scaled to fit each
// hole's disc-extent square. The fragment shader receives fragTexCoord in [0,1],
// remaps to centered [-1,1], and produces a swirling accretion disc + dark core.
// One global Shader + Texture loaded lazily on first call (after raylib's window is
// up). raylib auto-cleans both on CloseWindow().
const char* const kBlackHoleFS = R"GLSL(
#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform float u_time;
uniform float u_horizon;   // 0..1, event horizon radius

void main() {
    vec2  uv = (fragTexCoord - 0.5) * 2.0;
    float r  = length(uv);
    if (r > 1.0) discard;

    float theta = atan(uv.y, uv.x);

    // Frame-dragging swirl: angular shift grows toward the centre, so streamlines
    // wind tighter near the horizon. Time term is the slow rotation rate.
    float swirl = 4.5 * pow(1.0 - r, 1.4);
    float spin  = theta + u_time * 0.22 + swirl;

    // Two-armed spiral pattern, sharpened so the arms read crisp.
    float arms = 0.5 + 0.5 * sin(spin * 2.0 + r * 5.0);
    arms = pow(arms, 2.5);

    // Accretion brightness: peaks just outside the horizon, falls off both inward
    // (into the void) and outward (into the pull halo).
    float horizon_edge   = smoothstep(u_horizon * 0.95, u_horizon * 1.08, r);
    float outer_falloff  = 1.0 - smoothstep(u_horizon * 1.2, 0.95, r);
    float disc_brightness = horizon_edge * outer_falloff;

    // Palette: deep indigo void -> dark red disc dust -> hot orange inner.
    vec3 col_void  = vec3(0.06, 0.02, 0.12);
    vec3 col_dust  = vec3(0.45, 0.06, 0.18);
    vec3 col_blaze = vec3(1.00, 0.55, 0.18);

    float depth = smoothstep(1.0, u_horizon * 1.1, r); // 0 at edge, 1 near horizon
    vec3  base  = mix(col_void, col_dust, depth);
    vec3  color = mix(base, col_blaze, disc_brightness * arms);

    // Event horizon: pure black inside the inner radius with a thin smoothed edge.
    color *= smoothstep(u_horizon * 0.95, u_horizon * 1.02, r);

    // Outer alpha: soft fade so the disc blends into the world background.
    float alpha = smoothstep(1.0, 0.55, r);

    finalColor = vec4(color, alpha);
}
)GLSL";

struct BlackHoleGfx {
    Shader     shader{};
    Texture2D  white{};
    int        loc_time    = -1;
    int        loc_horizon = -1;
    bool       initialized = false;
    bool       failed      = false;
};

BlackHoleGfx g_bh_gfx;

void ensureBlackHoleGfx() {
    if (g_bh_gfx.initialized || g_bh_gfx.failed) return;

    g_bh_gfx.shader = LoadShaderFromMemory(nullptr, kBlackHoleFS);
    if (g_bh_gfx.shader.id == 0) {
        g_bh_gfx.failed = true;
        return;
    }
    g_bh_gfx.loc_time    = GetShaderLocation(g_bh_gfx.shader, "u_time");
    g_bh_gfx.loc_horizon = GetShaderLocation(g_bh_gfx.shader, "u_horizon");

    // 1x1 white texture for the shader pass. We avoid raylib's built-in shapes
    // texture because we want predictable UVs (DrawTexturePro maps source 0..1
    // across the destination, so fragTexCoord interpolates cleanly across the quad).
    Image img = GenImageColor(1, 1, WHITE);
    g_bh_gfx.white = LoadTextureFromImage(img);
    UnloadImage(img);

    g_bh_gfx.initialized = true;
}

void unloadBlackHoleGfx() {
    // Reverse of ensureBlackHoleGfx -- frees GPU resources at shutdown. Must run while
    // raylib's GL context is still alive (i.e. before CloseWindow). Idempotent: a
    // freshly-constructed BlackHoleGfx has initialized=false so a redundant call is
    // a no-op.
    if (g_bh_gfx.initialized) {
        UnloadTexture(g_bh_gfx.white);
        UnloadShader(g_bh_gfx.shader);
        g_bh_gfx = BlackHoleGfx{}; // reset to default-constructed (all fields cleared)
    } else if (g_bh_gfx.failed && g_bh_gfx.shader.id != 0) {
        // Shader compile failed but still left an id behind on some drivers; nuke it.
        UnloadShader(g_bh_gfx.shader);
        g_bh_gfx = BlackHoleGfx{};
    }
}

void drawBlackHole(const BlackHoleSnap& b, double now_sec) {
    const Vector2 c{b.pos.x, b.pos.y};
    const float   t = static_cast<float>(now_sec);

    ensureBlackHoleGfx();

    // Outer pull-ring tell -- soft purple disc + thin ring. Shows the player the
    // danger zone without dominating the screen. Drawn before the shader pass so
    // the accretion disc sits on top of it.
    {
        float pulse = 0.5f + 0.5f * std::sin(t * 0.9f);
        unsigned char a = static_cast<unsigned char>(24 + pulse * 20);
        DrawCircleV(c, b.pull_radius, Color{110, 40, 175, a});
        DrawCircleLinesV(c, b.pull_radius,
                         Color{170, 80, 220, static_cast<unsigned char>(a * 2)});
    }

    if (g_bh_gfx.initialized) {
        // Shader pass: draw a 1x1 white texture stretched to a square covering the
        // disc. The fragment shader does the visual work. Disc extent is ~2.2x the
        // event horizon so the accretion glow has fade-out room.
        const float disc_extent = b.radius * 2.2f;
        const Rectangle dst{c.x - disc_extent, c.y - disc_extent,
                            disc_extent * 2.0f, disc_extent * 2.0f};
        // Event horizon in shader UV space (uv is [-1, 1] across dst):
        const float horizon_norm = b.radius / disc_extent;

        BeginShaderMode(g_bh_gfx.shader);
        SetShaderValue(g_bh_gfx.shader, g_bh_gfx.loc_time,    &t,            SHADER_UNIFORM_FLOAT);
        SetShaderValue(g_bh_gfx.shader, g_bh_gfx.loc_horizon, &horizon_norm, SHADER_UNIFORM_FLOAT);
        DrawTexturePro(g_bh_gfx.white,
                       Rectangle{0, 0, 1, 1},
                       dst,
                       Vector2{0, 0},
                       0.0f,
                       WHITE);
        EndShaderMode();
    } else {
        // Fallback path if shader compilation failed (e.g. driver weirdness): a
        // plain dark core + red ring. Loses the swirl but keeps the entity visible.
        DrawCircleV(c, b.radius * 1.15f, Color{50, 10, 30, 200});
        DrawCircleV(c, b.radius,         Color{ 0,  0,  0, 255});
        DrawCircleLinesV(c, b.radius * 1.05f, Color{200, 50, 60, 220});
    }

    // Occupancy orbit dots -- drawn on top of the swirl so they read clearly. Slow
    // rotation so they don't fight the shader's animation.
    if (b.occupancy > 0) {
        const int   n       = std::min<int>(b.occupancy, 6);
        const float orbit_r = b.radius * 0.62f;
        const float omega   = 0.6f;
        for (int i = 0; i < n; ++i) {
            float a = i * (2.0f * kPi / n) + t * omega;
            Vector2 p{c.x + std::cos(a) * orbit_r, c.y + std::sin(a) * orbit_r};
            DrawCircleV(p, 2.5f, Color{240, 215, 255, 230});
        }
    }
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

// ---- Pickup colors + draw helper ----
struct PickupVisual {
    Color core;       // central body
    Color glow;       // halo color
    Color accent;     // decoration / icon
};

PickupVisual pickupVisual(PickupKind kind) {
    switch (kind) {
        case PickupKind::Shield:  return {Color{ 90, 200, 255, 255},
                                          Color{ 70, 180, 255, 255},
                                          Color{220, 245, 255, 255}};
        case PickupKind::Magnet:  return {Color{255, 150,  60, 255},
                                          Color{255, 120,  40, 255},
                                          Color{255, 230, 180, 255}};
        case PickupKind::Stealth: return {Color{170,  90, 220, 255},
                                          Color{120,  60, 200, 255},
                                          Color{220, 200, 245, 255}};
        case PickupKind::None:
        default:                  return {Color{200, 200, 200, 255},
                                          Color{160, 160, 160, 255},
                                          Color{240, 240, 240, 255}};
    }
}

void drawPickup(const PickupSnap& p, double now_sec) {
    const float r     = pickupRadius();
    PickupVisual vis  = pickupVisual(p.kind);

    // Slow halo pulse so pickups read as "valuable" from across the world.
    float phase = static_cast<float>(p.id % 64) * 0.07f;
    float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(now_sec) * 3.2f + phase);

    // Outer halo (very translucent).
    unsigned char halo_a = static_cast<unsigned char>(40.0f + pulse * 60.0f);
    DrawCircleV(Vector2{p.pos.x, p.pos.y}, r * 2.4f,
                Color{vis.glow.r, vis.glow.g, vis.glow.b, halo_a});

    // Mid halo.
    DrawCircleV(Vector2{p.pos.x, p.pos.y}, r * 1.6f,
                Color{vis.glow.r, vis.glow.g, vis.glow.b,
                      static_cast<unsigned char>(80 + pulse * 60.0f)});

    // Body.
    DrawCircleV(Vector2{p.pos.x, p.pos.y}, r, vis.core);
    DrawCircleLinesV(Vector2{p.pos.x, p.pos.y}, r,
                     Color{255, 255, 255, 200});

    // Per-kind decoration so the three powerups read distinctly without text labels.
    switch (p.kind) {
        case PickupKind::Shield: {
            // Buckler look: inner ring + 4 stud dots at cardinal directions.
            DrawCircleLinesV(Vector2{p.pos.x, p.pos.y}, r * 0.55f, vis.accent);
            for (int i = 0; i < 4; ++i) {
                float a = i * (kPi * 0.5f);
                Vector2 d{p.pos.x + std::cos(a) * r * 0.78f,
                          p.pos.y + std::sin(a) * r * 0.78f};
                DrawCircleV(d, 2.5f, vis.accent);
            }
            break;
        }
        case PickupKind::Magnet: {
            // Two opposing "poles" -- horizontal bars on either side.
            const float bw = r * 0.55f;
            const float bh = r * 0.30f;
            DrawRectangleRounded(
                Rectangle{p.pos.x - bw - 1.0f, p.pos.y - bh * 0.5f, bw, bh},
                0.45f, 4, vis.accent);
            DrawRectangleRounded(
                Rectangle{p.pos.x + 1.0f,      p.pos.y - bh * 0.5f, bw, bh},
                0.45f, 4, vis.accent);
            break;
        }
        case PickupKind::Stealth: {
            // Wispy shimmer: three concentric thin rings whose phase shifts with time,
            // so the icon feels "unstable" / about to vanish.
            for (int i = 0; i < 3; ++i) {
                float t = (now_sec * 1.4f + i * 0.4f);
                float k = 0.5f + 0.5f * std::sin(t);
                float rr = r * (0.30f + 0.20f * k);
                unsigned char a = static_cast<unsigned char>(80 + k * 120);
                DrawCircleLinesV(Vector2{p.pos.x, p.pos.y}, rr,
                                 Color{vis.accent.r, vis.accent.g, vis.accent.b, a});
            }
            break;
        }
        case PickupKind::None: break;
    }
}

void drawCell(Vec2 pos, const CellSnap& c, bool watched, double now_sec, bool flair_star) {
    // Fully hidden -- BH occupancy dots represent the cell; the body/label aren't
    // drawn at the pinned centre. Cells mid-entry-anim and mid-exit-anim DO render
    // (with a shrunken radius via blackhole_visual_scale).
    if (c.hiding || c.blackhole_visual_scale < 0.02f) return;

    const float scale = std::clamp(c.blackhole_visual_scale, 0.02f, 1.0f);
    float r           = cellRadius(c.mass) * scale;
    Color fill        = colorForPlayer(c.owner);
    Color outline     = outlineFor(fill);

    // Mid-animation cells also fade slightly so the shrink reads as "being
    // absorbed by the void" rather than just resizing.
    if (scale < 1.0f) {
        unsigned char a = static_cast<unsigned char>(scale * 255.0f);
        fill.a    = a;
        outline.a = a;
    }

    // Stealth visual: the cell fades to translucent so the player sees they've gone
    // "ghostly". Bots can't perceive them; the player still needs feedback.
    if (c.stealth_active) {
        // Pulse the alpha low-high low-high so stealth reads as "active" not "broken".
        float k = 0.5f + 0.5f * std::sin(static_cast<float>(now_sec) * 2.4f);
        unsigned char a = static_cast<unsigned char>(70 + k * 70);
        fill.a    = a;
        outline.a = a;
    }

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

    // ---- Power-up effect auras ----
    // Shield: pulsing cyan double-ring around the cell. Strong enough to read at a
    // glance so the player and other bots can both tell the cell is uneatable.
    if (c.shield_active) {
        float k = 0.5f + 0.5f * std::sin(static_cast<float>(now_sec) * 4.0f);
        unsigned char a_inner = static_cast<unsigned char>(160 + k * 80);
        unsigned char a_outer = static_cast<unsigned char>( 80 + k * 60);
        DrawCircleLinesV(Vector2{pos.x, pos.y}, r + 4.0f,
                         Color{120, 220, 255, a_inner});
        DrawCircleLinesV(Vector2{pos.x, pos.y}, r + 7.0f,
                         Color{ 90, 200, 255, a_outer});
        DrawCircleLinesV(Vector2{pos.x, pos.y}, r + 10.0f,
                         Color{ 70, 180, 255,
                                static_cast<unsigned char>(a_outer * 0.6f)});
    }
    // Magnet: short radial spokes pulsing outward to read as "pulling".
    if (c.magnet_active) {
        float k = static_cast<float>(now_sec) * 3.0f;
        for (int i = 0; i < 8; ++i) {
            float a   = i * (kPi * 0.25f);
            float ph  = 0.5f + 0.5f * std::sin(k + i * 0.6f);
            float inn = r + 4.0f;
            float out = r + 12.0f + ph * 8.0f;
            Vector2 a0{pos.x + std::cos(a) * inn, pos.y + std::sin(a) * inn};
            Vector2 a1{pos.x + std::cos(a) * out, pos.y + std::sin(a) * out};
            DrawLineEx(a0, a1, 2.0f,
                       Color{255, 150, 60,
                             static_cast<unsigned char>(100 + ph * 130)});
        }
    }

    char letter = ai::letterForTag(c.personality_tag);
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

void unloadRendererGpuResources() {
    // Currently just the black-hole shader / 1x1 texture. If we ever add more lazy GPU
    // resources (e.g. a virus-glow shader), hang their cleanup off this same function so
    // shutdown stays one call.
    unloadBlackHoleGfx();
}

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

    // Black holes: drawn before food/viruses/cells so the swirling visuals appear
    // as a backdrop, with cells layered on top -- except hiding cells, which the
    // black hole's occupancy dots represent instead.
    for (const auto& b : curr.blackholes) {
        if (!circleInView(b.pos, b.pull_radius, view_min, view_max)) continue;
        drawBlackHole(b, now_sec);
    }

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

    // Power-up pickups. They don't move, so no interpolation needed. Drawn before
    // viruses and cells so big cells visually overlap them (cells eat pickups).
    for (const auto& p : curr.pickups) {
        if (!circleInView(p.pos, pickupRadius() * 2.6f, view_min, view_max)) continue;
        drawPickup(p, now_sec);
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
        // Interpolate position AND visual scale between prev/curr so the smooth
        // black-hole transition reads smoothly at 60+ fps render while the sim
        // ticks at 30 Hz.
        Vec2  pos          = c.pos;
        float interp_scale = c.blackhole_visual_scale;
        if (have_prev) {
            for (const auto& prev_c : prev.cells) {
                if (prev_c.id == c.id) {
                    pos          = lerp(prev_c.pos, c.pos, alpha);
                    interp_scale = prev_c.blackhole_visual_scale
                                 + (c.blackhole_visual_scale
                                    - prev_c.blackhole_visual_scale) * alpha;
                    break;
                }
            }
        }
        const bool flair = (watched_player != INVALID_PLAYER)
                          && (c.owner == watched_player)
                          && (watched_player_level >= 20);
        // Temporarily swap the snap's visual_scale with the interpolated value so
        // drawCell uses smooth motion (we can't pass it separately without changing
        // the signature, and the snap is local to this loop).
        CellSnap c_local = c;
        c_local.blackhole_visual_scale = interp_scale;
        drawCell(pos, c_local, /*watched=*/c.id == watched_cell, now_sec, flair);
    }
}

void Renderer::drawMinimap(const Interpolator& interp,
                           int                 world_w,
                           int                 world_h,
                           Vec2                view_min,
                           Vec2                view_max,
                           int                 screen_w,
                           int                 screen_h,
                           PlayerId            watched_player) const {
    if (!interp.hasCurr()) return;

    // Layout: bottom-right corner. The minimap aspect mirrors the world aspect so the
    // shape on the minimap matches what the player sees in the world (square in our
    // case, but parameterised so a future non-square world doesn't squish).
    constexpr int kPadding   = 12;
    constexpr int kMaxExtent = 200;   // px -- ~15% of a 1280 screen, less of a 1920
    constexpr int kBorder    = 2;

    // Fit the world AABB into a kMaxExtent x kMaxExtent box, preserving aspect.
    const float world_w_f = static_cast<float>(world_w);
    const float world_h_f = static_cast<float>(world_h);
    const float aspect    = world_w_f / world_h_f;
    int box_w, box_h;
    if (aspect >= 1.0f) {
        box_w = kMaxExtent;
        box_h = static_cast<int>(kMaxExtent / aspect);
    } else {
        box_h = kMaxExtent;
        box_w = static_cast<int>(kMaxExtent * aspect);
    }
    const int x0 = screen_w - box_w - kPadding;
    const int y0 = screen_h - box_h - kPadding;

    // Scale: world coord -> minimap px.
    const float sx = static_cast<float>(box_w) / world_w_f;
    const float sy = static_cast<float>(box_h) / world_h_f;
    auto toMx = [&](float wx) { return x0 + wx * sx; };
    auto toMy = [&](float wy) { return y0 + wy * sy; };

    // Backdrop: semi-transparent dark panel + subtle border. Border first (acts as
    // outer frame), then inner fill.
    DrawRectangle(x0 - kBorder, y0 - kBorder,
                  box_w + kBorder * 2, box_h + kBorder * 2,
                  Color{20, 24, 32, 220});
    DrawRectangle(x0, y0, box_w, box_h, Color{8, 10, 14, 200});

    const Snapshot& snap = interp.curr();

    // Black holes first (under everything else). Pull radius as a faint ring, event
    // horizon as a filled purple disc.
    for (const auto& b : snap.blackholes) {
        const float mx = toMx(b.pos.x);
        const float my = toMy(b.pos.y);
        const float pr_px = std::max(2.0f, b.pull_radius * sx);
        const float r_px  = std::max(1.5f, b.radius      * sx);
        DrawCircleV(Vector2{mx, my}, pr_px, Color{110, 40, 175,  60});
        DrawCircleV(Vector2{mx, my}, r_px,  Color{230, 60, 200, 220});
    }

    // Cells: dot per cell, radius scales like sqrt(mass) so larger threats read at a
    // glance. Watched player's cells get a white outline so they stand out among bots.
    for (const auto& c : snap.cells) {
        if (c.hiding) continue; // hidden cells are pinned to the BH centre; skip the dup
        const float mx = toMx(c.pos.x);
        const float my = toMy(c.pos.y);
        // Cell radius on the minimap. We use cellRadius's sqrt scaling but clamp so
        // tiny cells stay visible and huge ones don't dominate.
        const float world_r = cellRadius(c.mass);
        const float dot_r   = std::clamp(world_r * sx, 1.5f, 5.0f);
        Color col = colorForPlayer(c.owner);
        // Watched player's cells: brighter accent with a thin white ring so they pop.
        const bool is_watched = (watched_player != INVALID_PLAYER) && (c.owner == watched_player);
        DrawCircleV(Vector2{mx, my}, dot_r, col);
        if (is_watched) {
            DrawCircleLinesV(Vector2{mx, my}, dot_r + 1.0f, Color{255, 255, 255, 230});
        } else if (c.is_elite) {
            // Elites get a faint red ring even when off-screen so the player can track
            // them on the minimap (the in-world halo is also red).
            DrawCircleLinesV(Vector2{mx, my}, dot_r + 1.0f, Color{255, 80, 80, 200});
        }
    }

    // Camera frustum: thin yellow rectangle showing what the player can currently see.
    // Clamp to the minimap rect (the in-world view AABB has a cull margin and may
    // extend slightly past the world edge).
    {
        const float fx0 = std::clamp(toMx(view_min.x), static_cast<float>(x0),
                                     static_cast<float>(x0 + box_w));
        const float fy0 = std::clamp(toMy(view_min.y), static_cast<float>(y0),
                                     static_cast<float>(y0 + box_h));
        const float fx1 = std::clamp(toMx(view_max.x), static_cast<float>(x0),
                                     static_cast<float>(x0 + box_w));
        const float fy1 = std::clamp(toMy(view_max.y), static_cast<float>(y0),
                                     static_cast<float>(y0 + box_h));
        const Rectangle fr{fx0, fy0, std::max(2.0f, fx1 - fx0), std::max(2.0f, fy1 - fy0)};
        DrawRectangleLinesEx(fr, 1.0f, Color{255, 220, 80, 230});
    }
}

} // namespace cr
