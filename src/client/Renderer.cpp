#include "Renderer.h"

#include "ai/BotPersonality.h" // letterForTag

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

namespace cr {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Globally-shared accessibility state. setPaletteMode/setHighContrast write; the
// drawing helpers read. Process-lifetime; not thread-safe (drawing is single-threaded).
PaletteMode s_palette_mode  = PaletteMode::Default;
bool        s_high_contrast = false;

// Per-match player-name registry. Renders to nameplates above cells. Client
// owns the lifecycle: clearPlayerNames at match start; setPlayerName as names
// arrive (own + peer's). The map is tiny (one entry per connected player) so a
// linear scan would also work -- the hashmap just keeps the per-frame cost flat
// when there are many bots + peers.
std::unordered_map<PlayerId, std::string> s_player_names;

// Per-cell wobble pulse map. Bumped to ~1.0 by the event handler on absorbs
// / splits / blasts / recombines; decayed each frame in Client::updateFrame.
// At rest (no recent event) entries are dropped; drawCell reads zero for
// any cell not in the map, which makes the cell render as a perfect circle.
std::unordered_map<EntityId, float> s_cell_wobble_pulse;

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

void setPlayerName(PlayerId p, const char* name) {
    if (name == nullptr || *name == '\0') {
        s_player_names.erase(p);
    } else {
        s_player_names[p] = name;
    }
}
void clearPlayerNames() { s_player_names.clear(); }

void bumpCellWobblePulse(EntityId id, float amount) {
    if (id == INVALID_ENTITY || amount <= 0.0f) return;
    auto& v = s_cell_wobble_pulse[id];
    // Take the max so back-to-back events on the same cell don't reset the
    // pulse to a smaller value -- they hold the peak and continue decaying.
    v = std::max(v, amount);
}

void decayCellWobblePulses(float dt) {
    // Exponential decay; ~3.0/sec gives ~230ms half-life so a single
    // collision wobble is visible for ~0.6-0.8s before fading to nothing.
    constexpr float kDecayPerSec = 3.0f;
    const float factor = std::exp(-kDecayPerSec * dt);
    for (auto it = s_cell_wobble_pulse.begin(); it != s_cell_wobble_pulse.end(); ) {
        it->second *= factor;
        if (it->second < 0.02f) {
            it = s_cell_wobble_pulse.erase(it);
        } else {
            ++it;
        }
    }
}

void clearCellWobblePulses() { s_cell_wobble_pulse.clear(); }

float cellWobblePulse(EntityId id) {
    auto it = s_cell_wobble_pulse.find(id);
    return (it == s_cell_wobble_pulse.end()) ? 0.0f : it->second;
}

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

// Three-tier world grid clamped to the visible AABB. Tiers (from faintest to
// strongest):
//   sub   -- 100px subdivisions, almost-invisible "graph paper" texture
//   main  -- 400px lines (the original grid), slightly polished color
//   major -- 1600px accent dots at intersections so the player has a coarse
//            distance reference at high zoom-out
// Plus a polished world boundary: outer thick stroke + inner thin glow line.
// All passes cull lines outside the visible AABB so a 16k x 16k world stays
// cheap to draw.
void drawWorldGrid(int world_w, int world_h, Vec2 view_min, Vec2 view_max) {
    const int   sub_step   = 100;
    const int   main_step  = 400;
    const int   major_step = 1600;
    const Color sub_line   = {32, 38, 48,  90};   // very subtle subdivisions
    const Color main_line  = {52, 58, 72, 180};   // primary grid (slightly cooler/brighter than the old 40,44,52 so it reads against the gradient bg)
    const Color dot_accent = {120, 145, 175, 200}; // major intersection dot

    const float xmin = std::max(view_min.x, 0.0f);
    const float xmax = std::min(view_max.x, static_cast<float>(world_w));
    const float ymin = std::max(view_min.y, 0.0f);
    const float ymax = std::min(view_max.y, static_cast<float>(world_h));

    if (xmin < xmax && ymin < ymax) {
        // ---- Sub grid (100px). ----
        {
            int xs = static_cast<int>(std::floor(xmin / sub_step)) * sub_step;
            int xe = static_cast<int>(std::ceil(xmax  / sub_step)) * sub_step;
            int ys = static_cast<int>(std::floor(ymin / sub_step)) * sub_step;
            int ye = static_cast<int>(std::ceil(ymax  / sub_step)) * sub_step;
            for (int x = xs; x <= xe; x += sub_step) {
                if (x < 0 || x > world_w) continue;
                if (x % main_step == 0) continue; // main grid will overdraw
                DrawLine(x, static_cast<int>(ymin),
                         x, static_cast<int>(ymax), sub_line);
            }
            for (int y = ys; y <= ye; y += sub_step) {
                if (y < 0 || y > world_h) continue;
                if (y % main_step == 0) continue;
                DrawLine(static_cast<int>(xmin), y,
                         static_cast<int>(xmax), y, sub_line);
            }
        }

        // ---- Main grid (400px). ----
        {
            int xs = static_cast<int>(std::floor(xmin / main_step)) * main_step;
            int xe = static_cast<int>(std::ceil(xmax  / main_step)) * main_step;
            int ys = static_cast<int>(std::floor(ymin / main_step)) * main_step;
            int ye = static_cast<int>(std::ceil(ymax  / main_step)) * main_step;
            for (int x = xs; x <= xe; x += main_step) {
                if (x < 0 || x > world_w) continue;
                DrawLine(x, static_cast<int>(ymin),
                         x, static_cast<int>(ymax), main_line);
            }
            for (int y = ys; y <= ye; y += main_step) {
                if (y < 0 || y > world_h) continue;
                DrawLine(static_cast<int>(xmin), y,
                         static_cast<int>(xmax), y, main_line);
            }
        }

        // ---- Major intersection dots (every 1600px). ----
        // Tiny accent circles where four main grid lines meet. Gives the
        // grid a sense of "graph paper" rhythm without changing the line
        // structure. Two-pass (glow + core) so they have a hint of depth.
        {
            int xs = static_cast<int>(std::floor(xmin / major_step)) * major_step;
            int xe = static_cast<int>(std::ceil(xmax  / major_step)) * major_step;
            int ys = static_cast<int>(std::floor(ymin / major_step)) * major_step;
            int ye = static_cast<int>(std::ceil(ymax  / major_step)) * major_step;
            for (int x = xs; x <= xe; x += major_step) {
                if (x < 0 || x > world_w) continue;
                for (int y = ys; y <= ye; y += major_step) {
                    if (y < 0 || y > world_h) continue;
                    DrawCircle(x, y, 5.0f,
                               Color{dot_accent.r, dot_accent.g, dot_accent.b, 50});
                    DrawCircle(x, y, 2.0f, dot_accent);
                }
            }
        }
    }

    // ---- World boundary: triple-stroke for depth ----
    // Outer thick warm-grey stroke marks the hard edge.
    // Inner thinner cool stroke + a faint inner glow gives it presence.
    DrawRectangleLinesEx(Rectangle{0.0f, 0.0f,
                                   static_cast<float>(world_w),
                                   static_cast<float>(world_h)},
                         4.0f, Color{96, 108, 128, 255});
    // Slight inset thin highlight ring inside the world for a "framed" feel.
    DrawRectangleLinesEx(Rectangle{6.0f, 6.0f,
                                   static_cast<float>(world_w - 12),
                                   static_cast<float>(world_h - 12)},
                         1.5f, Color{140, 160, 200, 70});
    // Far-inset faint glow so the boundary breathes inward instead of
    // being a hard razor edge against the play area.
    DrawRectangleLinesEx(Rectangle{14.0f, 14.0f,
                                   static_cast<float>(world_w - 28),
                                   static_cast<float>(world_h - 28)},
                         1.0f, Color{140, 160, 200, 30});
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

// Procedural wormhole shader. Same overall approach as the black-hole shader
// (full-screen quad + radial UV + rotating spiral) but tuned for a *bright*
// blue portal feel rather than a destructive sink:
//   * The "void" is a glowing cyan-white aperture instead of a black core
//     -- this thing is a doorway, not a death.
//   * The accretion disc has 4-armed spirals (vs 2-armed on BH) that wind
//     INWARD into the aperture, then re-emerge faintly, suggesting energy
//     transiting through the portal.
//   * Outer halo is a slow indigo pulse so the pair on the minimap reads
//     as "linked rifts" instead of "scary holes".
//
// `u_spin` is the snapshot-supplied spin_phase (radians), so two endpoints
// of a pair counter-rotate by passing the same value with sign flipped.
const char* const kWormholeFS = R"GLSL(
#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform float u_time;
uniform float u_horizon;   // 0..1, "aperture" radius (visible bright core)
uniform float u_spin;      // radians, host-supplied so all clients agree

void main() {
    vec2  uv = (fragTexCoord - 0.5) * 2.0;
    float r  = length(uv);
    if (r > 1.0) discard;

    float theta = atan(uv.y, uv.x);

    // Frame-dragging swirl. The 5.5 factor + pow(1-r, 1.3) winds streamlines
    // tighter near the aperture so the spiral feels like it's being pulled
    // *into* a doorway rather than just rotating uniformly.
    float swirl = 5.5 * pow(1.0 - r, 1.3);
    float spin  = theta + u_spin + u_time * 0.30 + swirl;

    // Four-armed spiral (vs the black hole's two arms) -- gives the wormhole
    // a denser, more "energized" feel. Higher arm-power sharpens the bands.
    float arms = 0.5 + 0.5 * sin(spin * 4.0 + r * 6.0);
    arms = pow(arms, 2.2);

    // Secondary counter-spinning faint arm set, so the portal feels like
    // overlapping currents instead of a single rotation.
    float arms2 = 0.5 + 0.5 * sin(-spin * 2.0 + r * 3.5 + u_time * 0.6);
    arms2 = pow(arms2, 4.0) * 0.55;

    float arms_total = clamp(arms + arms2, 0.0, 1.0);

    // Disc brightness: peaks JUST OUTSIDE the aperture, falls off both
    // inward (where the bright core takes over) and outward (into the halo).
    float aperture_edge = smoothstep(u_horizon * 0.95, u_horizon * 1.10, r);
    float outer_falloff = 1.0 - smoothstep(u_horizon * 1.3, 0.95, r);
    float disc          = aperture_edge * outer_falloff;

    // Palette: deep indigo void at the outer rim -> dark purple-blue mid
    // disc -> dim magenta-violet spiral arms -> a DARK glowing aperture
    // (slightly brighter than the void but never white). Reads as a
    // brooding cosmic rift rather than a friendly portal.
    vec3 col_void  = vec3(0.04, 0.02, 0.12); // outer dark indigo
    vec3 col_disc  = vec3(0.18, 0.08, 0.36); // mid-spiral deep purple-blue
    vec3 col_arms  = vec3(0.55, 0.18, 0.55); // spiral arms: muted magenta/red-violet
    vec3 col_core  = vec3(0.12, 0.05, 0.22); // aperture: dark purple-blue (slightly
                                             // brighter than the void but still dark)

    float depth = smoothstep(1.0, u_horizon * 1.1, r); // 0 at edge, 1 near core
    vec3  base  = mix(col_void, col_disc, depth);
    vec3  color = mix(base, col_arms, disc * arms_total);

    // Aperture core: the wormhole settles into a DIM glowing purple-blue
    // inside the inner radius -- not white, not black. Subtle pulse so the
    // core breathes without ever lighting up the surroundings.
    float core_mask = 1.0 - smoothstep(u_horizon * 0.55, u_horizon * 1.02, r);
    float pulse = 0.90 + 0.10 * sin(u_time * 2.2);
    color = mix(color, col_core * pulse, core_mask);

    // Rim glow at the aperture boundary -- a soft warm-purple ring (not
    // cyan-white) so it stays inside the moody palette. Squared signed
    // distance instead of pow(x, 2.0), which is undefined for negative x
    // in GLSL.
    float rim_d = (r - u_horizon) * 16.0;
    float rim   = exp(-rim_d * rim_d);
    color += vec3(0.45, 0.18, 0.55) * rim * 0.55;

    // Outer alpha: soft fade so the disc blends into the world background.
    // Slightly more generous than the black hole's 0.55 cutoff so the
    // wormhole's halo extends a touch further -- helps the pair feel
    // "linked" peripherally.
    float alpha = smoothstep(1.0, 0.50, r);

    finalColor = vec4(color, alpha);
}
)GLSL";

// Procedural "cell blob" shader. Replaces the flat DrawCircleV body fill with
// a watery deformable disc: soft alpha at the rim, low-frequency wobble around
// the edge so the silhouette breathes, and a stretch factor along the velocity
// direction so a moving cell elongates like a water droplet. Plus an
// upper-left highlight for a 3D droplet feel.
//
// Per-cell uniforms (updated between draws):
//   u_color    -- base RGB (0..1).
//   u_vel_dir  -- unit velocity direction; (0,0) when stationary.
//   u_stretch  -- 1.0 = circular, >1 = elongated along u_vel_dir.
//   u_id_seed  -- per-cell radians offset so neighbouring cells don't wobble
//                 in lockstep.
//   u_alpha    -- overall alpha multiplier (fade effects: BH transit, stealth).
//   u_padding  -- the quad's half-size in units of cell radius. Caller draws
//                 a 2*r*padding-sized quad so there's room for stretch +
//                 wobble; the shader uses this to map fragTexCoord -> uv.
//
// u_time is shared per-frame.
const char* const kCellBlobFS = R"GLSL(
#version 330

in  vec2 fragTexCoord;
out vec4 finalColor;

uniform float u_time;
uniform vec3  u_color;
uniform vec2  u_vel_dir;
uniform float u_stretch;
uniform float u_id_seed;
uniform float u_alpha;
uniform float u_padding;
// Wobble intensity. 0.0 = perfect circle (cell at rest, no collisions);
// 1.0 = baseline wobble; up to ~1.5 right after a collision pulse.
// Driven from C++ -- composed of (a) a velocity-derived baseline and
// (b) transient pulses pushed in by collision / split / absorb events.
uniform float u_wobble_amp;

void main() {
    // Centered uv in [-padding, +padding] across the quad. The cell's
    // unstretched edge sits at radius 1.0 in this space, so `padding` must
    // be > the max stretch + max wobble amplitude.
    vec2 uv = (fragTexCoord - 0.5) * 2.0 * u_padding;

    // Rotate uv into the velocity frame so x-axis aligns with motion.
    // If the cell is stationary, identity.
    vec2 local = uv;
    if (dot(u_vel_dir, u_vel_dir) > 0.0001) {
        float cx = u_vel_dir.x;
        float sy = u_vel_dir.y;
        local = vec2( uv.x * cx + uv.y * sy,
                     -uv.x * sy + uv.y * cx);
    }
    // Stretch along the motion axis: dividing local.x by stretch makes the
    // edge (at local-x = stretch) line up with radius 1.0 -- so a stretched
    // cell visually elongates in the velocity direction.
    local.x /= u_stretch;

    float r     = length(local);
    float theta = atan(local.y, local.x);

    // Three-harmonic angular wobble. ALL amplitudes are multiplied by
    // u_wobble_amp, so when the cell is at rest (no velocity, no recent
    // collision pulse) the wobble is zero and the cell renders as a
    // perfect circle. Up to ~15% peak deformation when u_wobble_amp = 1.
    // Per-cell phase offset (u_id_seed) keeps neighbouring cells
    // out-of-sync when they ARE wobbling. No "breathing" oscillation --
    // cells should be still when nothing is happening to them.
    float wobble  = 0.080 * sin(theta *  4.0 + u_time * 2.5 + u_id_seed);
    wobble       += 0.050 * sin(theta *  7.0 - u_time * 3.5 + u_id_seed * 1.3);
    wobble       += 0.025 * sin(theta * 10.0 + u_time * 5.0 + u_id_seed * 0.5);
    wobble       *= u_wobble_amp;

    float edge = 1.0 + wobble;

    // Soft alpha falloff at the wobbly rim. The body holds full alpha
    // until 96% of the edge, then smoothly fades to 0 just past it --
    // narrow fade band keeps the colour solid almost all the way to
    // the silhouette so the blob reads at the same brightness as the
    // old flat circle.
    float fade_start = edge * 0.96;
    float fade_end   = edge * 1.02;
    float body_a     = 1.0 - smoothstep(fade_start, fade_end, r);
    if (body_a < 0.004) discard;

    // Flat colour. No inner shading, no rim darkening, no highlight --
    // the cell just gets the same fill it had before, in a slightly
    // wobbly shape.
    finalColor = vec4(u_color, body_a * u_alpha);
}
)GLSL";

struct CellBlobGfx {
    Shader     shader{};
    Texture2D  white{};
    int        loc_time       = -1;
    int        loc_color      = -1;
    int        loc_vel_dir    = -1;
    int        loc_stretch    = -1;
    int        loc_id_seed    = -1;
    int        loc_alpha      = -1;
    int        loc_padding    = -1;
    int        loc_wobble_amp = -1;
    bool       initialized = false;
    bool       failed      = false;
};

struct WormholeGfx {
    Shader     shader{};
    Texture2D  white{};
    int        loc_time    = -1;
    int        loc_horizon = -1;
    int        loc_spin    = -1;
    bool       initialized = false;
    bool       failed      = false;
};

// Procedural fire-ball shader for the crashing-comet head. The previous version
// composited three discrete intensity bands (core / mid / outer) and ended up looking
// like layered images. This rewrite uses a single continuous blackbody-style gradient
// where temperature decreases smoothly with distance from the centre; FBM noise warps
// the radial distance so the silhouette flickers, but the *colour curve* stays
// continuous. No mix() between palette stops -- the channels are independent smooth
// power functions, which is how real hot-body radiation actually behaves.
const char* const kCometHeadFS = R"GLSL(
#version 330

in  vec2 fragTexCoord;
out vec4 finalColor;

uniform float u_time;
uniform float u_telegraph; // 0..1; <1 dims the comet during the telegraph window

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i),               hash(i + vec2(1.0, 0.0)), f.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 6; ++i) {
        s += a * vnoise(p);
        p *= 2.03;
        a *= 0.5;
    }
    return s;
}

// Blackbody-radiator-ish curve. `t` is temperature inverted: 0 = white-hot,
// 1 = nearly cool. Channels fall off at different rates: blue earliest (only the
// hottest part is bluish-white), green next, red latest. Subtle red dim at very high
// `t` makes the deep tail go to dark cherry instead of glowing pink.
vec3 firePalette(float t) {
    t = clamp(t, 0.0, 1.0);
    float R = 1.0 - 0.30 * pow(t, 3.0);
    float G = pow(1.0 - t, 1.5) - 0.05 * t;
    float B = pow(1.0 - t, 5.0);
    return vec3(max(0.0, R), max(0.0, G), max(0.0, B));
}

void main() {
    // uv ranges -1..1 across the quad; r is distance from centre.
    vec2  uv = fragTexCoord * 2.0 - 1.0;
    float r  = length(uv);
    if (r > 1.05) discard;

    // Two FBM layers moving in opposite directions to make the surface boil rather
    // than scroll. Slightly different frequencies keep the result aperiodic.
    vec2  p1 = uv * 2.3 + vec2( u_time * 0.65, -u_time * 0.45);
    vec2  p2 = uv * 5.8 + vec2(-u_time * 1.15,  u_time * 0.85);
    float n  = fbm(p1) * 0.65 + fbm(p2) * 0.35;

    // Warp the radial distance with the noise: flame tongues push out, voids pull in.
    // The 0.40 amplitude is enough to make the silhouette visibly flicker without
    // turning it into a blob.
    float warp = (n - 0.5) * 0.40;
    float t    = clamp(r - warp, 0.0, 1.0); // temperature: 0 hottest, 1 coldest

    vec3  rgb = firePalette(t);

    // Add a faint core boost so the centre punches harder than a pure inverse-r curve.
    rgb *= 1.0 + 0.45 * exp(-r * 4.0);

    // Smooth alpha falloff. pow keeps the core fully opaque while the edge dissolves.
    float alpha = pow(1.0 - clamp(r * 0.95 - warp * 0.6, 0.0, 1.0), 1.4);
    alpha = clamp(alpha * 1.25, 0.0, 1.0);
    alpha *= u_telegraph;

    finalColor = vec4(rgb, alpha);
}
)GLSL";

// Trail shader: paints a long, smooth fire streak behind the comet. The quad is drawn
// once per frame with one end anchored at the comet's position and the other end
// extending backwards along -vel for `kCometTrailLengthMul * comet.radius` world units.
// fragTexCoord.x = 0 at the tail, 1 at the head; fragTexCoord.y = 0..1 across the
// trail's width. Animated noise scrolls toward the tail so the trail "flows".
const char* const kCometTrailFS = R"GLSL(
#version 330

in  vec2 fragTexCoord;
out vec4 finalColor;

uniform float u_time;
uniform float u_telegraph;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i),               hash(i + vec2(1.0, 0.0)), f.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) {
        s += a * vnoise(p);
        p *= 2.03;
        a *= 0.5;
    }
    return s;
}

vec3 firePalette(float t) {
    t = clamp(t, 0.0, 1.0);
    float R = 1.0 - 0.30 * pow(t, 3.0);
    float G = pow(1.0 - t, 1.5) - 0.05 * t;
    float B = pow(1.0 - t, 5.0);
    return vec3(max(0.0, R), max(0.0, G), max(0.0, B));
}

void main() {
    float u = fragTexCoord.x; // 0 = tail, 1 = head
    float v = fragTexCoord.y; // 0..1 across width

    // Cone shape: trail is narrow at the tail and wide at the head. Quadratic so the
    // taper is gentle near the head and pinches off cleanly at the tail.
    float half_w = mix(0.04, 0.48, u * u);
    float off    = abs(v - 0.5);
    if (off > half_w) discard;
    // Width-direction intensity: 1 at centerline, 0 at the cone edge.
    float across = 1.0 - off / half_w;

    // Length-direction intensity: bright at the head, dim at the tail. The pow shapes
    // the falloff so most of the brightness is concentrated near the head.
    float along  = pow(u, 1.6);

    // Animated noise that scrolls along the trail length. Higher frequency on the
    // width axis so the flame has visible vertical detail.
    vec2  np  = vec2(u * 5.5 - u_time * 1.8, (v - 0.5) * 8.0);
    float n   = fbm(np);
    // Second slower layer adds large rolling waves.
    vec2  np2 = vec2(u * 2.2 - u_time * 0.8, (v - 0.5) * 3.5);
    n = 0.65 * n + 0.35 * fbm(np2);

    // Combine: width falloff * length falloff * noise modulation.
    float intensity = across * along * (0.55 + 0.85 * n);
    intensity = clamp(intensity, 0.0, 1.0);

    // Temperature ramp: hotter (more opaque, whiter) at high intensity.
    float temp = 1.0 - intensity;
    vec3  rgb  = firePalette(temp);

    float alpha = pow(intensity, 1.1) * u_telegraph;
    alpha = clamp(alpha, 0.0, 1.0);

    finalColor = vec4(rgb, alpha);
}
)GLSL";

struct CometGfx {
    Shader    head_shader{};
    Shader    trail_shader{};
    Texture2D white{};
    int       loc_head_time      = -1;
    int       loc_head_telegraph = -1;
    int       loc_trail_time      = -1;
    int       loc_trail_telegraph = -1;
    bool      initialized        = false;
    bool      failed             = false;
};

BlackHoleGfx g_bh_gfx;
CometGfx     g_comet_gfx;
WormholeGfx  g_wh_gfx;
CellBlobGfx  g_blob_gfx;

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

void ensureWormholeGfx() {
    if (g_wh_gfx.initialized || g_wh_gfx.failed) return;

    g_wh_gfx.shader = LoadShaderFromMemory(nullptr, kWormholeFS);
    if (g_wh_gfx.shader.id == 0) {
        g_wh_gfx.failed = true;
        return;
    }
    g_wh_gfx.loc_time    = GetShaderLocation(g_wh_gfx.shader, "u_time");
    g_wh_gfx.loc_horizon = GetShaderLocation(g_wh_gfx.shader, "u_horizon");
    g_wh_gfx.loc_spin    = GetShaderLocation(g_wh_gfx.shader, "u_spin");

    Image img = GenImageColor(1, 1, WHITE);
    g_wh_gfx.white = LoadTextureFromImage(img);
    UnloadImage(img);

    g_wh_gfx.initialized = true;
}

void unloadWormholeGfx() {
    if (g_wh_gfx.initialized) {
        UnloadTexture(g_wh_gfx.white);
        UnloadShader(g_wh_gfx.shader);
        g_wh_gfx = WormholeGfx{};
    } else if (g_wh_gfx.failed && g_wh_gfx.shader.id != 0) {
        UnloadShader(g_wh_gfx.shader);
        g_wh_gfx = WormholeGfx{};
    }
}

void ensureCellBlobGfx() {
    if (g_blob_gfx.initialized || g_blob_gfx.failed) return;

    g_blob_gfx.shader = LoadShaderFromMemory(nullptr, kCellBlobFS);
    if (g_blob_gfx.shader.id == 0) {
        g_blob_gfx.failed = true;
        return;
    }
    g_blob_gfx.loc_time       = GetShaderLocation(g_blob_gfx.shader, "u_time");
    g_blob_gfx.loc_color      = GetShaderLocation(g_blob_gfx.shader, "u_color");
    g_blob_gfx.loc_vel_dir    = GetShaderLocation(g_blob_gfx.shader, "u_vel_dir");
    g_blob_gfx.loc_stretch    = GetShaderLocation(g_blob_gfx.shader, "u_stretch");
    g_blob_gfx.loc_id_seed    = GetShaderLocation(g_blob_gfx.shader, "u_id_seed");
    g_blob_gfx.loc_alpha      = GetShaderLocation(g_blob_gfx.shader, "u_alpha");
    g_blob_gfx.loc_padding    = GetShaderLocation(g_blob_gfx.shader, "u_padding");
    g_blob_gfx.loc_wobble_amp = GetShaderLocation(g_blob_gfx.shader, "u_wobble_amp");

    Image img = GenImageColor(1, 1, WHITE);
    g_blob_gfx.white = LoadTextureFromImage(img);
    UnloadImage(img);

    g_blob_gfx.initialized = true;
}

void unloadCellBlobGfx() {
    if (g_blob_gfx.initialized) {
        UnloadTexture(g_blob_gfx.white);
        UnloadShader(g_blob_gfx.shader);
        g_blob_gfx = CellBlobGfx{};
    } else if (g_blob_gfx.failed && g_blob_gfx.shader.id != 0) {
        UnloadShader(g_blob_gfx.shader);
        g_blob_gfx = CellBlobGfx{};
    }
}

void ensureCometGfx() {
    if (g_comet_gfx.initialized || g_comet_gfx.failed) return;
    g_comet_gfx.head_shader  = LoadShaderFromMemory(nullptr, kCometHeadFS);
    g_comet_gfx.trail_shader = LoadShaderFromMemory(nullptr, kCometTrailFS);
    if (g_comet_gfx.head_shader.id == 0 || g_comet_gfx.trail_shader.id == 0) {
        g_comet_gfx.failed = true;
        if (g_comet_gfx.head_shader.id  != 0) UnloadShader(g_comet_gfx.head_shader);
        if (g_comet_gfx.trail_shader.id != 0) UnloadShader(g_comet_gfx.trail_shader);
        return;
    }
    g_comet_gfx.loc_head_time       = GetShaderLocation(g_comet_gfx.head_shader,  "u_time");
    g_comet_gfx.loc_head_telegraph  = GetShaderLocation(g_comet_gfx.head_shader,  "u_telegraph");
    g_comet_gfx.loc_trail_time      = GetShaderLocation(g_comet_gfx.trail_shader, "u_time");
    g_comet_gfx.loc_trail_telegraph = GetShaderLocation(g_comet_gfx.trail_shader, "u_telegraph");

    Image img = GenImageColor(1, 1, WHITE);
    g_comet_gfx.white = LoadTextureFromImage(img);
    UnloadImage(img);

    g_comet_gfx.initialized = true;
}

void unloadCometGfx() {
    if (g_comet_gfx.initialized) {
        UnloadTexture(g_comet_gfx.white);
        UnloadShader(g_comet_gfx.head_shader);
        UnloadShader(g_comet_gfx.trail_shader);
        g_comet_gfx = CometGfx{};
    } else if (g_comet_gfx.failed) {
        // Defensive: if compile failed but ids snuck through anyway, free them.
        if (g_comet_gfx.head_shader.id  != 0) UnloadShader(g_comet_gfx.head_shader);
        if (g_comet_gfx.trail_shader.id != 0) UnloadShader(g_comet_gfx.trail_shader);
        g_comet_gfx = CometGfx{};
    }
}

// Trail length is `kCometTrailLengthMul * radius` world units. With radius=440 and
// mul=8 that's a 3520 px trail -- substantial against the 16000 px world.
constexpr float kCometTrailLengthMul = 8.0f;
// Trail width at the head end is `kCometTrailWidthMul * radius`. The shader cone
// tapers from this down to a thin point at the tail.
constexpr float kCometTrailWidthMul  = 1.05f;

// Render a single comet -- telegraph line + fire trail + fire head. Called from
// drawWorld inside BeginMode2D so coordinates are in world space. Order matters:
// trail draws FIRST so the head occludes its hot end cleanly; the trail's shader is
// already alpha-shaped to fade in toward the head so seams are invisible.
void drawComet(const CometSnap& cm, double now_sec) {
    ensureCometGfx();
    const float t = static_cast<float>(now_sec);

    // ---- Telegraph line (warning window only) ----
    // Drawn under everything so the comet head clearly lands on top once it crosses.
    if (cm.telegraph_norm < 1.0f) {
        float pulse = 0.5f + 0.5f * std::sin(t * 9.0f);
        float vis   = std::clamp(cm.telegraph_norm * 1.3f, 0.0f, 1.0f);
        unsigned char a_glow = static_cast<unsigned char>(60  + pulse * 90 * vis);
        unsigned char a_line = static_cast<unsigned char>(140 + pulse * 90 * vis);
        Vector2 a{cm.telegraph_start.x, cm.telegraph_start.y};
        Vector2 b{cm.telegraph_end.x,   cm.telegraph_end.y};
        DrawLineEx(a, b, 18.0f, Color{255, 120, 40, a_glow});
        DrawLineEx(a, b,  4.0f, Color{255, 220, 130, a_line});
        float ring_r = cm.radius * (0.6f + 0.25f * pulse);
        DrawCircleLinesV(a, ring_r, Color{255, 180, 80,
                                          static_cast<unsigned char>(180 * vis)});
    }

    // Pick a direction for the trail. While active, use the comet's actual velocity
    // (so the trail trails behind motion). During telegraph, fall back to the
    // telegraph direction so a faint pre-trail hints at the strike line.
    Vec2  vel       = cm.vel;
    float vel_len   = std::sqrt(vel.x * vel.x + vel.y * vel.y);
    Vec2  unit_vel{1.0f, 0.0f};
    if (vel_len > 1e-3f) {
        unit_vel = Vec2{vel.x / vel_len, vel.y / vel_len};
    } else {
        Vec2 tdir{cm.telegraph_end.x - cm.telegraph_start.x,
                  cm.telegraph_end.y - cm.telegraph_start.y};
        float td = std::sqrt(tdir.x * tdir.x + tdir.y * tdir.y);
        if (td > 1e-3f) unit_vel = Vec2{tdir.x / td, tdir.y / td};
    }

    // ---- Trail ----
    // Build a rotated rectangle whose RIGHT-CENTER (the head-end of the cone) sits at
    // the comet's position and whose body extends in the -vel direction. raylib's
    // DrawTexturePro rotation is in degrees, clockwise (its Y axis points down).
    // atan2(-vel.y, -vel.x) gives the angle whose direction is -vel; converting to
    // degrees aligns the rect's local +X axis with -vel direction... but our chosen
    // origin makes the rect extend LEFT of the pivot (i.e. in the rect's local -X
    // direction). So we want -X to align with -vel, i.e. +X to align with +vel. The
    // angle of +vel is atan2(vel.y, vel.x). Convert to degrees for raylib.
    if (cm.telegraph_norm > 0.05f && g_comet_gfx.initialized) {
        const float trail_len   = cm.radius * kCometTrailLengthMul;
        const float trail_w     = cm.radius * kCometTrailWidthMul * 2.0f; // full width
        const float angle_rad   = std::atan2(unit_vel.y, unit_vel.x);
        const float angle_deg   = angle_rad * (180.0f / kPi);

        // Telegraph: trail is faint and short. We pre-multiply the shader's u_telegraph
        // by an extra ramp so the trail blooms into existence as the strike approaches.
        float tg = std::min(1.0f, cm.telegraph_norm * cm.telegraph_norm * 1.4f);
        if (cm.telegraph_norm >= 1.0f) tg = 1.0f;

        // The rect is drawn with its top-LEFT in world space (pre-rotation), so we set
        // origin to (trail_len, trail_w/2) -- the right-center of the rect -- and place
        // the rect at the comet's position. Rotation around that origin pivots the rect
        // such that the head stays anchored at the comet and the body sweeps to align
        // with -vel direction.
        const Rectangle dst{cm.pos.x, cm.pos.y, trail_len, trail_w};
        const Vector2   origin{trail_len, trail_w * 0.5f};

        BeginShaderMode(g_comet_gfx.trail_shader);
        SetShaderValue(g_comet_gfx.trail_shader, g_comet_gfx.loc_trail_time,
                       &t, SHADER_UNIFORM_FLOAT);
        SetShaderValue(g_comet_gfx.trail_shader, g_comet_gfx.loc_trail_telegraph,
                       &tg, SHADER_UNIFORM_FLOAT);
        // BLEND_ADDITIVE gives the trail a glowy bloom that reads as fire rather than
        // a flat painted streak. Restore the default blend mode after.
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTexturePro(g_comet_gfx.white,
                       Rectangle{0, 0, 1, 1},
                       dst,
                       origin,
                       angle_deg,
                       WHITE);
        EndBlendMode();
        EndShaderMode();
    }

    // ---- Head ----
    const Vector2 c{cm.pos.x, cm.pos.y};
    const float   draw_r = cm.radius * 1.35f;
    if (g_comet_gfx.initialized) {
        const Rectangle dst{c.x - draw_r, c.y - draw_r, draw_r * 2.0f, draw_r * 2.0f};
        float intensity = (cm.telegraph_norm < 1.0f)
                            ? (cm.telegraph_norm * 0.40f)
                            : 1.0f;
        BeginShaderMode(g_comet_gfx.head_shader);
        SetShaderValue(g_comet_gfx.head_shader, g_comet_gfx.loc_head_time,
                       &t, SHADER_UNIFORM_FLOAT);
        SetShaderValue(g_comet_gfx.head_shader, g_comet_gfx.loc_head_telegraph,
                       &intensity, SHADER_UNIFORM_FLOAT);
        DrawTexturePro(g_comet_gfx.white,
                       Rectangle{0, 0, 1, 1},
                       dst,
                       Vector2{0, 0},
                       0.0f,
                       WHITE);
        EndShaderMode();
    } else {
        // Fallback: bright orange core + red halo (no boiling fire).
        if (cm.telegraph_norm >= 1.0f) {
            DrawCircleV(c, cm.radius * 0.85f, Color{255, 230, 120, 230});
            DrawCircleV(c, cm.radius * 0.55f, Color{255, 250, 200, 255});
            DrawCircleLinesV(c, cm.radius,    Color{255, 140,  40, 220});
        }
    }

    // ---- Additive shock ring (active only) ----
    if (cm.telegraph_norm >= 1.0f) {
        float pulse = 0.5f + 0.5f * std::sin(t * 3.5f);
        DrawCircleLinesV(c, cm.radius * (1.05f + 0.04f * pulse),
                         Color{255, 200, 80,
                               static_cast<unsigned char>(110 + 60.0f * pulse)});
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

// ---- Tidal current band ----
// A horizontal river stretching the full world width. The visible region the
// renderer covers is just the on-screen slice -- we never paint thousands of
// off-screen pixels. Visual design (no arrows, no hard centre-line):
//   * A feathered cyan-tinted fill with smoothstep alpha at the top + bottom
//     edges so the band fades into the world.
//   * Three parallax lanes of *streaks* (short tapered line segments) drifting
//     along `dir`. Far lanes are dim + slow, near lanes are bright + fast --
//     gives water-like depth without a literal "this way" indicator.
//   * A few longer, fainter "current threads" -- horizontal hairline ribbons
//     scrolling along the band that hint at flow without competing with the
//     cells.
//
// `view_min` / `view_max` clip the visible slice so work scales with what's
// on-screen, not the 16k-wide world.
void drawTidalCurrent(const CurrentSnap& c, double now_sec,
                      int world_w, Vec2 view_min, Vec2 view_max) {
    const float t = static_cast<float>(now_sec);
    const float h = c.half_height;
    if (h <= 0.0f) return;

    const float band_x0 = 0.0f;
    const float band_x1 = static_cast<float>(world_w);
    const float band_y0 = c.pos.y - h;
    const float band_y1 = c.pos.y + h;
    const float band_yc = c.pos.y;

    const float vx0 = std::max(view_min.x, band_x0);
    const float vx1 = std::min(view_max.x, band_x1);
    if (vx0 >= vx1) return;
    if (band_y1 < view_min.y || band_y0 > view_max.y) return;

    // --- Soft fill with feathered top/bottom edges ---
    // Sub-strip composite approximates a smoothstep alpha gradient (full at
    // the centre, 0 at the rim). More strips = smoother fade; 7 is enough
    // for the eye to read it as continuous.
    constexpr int kStripsPerSide = 7;
    const float strip_h = h / static_cast<float>(kStripsPerSide);
    for (int i = -kStripsPerSide; i < kStripsPerSide; ++i) {
        const float sy0 = band_yc + i * strip_h;
        const float sy1 = sy0 + strip_h;
        const float yc_off = (sy0 + sy1) * 0.5f - band_yc;
        const float ssy0 = std::max(sy0, view_min.y);
        const float ssy1 = std::min(sy1, view_max.y);
        if (ssy0 >= ssy1) continue;
        float u = 1.0f - (std::abs(yc_off) / h);
        u = std::clamp(u, 0.0f, 1.0f);
        // Steeper-than-smoothstep falloff so the band fades quickly at the
        // rim but stays solid in the middle -- gives a clearer "this is the
        // water" silhouette.
        const float s = u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f); // quintic smoothstep
        // Brighter peak alpha + slight floor so the dim edges still register.
        const unsigned char a = static_cast<unsigned char>(6.0f + s * 32.0f);
        DrawRectangle(static_cast<int>(vx0),
                      static_cast<int>(ssy0),
                      static_cast<int>(vx1 - vx0),
                      static_cast<int>(ssy1 - ssy0),
                      Color{55, 130, 195, a});
    }

    // --- Long flowing "current threads" ---
    // A handful of nearly-invisible horizontal hairlines that scroll across
    // the band at the flow speed. They give a subtle sense of motion across
    // the whole width without the visual weight of a centre line. Each
    // thread is at a unique y-offset and has its own phase. Drawn under
    // the streak particles so the streaks pop on top.
    constexpr int kThreads = 6;
    const float thread_speed = std::max(60.0f, c.strength * 1.4f);
    for (int i = 0; i < kThreads; ++i) {
        // Distribute threads across the band height, biased toward centre.
        // Map i in [0, kThreads) to a u in [0, 1] then bias.
        const float u  = (static_cast<float>(i) + 0.5f) / static_cast<float>(kThreads);
        // Bias toward centre: 0.5 + (u - 0.5)^3 * sign. Keeps edges sparse.
        const float bu = 0.5f + (u - 0.5f) * (u - 0.5f) * (u - 0.5f) * 3.5f;
        const float ty = band_y0 + std::clamp(bu, 0.05f, 0.95f) * (band_y1 - band_y0);
        // Per-thread alpha falloff toward the edges.
        const float edge_t = 1.0f - std::abs(bu - 0.5f) * 2.0f;
        const float edge_a = std::clamp(edge_t * 1.4f, 0.0f, 1.0f);
        // Per-thread phase so they're not synchronized.
        uint32_t   h1 = static_cast<uint32_t>(c.id) * 0x9E3779B9u
                      + static_cast<uint32_t>(i) * 0x85EBCA6Bu;
        const float phase_off = ((h1 >> 8) & 0xFFFFu) / 65535.0f * 1.0f;
        // Tiny vertical wobble so threads breathe.
        const float wobble = std::sin(t * 0.6f + (h1 & 0xFFFu) * 0.001f) * 4.0f;
        const float y      = ty + wobble;

        // Render the thread as a series of short dashes scrolling along x.
        const float dash_len   = 38.0f;
        const float gap_len    = 110.0f;
        const float cycle      = dash_len + gap_len;
        const float scroll     = t * thread_speed * c.dir.x + phase_off * cycle;
        // Snap to cycle grid. Start one cycle before the visible slice so a
        // dash entering from the side renders cleanly.
        const int ix0 = static_cast<int>(std::floor((vx0 - scroll) / cycle)) - 1;
        const int ix1 = static_cast<int>(std::ceil ((vx1 - scroll) / cycle)) + 1;
        for (int ix = ix0; ix <= ix1; ++ix) {
            const float dash_x0 = ix * cycle + scroll;
            const float dash_x1 = dash_x0 + dash_len;
            const float ddx0 = std::max(dash_x0, vx0);
            const float ddx1 = std::min(dash_x1, vx1);
            if (ddx0 >= ddx1) continue;
            const unsigned char a = static_cast<unsigned char>(35.0f * edge_a);
            DrawLineEx(Vector2{ddx0, y}, Vector2{ddx1, y}, 1.5f,
                       Color{150, 200, 235, a});
        }
    }

    // --- Streak particles in parallax lanes ---
    // Three lanes at different scales/speeds for depth. Each "particle" is
    // rendered as a tapered streak (a short bright head + a dimmer trailing
    // tail) so the water reads as flowing, not bubbling.
    struct LaneCfg {
        float density_stride;   // smaller = more particles
        float speed_mult;       // 1.0 = base; <1 = far/slow, >1 = near/fast
        float streak_len;       // pixel length of each streak
        float head_alpha;       // brightest pixel alpha
        float thickness;        // line thickness
        int   y_div;            // dither: only every Nth row gets a particle
    };
    const LaneCfg lanes[] = {
        // far layer: subtle, long, slow
        { 220.0f, 0.55f, 28.0f, 70.0f,  1.5f, 1 },
        // mid layer: balanced
        { 130.0f, 0.85f, 22.0f, 130.0f, 1.8f, 1 },
        // near layer: bright, faster, more density
        {  85.0f, 1.20f, 18.0f, 200.0f, 2.2f, 1 },
    };
    const float base_speed_px = std::max(50.0f, c.strength * 1.4f);

    for (int li = 0; li < 3; ++li) {
        const LaneCfg& L = lanes[li];
        const float stride = L.density_stride;
        const float lane_speed = base_speed_px * L.speed_mult;
        const float phase = (t * lane_speed * c.dir.x) / stride;
        // Stable per-lane hash so different bands don't draw identical streaks.
        const uint32_t lane_hash = static_cast<uint32_t>(c.id) * 2654435761u
                                 + static_cast<uint32_t>(li) * 0xB7E15163u;
        const float lane_jitter = ((lane_hash >> 8) & 0xFFFFu) / 65535.0f;

        const int ix0 = static_cast<int>(std::floor(vx0 / stride)) - 1;
        const int ix1 = static_cast<int>(std::ceil (vx1 / stride)) + 1;
        for (int ix = ix0; ix <= ix1; ++ix) {
            // World-x of the streak HEAD.
            const float px = (static_cast<float>(ix) + lane_jitter
                              + std::fmod(phase, 1.0f)) * stride;
            if (px < vx0 - L.streak_len || px > vx1 + L.streak_len) continue;
            if (px < band_x0 || px > band_x1) continue;

            // Deterministic per-particle hash for y-offset + size jitter.
            uint32_t h2 = static_cast<uint32_t>(ix) * 0x85EBCA6Bu
                        + lane_hash;
            // y in [-0.85, +0.85] * h so streaks rarely sit at the very rim.
            float y_norm = ((h2 >> 8) & 0xFFFFu) / 65535.0f;       // 0..1
            y_norm = (y_norm - 0.5f) * 1.7f;                       // -0.85..0.85
            // Per-particle wobble keeps the lane organic across time.
            float wobble = std::sin(t * 1.5f + (h2 & 0xFFFu) * 0.0017f) * 5.0f;
            const float py = band_yc + y_norm * h + wobble;

            // Edge-aware alpha: dim toward the band rim AND toward the lane
            // extremes so streaks at very high or very low y read as fading.
            const float edge_t = 1.0f - std::abs(y_norm) / 0.85f;
            const float edge_a = std::clamp(edge_t * 1.4f, 0.0f, 1.0f);

            // Tail x is "behind" the head along the flow direction. Negative
            // sign because if flow is +x, streak trails to the LEFT.
            const float head_x = px;
            const float tail_x = px - c.dir.x * L.streak_len;

            const unsigned char head_a = static_cast<unsigned char>(L.head_alpha * edge_a);
            const unsigned char mid_a  = static_cast<unsigned char>(L.head_alpha * 0.6f * edge_a);
            const unsigned char tail_a = static_cast<unsigned char>(L.head_alpha * 0.18f * edge_a);

            // Render as three overlapping line segments to fake a tapered
            // alpha gradient (raylib DrawLineEx doesn't support per-vertex
            // alpha). Tail segment is longest + dimmest; head is shortest +
            // brightest. Visually reads as a comet-tail streak.
            const float t1_x = px - c.dir.x * L.streak_len * 0.66f;
            const float t2_x = px - c.dir.x * L.streak_len * 0.33f;
            DrawLineEx(Vector2{tail_x, py}, Vector2{t1_x, py}, L.thickness * 0.6f,
                       Color{140, 195, 235, tail_a});
            DrawLineEx(Vector2{t1_x,  py}, Vector2{t2_x, py}, L.thickness * 0.85f,
                       Color{180, 215, 245, mid_a});
            DrawLineEx(Vector2{t2_x,  py}, Vector2{head_x, py}, L.thickness,
                       Color{220, 240, 255, head_a});
            // Bright head pip: a tiny circle at the leading tip so the
            // streak looks like the head is "pulling" the trail.
            DrawCircleV(Vector2{head_x, py}, L.thickness * 0.6f,
                        Color{235, 250, 255, head_a});
        }
    }
}

// ---- Wormhole ----
// Shader-driven blue swirl, modelled on the black-hole shader but tuned for
// a *portal* feel rather than a destructive sink:
//   * Cool blue / cyan palette instead of red-orange.
//   * BRIGHT cyan-white aperture in the centre (vs the black hole's dark
//     event horizon) -- this is a doorway, not the abyss.
//   * Four-armed spiral with a counter-rotating second arm set so the
//     portal feels "energised".
//
// The shader receives `u_spin = wh.spin_phase` from the snapshot, so every
// client renders the swirl at the same angle on the same tick. With the
// host's spin_phase advancing deterministically (Simulation::buildSnapshot)
// the two endpoints of a pair end up perfectly counter-rotated visually
// because we negate spin for the partner inside the shader call below.
void drawWormhole(const WormholeSnap& wh, double now_sec) {
    ensureWormholeGfx();
    const Vector2 center{wh.pos.x, wh.pos.y};
    const float   r = wh.radius;
    const float   t = static_cast<float>(now_sec);

    // Outer pull-ring tell -- soft dark-purple disc + thin rim ring,
    // matching the moodier palette of the shader pass. Shows the capture
    // radius without lighting up the screen.
    {
        float pulse = 0.5f + 0.5f * std::sin(t * 1.0f);
        unsigned char a = static_cast<unsigned char>(24 + pulse * 18);
        DrawCircleV(center, r * 1.55f,
                    Color{60, 30, 110, static_cast<unsigned char>(a / 2)});
        DrawCircleLinesV(center, r * 1.55f,
                         Color{130, 80, 180, static_cast<unsigned char>(a)});
    }

    if (g_wh_gfx.initialized) {
        // Shader pass: a 1x1 white texture stretched to a square covering
        // the disc. The fragment shader handles all the visual heavy
        // lifting (spiral, aperture, rim glow). disc_extent slightly larger
        // than radius gives the alpha falloff room to fade out smoothly.
        const float disc_extent = r * 2.0f;
        const Rectangle dst{center.x - disc_extent, center.y - disc_extent,
                            disc_extent * 2.0f, disc_extent * 2.0f};
        // Aperture in shader UV space: uv is [-1, 1] across dst, so r/disc
        // is the normalised aperture radius.
        const float horizon_norm = r / disc_extent;
        // Sign-flip the spin every other endpoint of a pair (id is odd
        // for the second endpoint thanks to spawnWormholePair's
        // increment order) so paired portals counter-rotate visually.
        const float spin = wh.spin_phase * ((wh.id & 1u) ? -1.0f : 1.0f);

        BeginShaderMode(g_wh_gfx.shader);
        SetShaderValue(g_wh_gfx.shader, g_wh_gfx.loc_time,    &t,            SHADER_UNIFORM_FLOAT);
        SetShaderValue(g_wh_gfx.shader, g_wh_gfx.loc_horizon, &horizon_norm, SHADER_UNIFORM_FLOAT);
        SetShaderValue(g_wh_gfx.shader, g_wh_gfx.loc_spin,    &spin,         SHADER_UNIFORM_FLOAT);
        DrawTexturePro(g_wh_gfx.white,
                       Rectangle{0, 0, 1, 1},
                       dst,
                       Vector2{0, 0},
                       0.0f,
                       WHITE);
        EndShaderMode();
    } else {
        // Fallback if shader compilation failed: dim purple core + magenta
        // rim. Less interesting but the portal stays visible.
        DrawCircleV(center, r * 1.10f, Color{40, 20, 70, 220});
        DrawCircleV(center, r * 0.60f, Color{30, 12, 55, 240});
        DrawCircleLinesV(center, r,    Color{140, 60, 160, 230});
    }
}

// ---- Geyser ----
// Three visual phases mapping to GeyserSnap::state:
//   0 = Idle: dim bottom pool + slow lazy ring at the base.
//   1 = Telegraph: warning ring grows + flickers + brightens as it approaches
//        eruption. phase_norm reads as "how imminent".
//   2 = Erupt: bright burst flare with radiating spokes. Held one tick.
void drawGeyser(const GeyserSnap& g, double now_sec) {
    const Vector2 c{g.pos.x, g.pos.y};
    const float   r = g.radius;
    const float   t = static_cast<float>(now_sec);

    // ---- Base pool (always present) ----
    // Dark cyan-teal disc with a slightly brighter rim. Reads as a calm
    // pool when Idle; the higher-state layers paint over it.
    {
        DrawCircleV(c, r * 0.95f, Color{20, 50, 70, 210});
        DrawCircleV(c, r * 0.70f, Color{30, 70, 95, 220});
        DrawCircleLinesV(c, r, Color{90, 160, 200, 110});
    }

    switch (g.state) {
        case 0: { // Idle
            // Slow rotating cross of faint hints at the base. Cheap visual
            // "this thing exists" without competing with active threats.
            float pulse = 0.5f + 0.5f * std::sin(t * 0.6f);
            unsigned char a = static_cast<unsigned char>(35 + pulse * 35);
            float k = t * 0.3f;
            for (int i = 0; i < 4; ++i) {
                float ang = k + i * (kPi * 0.5f);
                Vector2 p{c.x + std::cos(ang) * r * 0.55f,
                          c.y + std::sin(ang) * r * 0.55f};
                DrawCircleV(p, 3.5f, Color{120, 200, 220, a});
            }
            // "Charging" radial sweep: maps phase_norm to a thin filled wedge
            // around the rim so attentive players can read "next eruption
            // soon" from the visual alone.
            if (g.phase_norm > 0.05f) {
                const int n = std::min<int>(28, static_cast<int>(g.phase_norm * 28.0f));
                for (int i = 0; i < n; ++i) {
                    float ang = -kPi * 0.5f + i * (kPi * 2.0f / 28.0f);
                    Vector2 p{c.x + std::cos(ang) * r * 0.92f,
                              c.y + std::sin(ang) * r * 0.92f};
                    DrawCircleV(p, 2.0f, Color{120, 220, 240, 180});
                }
            }
            break;
        }
        case 1: { // Telegraph
            // Warning ring grows from r*0.4 -> r*1.6 across phase_norm.
            // Color shifts cyan -> warm orange as eruption approaches so the
            // player feels the rising urgency.
            float p = std::clamp(g.phase_norm, 0.0f, 1.0f);
            float ring_r = r * (0.4f + p * 1.2f);
            unsigned char ir = static_cast<unsigned char>(120 + p * 130);
            unsigned char ig = static_cast<unsigned char>(220 - p * 90);
            unsigned char ib = static_cast<unsigned char>(240 - p * 180);
            // Pulsing alpha gives a "growing urgency" beat.
            float beat = 0.5f + 0.5f * std::sin(t * (3.0f + p * 12.0f));
            unsigned char a = static_cast<unsigned char>(160 + beat * 95 * p);
            DrawCircleLinesV(c, ring_r,
                             Color{ir, ig, ib, a});
            DrawCircleLinesV(c, ring_r * 1.04f,
                             Color{ir, ig, ib,
                                   static_cast<unsigned char>(a * 0.5f)});
            // Rising spout silhouette inside -- vertical streak suggesting
            // the upcoming eruption.
            float spout_h = r * 0.5f * p;
            DrawCircleV(Vector2{c.x, c.y - spout_h * 0.3f},
                        r * 0.20f * (0.6f + p * 0.6f),
                        Color{200, 230, 250,
                              static_cast<unsigned char>(120 + p * 100)});
            break;
        }
        case 2: { // Erupt
            // One-tick flash: bright burst + 12 outward spokes. Held for one
            // sim tick on the host (one snapshot frame); clients will see
            // it for ~33ms which reads clearly against the surrounding play.
            DrawCircleV(c, r * 1.55f, Color{255, 220, 140, 90});
            DrawCircleV(c, r * 1.10f, Color{255, 230, 170, 160});
            DrawCircleV(c, r * 0.65f, Color{255, 245, 200, 220});
            for (int i = 0; i < 12; ++i) {
                float ang = (i * (kPi * 2.0f / 12.0f));
                Vector2 a{c.x + std::cos(ang) * r * 0.65f,
                          c.y + std::sin(ang) * r * 0.65f};
                Vector2 b{c.x + std::cos(ang) * r * 1.85f,
                          c.y + std::sin(ang) * r * 1.85f};
                DrawLineEx(a, b, 4.0f, Color{255, 220, 130, 220});
            }
            DrawCircleLinesV(c, r * 1.55f, Color{255, 200, 110, 200});
            break;
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

    // ---- Body fill: shader-driven watery blob ----
    // Replaces the old flat DrawCircleV + hard outline. The shader produces
    // a wobbly soft-edged disc with a velocity-driven stretch and a baked
    // dark rim band (so we don't need DrawCircleLines for the silhouette).
    // Falls back to the legacy flat-circle path if the shader couldn't
    // compile.
    ensureCellBlobGfx();
    if (g_blob_gfx.initialized) {
        // Quad padding: the wobble + breath can extend the visual edge by
        // up to ~18% and the stretch can elongate by ~25%, so the rendered
        // quad has to be at least 1.25 * 1.18 = 1.475x the cell radius.
        // 1.55 gives a comfortable margin. The shader uses this constant
        // directly via u_padding.
        constexpr float kQuadPadding = 1.55f;

        // Velocity-based stretch. cellSpeed maxes around ~280 for starters
        // and ~840 during dash; launch_vel piles on top. We normalize by a
        // moderate reference (1500 u/s) and cap at +25% elongation -- enough
        // to read as motion without distorting the body grotesquely.
        Vec2 vel_dir{0.0f, 0.0f};
        const float speed = length(c.vel);
        if (speed > 1.0f) {
            vel_dir.x = c.vel.x / speed;
            vel_dir.y = c.vel.y / speed;
        }
        const float stretch = 1.0f + std::clamp(speed / 1500.0f, 0.0f, 0.25f);

        // Per-cell wobble phase. Hashed off the cell id so adjacent cells
        // don't ripple in sync. * 0.137 keeps the hash producing a smooth
        // distribution across the 0..2pi range.
        const float id_seed = static_cast<float>(c.id) * 0.137f;

        // Wobble amplitude: 0 = perfect circle (cell at rest, no recent
        // collisions). Composed of a velocity-derived baseline + any
        // transient collision pulse the event handler dropped on this
        // cell. Capped at 1.2 so even a stack of events doesn't blow the
        // shader's quad padding.
        const float vel_amp   = std::clamp(speed / 800.0f, 0.0f, 0.85f);
        const float pulse_amp = cellWobblePulse(c.id);
        const float wobble_amp = std::clamp(vel_amp + pulse_amp, 0.0f, 1.2f);

        // u_color expects 0..1 RGB. We've already mixed invuln pulses,
        // dash_telegraph bumps, stealth alpha, etc. into `fill`. Alpha
        // flows through u_alpha.
        const float r_col = static_cast<float>(fill.r) / 255.0f;
        const float g_col = static_cast<float>(fill.g) / 255.0f;
        const float b_col = static_cast<float>(fill.b) / 255.0f;
        const float a_col = static_cast<float>(fill.a) / 255.0f;

        const float t      = static_cast<float>(now_sec);
        const float padval = kQuadPadding;
        const float color3[3] = {r_col, g_col, b_col};
        const float veldir2[2] = {vel_dir.x, vel_dir.y};

        const float quad_half = r * kQuadPadding;
        const Rectangle dst{pos.x - quad_half, pos.y - quad_half,
                            quad_half * 2.0f, quad_half * 2.0f};

        BeginShaderMode(g_blob_gfx.shader);
        SetShaderValue(g_blob_gfx.shader, g_blob_gfx.loc_time,    &t,       SHADER_UNIFORM_FLOAT);
        // vec3 / vec2 uniforms need the VEC3 / VEC2 uniform type via
        // SetShaderValue (singular), NOT SetShaderValueV(..., FLOAT, n).
        // The latter uploads N separate floats into adjacent locations and
        // a vec3 ends up reading uninitialised (= zero, hence "completely
        // black") on most drivers.
        SetShaderValue(g_blob_gfx.shader, g_blob_gfx.loc_color,   color3,   SHADER_UNIFORM_VEC3);
        SetShaderValue(g_blob_gfx.shader, g_blob_gfx.loc_vel_dir, veldir2,  SHADER_UNIFORM_VEC2);
        SetShaderValue(g_blob_gfx.shader, g_blob_gfx.loc_stretch,    &stretch,    SHADER_UNIFORM_FLOAT);
        SetShaderValue(g_blob_gfx.shader, g_blob_gfx.loc_id_seed,    &id_seed,    SHADER_UNIFORM_FLOAT);
        SetShaderValue(g_blob_gfx.shader, g_blob_gfx.loc_alpha,      &a_col,      SHADER_UNIFORM_FLOAT);
        SetShaderValue(g_blob_gfx.shader, g_blob_gfx.loc_padding,    &padval,     SHADER_UNIFORM_FLOAT);
        SetShaderValue(g_blob_gfx.shader, g_blob_gfx.loc_wobble_amp, &wobble_amp, SHADER_UNIFORM_FLOAT);
        DrawTexturePro(g_blob_gfx.white,
                       Rectangle{0, 0, 1, 1},
                       dst,
                       Vector2{0, 0},
                       0.0f,
                       WHITE);
        EndShaderMode();
    } else {
        // Fallback path (shader compile failed). Old hard-circle look.
        DrawCircleV(Vector2{pos.x, pos.y}, r, fill);
    }

    // Outline ring -- always drawn so cells read clearly against the
    // backdrop. The circle is a perfect ring, not following the wobble,
    // which means the silhouette will protrude past it by a few pixels
    // when the blob wobbles outward. That's intentional -- the outline
    // marks the average / "rest" radius while the wobble is a subtle
    // motion-effect on top.
    if (s_high_contrast) {
        // 3-pass thick white outline (accessibility).
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

    // Prefer the registered display name (set by Client at match start /
    // welcome flow). Fall back to "<letter><id>" for bots + anyone we don't
    // have a name for yet. Names are truncated at 14 chars so a really long
    // one doesn't drown out the cell beneath it.
    char buf[32];
    auto name_it = s_player_names.find(c.owner);
    if (name_it != s_player_names.end() && !name_it->second.empty()) {
        const char* prefix = flair_star ? "*" : "";
        std::snprintf(buf, sizeof(buf), "%s%.14s",
                      prefix, name_it->second.c_str());
    } else {
        char letter = ai::letterForTag(c.personality_tag);
        if (flair_star) {
            std::snprintf(buf, sizeof(buf), "*%c%u", letter, static_cast<unsigned>(c.owner));
        } else {
            std::snprintf(buf, sizeof(buf), "%c%u", letter, static_cast<unsigned>(c.owner));
        }
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
    // All lazy GPU resources hang off this one call. Add new cleanups here as new
    // procedural shaders / textures get introduced.
    unloadBlackHoleGfx();
    unloadCometGfx();
    unloadWormholeGfx();
    unloadCellBlobGfx();
}

void Renderer::drawScreenBackdrop(int sw, int sh) const {
    // Vertical gradient base. Subtle (top ~12 brightness -> bottom ~24)
    // so the grid still reads cleanly against it. We're going for "deepening
    // toward the bottom" rather than a strong mood gradient.
    DrawRectangleGradientV(0, 0, sw, sh,
                           Color{12, 14, 22, 255},
                           Color{20, 24, 36, 255});

    // Soft corner vignette: four directional gradients that fade from black
    // at the edges to transparent toward the center. Together they look
    // approximately radial without the cost of a real radial gradient.
    DrawRectangleGradientV(0, 0, sw, 140,
                           Color{0, 0, 0, 120}, Color{0, 0, 0, 0});
    DrawRectangleGradientV(0, sh - 140, sw, 140,
                           Color{0, 0, 0, 0}, Color{0, 0, 0, 120});
    DrawRectangleGradientH(0, 0, 120, sh,
                           Color{0, 0, 0, 90}, Color{0, 0, 0, 0});
    DrawRectangleGradientH(sw - 120, 0, 120, sh,
                           Color{0, 0, 0, 0}, Color{0, 0, 0, 90});
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

    // Tidal current bands: lowest layer of "ambient terrain". Each band
    // stretches the full world width but the visible-slice clip inside
    // drawTidalCurrent keeps the work bounded by the camera AABB. AABB-cull
    // by y only (x always overlaps a band that spans the world width).
    for (const auto& cur : curr.currents) {
        const float band_y0 = cur.pos.y - cur.half_height;
        const float band_y1 = cur.pos.y + cur.half_height;
        if (band_y1 < view_min.y || band_y0 > view_max.y) continue;
        drawTidalCurrent(cur, now_sec, tuning.world_width, view_min, view_max);
    }

    // Geysers: stationary points; the eruption itself spawns food which
    // takes care of being visible via the regular food loop. Drawn before
    // black holes for the same layering reason.
    for (const auto& g : curr.geysers) {
        if (!circleInView(g.pos, g.radius * 2.0f, view_min, view_max)) continue;
        drawGeyser(g, now_sec);
    }

    // Wormholes: drawn before black holes but after currents/geysers so the
    // bright vortex sits on top of ambient flow without being covered by a
    // black hole's accretion disc.
    for (const auto& wh : curr.wormholes) {
        if (!circleInView(wh.pos, wh.radius * 1.5f, view_min, view_max)) continue;
        drawWormhole(wh, now_sec);
    }

    // Black holes: drawn before food/viruses/cells so the swirling visuals appear
    // as a backdrop, with cells layered on top -- except hiding cells, which the
    // black hole's occupancy dots represent instead.
    for (const auto& b : curr.blackholes) {
        if (!circleInView(b.pos, b.pull_radius, view_min, view_max)) continue;
        drawBlackHole(b, now_sec);
    }

    // Food: frustum-cull first (3600 entries × 16k² world means typically <1% on-screen).
    // Visual-only size bump for food, tier-aware. The sim's foodRadius
    // (eating collisions) stays unchanged; only the rendered body is
    // enlarged. Common food keeps its current 1.40x scale; each higher
    // tier gets a progressively bigger multiplier so rare drops are
    // *obviously* rare from across the map. With sim radii of 4/5/5.5/
    // 6.5/8.5/9.5 these scales produce visible diameters roughly
    // 5.6/7.5/9.1/12/15.7/20 px -- a clear visual hierarchy.
    auto foodVisualScale = [](float mass) {
        if (mass >= 30.0f) return 2.10f; // legendary
        if (mass >= 15.0f) return 1.85f; // settled / ejected pellets
        if (mass >= 8.0f)  return 1.85f; // epic
        if (mass >= 4.0f)  return 1.65f; // rare
        if (mass >= 2.0f)  return 1.50f; // uncommon
        return 1.40f;                    // common
    };

    // Render food with depth: per-pellet draw uses three (or four) layers --
    // a soft ambient glow halo (everyone), an optional pulsing halo for rare+
    // tiers (existing behaviour, re-scaled), the main coloured body, and a
    // small upper-left highlight dot for a hint of dimensionality. The
    // highlight position is deterministic from the food id so it doesn't
    // dance frame-to-frame.
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
        const float r_sim    = foodRadius(f.mass);
        const float r_body   = r_sim * foodVisualScale(f.mass);
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

        // ---- Layer 1: ambient soft glow (every pellet, including commons) ----
        // Subtle so it doesn't carpet-bomb the view; lifts pellets off the
        // grid backdrop. Tiered alpha so higher-mass food still reads as
        // brighter than the common ones via this layer alone.
        {
            unsigned char ambient_a =
                  f.mass >= 30.0f ? 75u
                : f.mass >= 10.0f ? 55u
                : f.mass >=  5.0f ? 45u
                : f.mass >=  2.0f ? 35u
                : /* common  */    28u;
            DrawCircleV(Vector2{pos.x, pos.y}, r_body * 1.85f,
                        Color{c.r, c.g, c.b, ambient_a});
        }

        // ---- Layer 2: rare+ pulsing halo (existing behaviour, re-tuned for the
        //               larger body) ----
        if (stationary && f.mass >= 5.0f) {
            float phase = static_cast<float>(f.id % 64) * 0.1f;
            float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(now_sec) * 4.0f + phase);
            float halo_strength = legendary ? 1.9f
                                : (f.mass >= 10.0f ? 1.0f : 0.55f);
            float halo_r_mult   = legendary ? 2.4f : 1.8f;
            unsigned char glow_a = static_cast<unsigned char>(
                std::min(255.0f, (35.0f + pulse * 50.0f) * halo_strength));
            DrawCircleV(Vector2{pos.x, pos.y}, r_body * halo_r_mult,
                        Color{c.r, c.g, c.b, glow_a});
        }

        // ---- Layer 3: darkened rim for body definition ----
        // Slightly larger circle in a darkened version of `c`, drawn under
        // the main body. Gives the pellet a subtle outline without an
        // explicit hard stroke.
        {
            Color rim{
                static_cast<unsigned char>(c.r * 0.55f),
                static_cast<unsigned char>(c.g * 0.55f),
                static_cast<unsigned char>(c.b * 0.55f),
                c.a,
            };
            DrawCircleV(Vector2{pos.x, pos.y}, r_body + 0.6f, rim);
        }

        // ---- Layer 4: main body ----
        DrawCircleV(Vector2{pos.x, pos.y}, r_body, c);

        // ---- Layer 5: upper-left highlight ----
        // Small bright dot offset toward the "light source" (upper-left).
        // Reads as a tiny specular highlight, giving the flat circle a hint
        // of 3D. Brighter version of `c`, scaled inward by ~30% of the body
        // radius so it sits firmly inside.
        {
            Color hi{
                static_cast<unsigned char>(std::min(255, (int)c.r + 80)),
                static_cast<unsigned char>(std::min(255, (int)c.g + 80)),
                static_cast<unsigned char>(std::min(255, (int)c.b + 80)),
                220,
            };
            const float hi_off = r_body * 0.35f;
            DrawCircleV(Vector2{pos.x - hi_off, pos.y - hi_off},
                        r_body * 0.30f, hi);
        }
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

    // Comets last so the fire ball draws OVER cells / food / viruses. Telegraph rays
    // span the full map so they're always in view; skip the per-comet head cull only
    // for the head pass (the line draws unconditionally during telegraph).
    for (const auto& cm : curr.comets) {
        Vec2 pos = cm.pos;
        if (have_prev && cm.telegraph_norm >= 1.0f) {
            for (const auto& prev_cm : prev.comets) {
                if (prev_cm.id == cm.id) {
                    pos = lerp(prev_cm.pos, cm.pos, alpha);
                    break;
                }
            }
        }
        CometSnap cm_local = cm;
        cm_local.pos = pos;
        drawComet(cm_local, now_sec);
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

    // Tidal current bands: thin faint cyan horizontal strips spanning the
    // full minimap width. Just the fill + a tiny direction tick -- no
    // centre-line, matching the in-world band visual.
    for (const auto& cur : snap.currents) {
        const float h_px  = std::max(1.0f, cur.half_height * sy);
        const float my    = toMy(cur.pos.y);
        // Strip fill (full minimap width).
        DrawRectangle(x0, static_cast<int>(my - h_px),
                      box_w, static_cast<int>(h_px * 2.0f),
                      Color{80, 150, 220, 35});
        // Direction tick at the centre of the visible minimap so the player
        // can read which way the current flows at a glance.
        const float cx = x0 + box_w * 0.5f;
        const Vector2 a{cx - cur.dir.x * 5.0f, my};
        const Vector2 b{cx + cur.dir.x * 5.0f, my};
        DrawLineEx(a, b, 1.5f, Color{220, 240, 255, 220});
        // Arrowhead pip.
        DrawCircleV(b, 1.5f, Color{220, 240, 255, 230});
    }

    // Geysers: small steady-state pip; brighter pip when erupting / about to
    // erupt so attentive players can pick the next event without looking up.
    for (const auto& g : snap.geysers) {
        const float mx = toMx(g.pos.x);
        const float my = toMy(g.pos.y);
        if (g.state == 2 /* Erupt */) {
            // Eruption flash on the minimap too -- big amber dot.
            DrawCircleV(Vector2{mx, my}, 5.0f, Color{255, 220, 130, 255});
            DrawCircleLinesV(Vector2{mx, my}, 7.0f, Color{255, 200, 110, 220});
        } else if (g.state == 1 /* Telegraph */) {
            unsigned char a = static_cast<unsigned char>(160 + g.phase_norm * 90);
            DrawCircleV(Vector2{mx, my}, 3.5f, Color{255, 180, 90, a});
        } else {
            DrawCircleV(Vector2{mx, my}, 2.5f, Color{120, 200, 220, 220});
        }
    }

    // Wormholes: paired endpoint dots connected by a thin link so the player
    // can plan teleports from the minimap.
    for (const auto& wh : snap.wormholes) {
        const float mx = toMx(wh.pos.x);
        const float my = toMy(wh.pos.y);
        DrawCircleV(Vector2{mx, my}, 3.0f, Color{220, 200, 255, 230});
        DrawCircleLinesV(Vector2{mx, my}, 5.0f, Color{170, 150, 240, 180});
        // Pair-link line: drawn once per pair (the partner-side iteration
        // would draw the same line, so we only render when this endpoint's
        // id is the lower of the pair to avoid double-draw).
        if (wh.id < wh.pair_id) {
            const WormholeSnap* partner = nullptr;
            for (const auto& other : snap.wormholes) {
                if (other.id == wh.pair_id) { partner = &other; break; }
            }
            if (partner) {
                const float px = toMx(partner->pos.x);
                const float py = toMy(partner->pos.y);
                DrawLineEx(Vector2{mx, my}, Vector2{px, py}, 1.0f,
                           Color{160, 140, 220, 90});
            }
        }
    }

    // Black holes (under cells but over ambient terrain). Pull radius as a
    // faint ring, event horizon as a filled purple disc.
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

    // Comets: telegraph as a thin orange line across the minimap (so the player can
    // see the predicted path even when looking elsewhere), plus a bright dot at the
    // current comet position once it's active.
    for (const auto& cm : snap.comets) {
        const float ax = toMx(cm.telegraph_start.x);
        const float ay = toMy(cm.telegraph_start.y);
        const float bx = toMx(cm.telegraph_end.x);
        const float by = toMy(cm.telegraph_end.y);
        const unsigned char a = static_cast<unsigned char>(
            120 + 100.0f * std::min(cm.telegraph_norm, 1.0f));
        DrawLineEx(Vector2{ax, ay}, Vector2{bx, by}, 1.5f,
                   Color{255, 150, 60, a});
        if (cm.telegraph_norm >= 1.0f) {
            const float hx = toMx(cm.pos.x);
            const float hy = toMy(cm.pos.y);
            DrawCircleV(Vector2{hx, hy}, 3.0f, Color{255, 220, 110, 255});
            DrawCircleLinesV(Vector2{hx, hy}, 5.0f, Color{255, 120, 30, 200});
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
