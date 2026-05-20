#include "MainMenu.h"

#include "UiWidgets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace cr {

namespace {

// Three tiers of background particles. Counts chosen so the total is light on
// fillrate (most particles are sub-pixel-radius circles).
constexpr int kDustCount  = 110;  // tiny moving dots, the "alive" layer
constexpr int kMoteCount  = 26;   // soft mid-size halos drifting slowly
constexpr int kBlobCount  = 5;    // huge faint blobs for depth
constexpr int kTotalCount = kDustCount + kMoteCount + kBlobCount;

// Local PRNG only for visual decoration -- no need to share the sim's RNG.
uint32_t s_menu_rng = 0xC0FFEEFEu;
uint32_t menuRand() {
    s_menu_rng ^= s_menu_rng << 13;
    s_menu_rng ^= s_menu_rng >> 17;
    s_menu_rng ^= s_menu_rng << 5;
    return s_menu_rng;
}
float menuRandRange(float lo, float hi) {
    return lo + (hi - lo) * (menuRand() & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

// Cooler palette for the dust + motes (drift colors). Gold-cream is reserved
// for the title halo so the focal area pops.
Color pickDustTint() {
    static const Color palette[] = {
        Color{180, 220, 240, 255}, // pale cyan
        Color{200, 235, 255, 255}, // ice blue
        Color{220, 220, 250, 255}, // lavender white
        Color{255, 240, 200, 255}, // warm cream (sparingly used)
        Color{160, 200, 220, 255}, // muted teal
    };
    return palette[menuRand() % (sizeof(palette) / sizeof(palette[0]))];
}

Color pickBlobTint() {
    // Deep cool blobs that read as "background nebula", not as cells.
    static const Color palette[] = {
        Color{ 40,  70, 110, 255},
        Color{ 60,  50,  90, 255},
        Color{ 30,  80, 100, 255},
        Color{ 70,  60, 100, 255},
        Color{ 35,  60,  90, 255},
    };
    return palette[menuRand() % (sizeof(palette) / sizeof(palette[0]))];
}

// Cubic ease-out used for smooth breathing alpha curves.
float easeOutCubic(float t) {
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

// 0..1 triangle wave (peaks every `period` seconds). Smoother than fabs-of-sin
// for breathing animations because the slope is constant.
float breathe(float t, float period) {
    float p = std::fmod(t, period) / period;          // 0..1
    float tri = p < 0.5f ? (p * 2.0f) : (2.0f - p * 2.0f);
    return easeOutCubic(tri);
}

} // namespace

MainMenu::MainMenu() = default;

void MainMenu::ensureBgInit(int sw, int sh) {
    if (bg_inited_ && bg_init_w_ == sw && bg_init_h_ == sh) return;
    particles_.clear();
    particles_.reserve(kTotalCount);

    // ---- Dust (tiny, fast-ish, bright) ----
    for (int i = 0; i < kDustCount; ++i) {
        Particle p;
        p.x  = menuRandRange(0.0f, static_cast<float>(sw));
        p.y  = menuRandRange(0.0f, static_cast<float>(sh));
        p.vx = menuRandRange(-12.0f, 12.0f);
        p.vy = menuRandRange( -8.0f,  8.0f);
        p.r  = menuRandRange(1.0f, 2.4f);
        p.tint  = pickDustTint();
        p.phase = menuRandRange(0.0f, 6.28318f);
        particles_.push_back(p);
    }
    // ---- Motes (soft mid-glows) ----
    for (int i = 0; i < kMoteCount; ++i) {
        Particle p;
        p.x  = menuRandRange(0.0f, static_cast<float>(sw));
        p.y  = menuRandRange(0.0f, static_cast<float>(sh));
        p.vx = menuRandRange(-6.0f, 6.0f);
        p.vy = menuRandRange(-4.0f, 4.0f);
        p.r  = menuRandRange(4.0f, 9.0f);
        p.tint  = pickDustTint();
        p.phase = menuRandRange(0.0f, 6.28318f);
        particles_.push_back(p);
    }
    // ---- Huge faint blobs for depth ----
    for (int i = 0; i < kBlobCount; ++i) {
        Particle p;
        p.x  = menuRandRange(-50.0f, static_cast<float>(sw + 50));
        p.y  = menuRandRange(-50.0f, static_cast<float>(sh + 50));
        p.vx = menuRandRange(-3.0f, 3.0f);
        p.vy = menuRandRange(-2.0f, 2.0f);
        p.r  = menuRandRange(180.0f, 320.0f);
        p.tint  = pickBlobTint();
        p.phase = menuRandRange(0.0f, 6.28318f);
        particles_.push_back(p);
    }
    bg_inited_ = true;
    bg_init_w_ = sw;
    bg_init_h_ = sh;
}

void MainMenu::update(float frame_dt, int sw, int sh) {
    ensureBgInit(sw, sh);
    anim_time_ += frame_dt;

    // Drift particles with wrap-around. All three tiers share the same loop.
    for (auto& p : particles_) {
        p.x += p.vx * frame_dt;
        p.y += p.vy * frame_dt;
        if (p.x < -p.r)        p.x = sw + p.r;
        if (p.x >  sw + p.r)   p.x = -p.r;
        if (p.y < -p.r)        p.y = sh + p.r;
        if (p.y >  sh + p.r)   p.y = -p.r;
    }
}

MenuAction MainMenu::render(int sw, int sh, const SaveData& save) {
    // ---------- Layer 1: gradient base ----------
    // Vertical gradient from near-black (top) to a deeper, slightly warmer
    // slate at the bottom. Cell Royale lives in a "microscope at midnight"
    // mood -- darker than a typical UI, but with enough variance that the
    // particles + halo read clearly.
    DrawRectangleGradientV(0, 0, sw, sh,
                           Color{ 6,  9, 16, 255},
                           Color{18, 22, 36, 255});

    // ---------- Layer 2: huge faint blobs (depth) ----------
    // Drawn before the gradient overlay so they sit *behind* everything but
    // the base gradient. Two passes per blob -- a softer outer halo + a
    // slightly tighter inner fill -- to mimic a radial gradient cheaply.
    for (size_t i = kDustCount + kMoteCount; i < particles_.size(); ++i) {
        const auto& p = particles_[i];
        float breath = 0.85f + 0.15f * std::sin(anim_time_ * 0.4f + p.phase);
        float r = p.r * breath;
        DrawCircle(static_cast<int>(p.x), static_cast<int>(p.y),
                   r, Color{p.tint.r, p.tint.g, p.tint.b, 22});
        DrawCircle(static_cast<int>(p.x), static_cast<int>(p.y),
                   r * 0.55f, Color{p.tint.r, p.tint.g, p.tint.b, 34});
    }

    // ---------- Layer 3: title halo (focal radial glow) ----------
    // A warm radial glow sits behind the title -- breathing alpha (slow,
    // smooth) instead of breathing size, so it reads as a steady ambient
    // light rather than a low-FPS pulse. Layered concentric circles
    // approximate a radial gradient. Radii scale by uiScale so the halo
    // grows to match a fullscreen window instead of hugging the centre.
    {
        const float ui       = uiScale(sw, sh);
        const float title_cx = sw * 0.5f;
        const float title_cy = sh * 0.22f;
        const float breath   = breathe(anim_time_, 5.5f);          // 0..1
        const float halo_a   = 35.0f + 25.0f * breath;
        const Color halo     = Color{255, 200, 110,
                                     static_cast<unsigned char>(halo_a)};
        DrawCircle(static_cast<int>(title_cx), static_cast<int>(title_cy),
                   420.0f * ui, Color{halo.r, halo.g, halo.b,
                                 static_cast<unsigned char>(halo.a * 0.35f)});
        DrawCircle(static_cast<int>(title_cx), static_cast<int>(title_cy),
                   320.0f * ui, Color{halo.r, halo.g, halo.b,
                                 static_cast<unsigned char>(halo.a * 0.55f)});
        DrawCircle(static_cast<int>(title_cx), static_cast<int>(title_cy),
                   220.0f * ui, Color{halo.r, halo.g, halo.b,
                                 static_cast<unsigned char>(halo.a * 0.85f)});
        DrawCircle(static_cast<int>(title_cx), static_cast<int>(title_cy),
                   130.0f * ui, halo);
    }

    // ---------- Layer 4: motes (soft mid glows) ----------
    for (size_t i = kDustCount; i < kDustCount + kMoteCount; ++i) {
        const auto& p = particles_[i];
        // Per-particle alpha shimmer so the layer subtly twinkles.
        float a = 32.0f + 28.0f * (0.5f + 0.5f * std::sin(anim_time_ * 1.1f + p.phase));
        DrawCircle(static_cast<int>(p.x), static_cast<int>(p.y),
                   p.r * 2.5f,
                   Color{p.tint.r, p.tint.g, p.tint.b,
                         static_cast<unsigned char>(a * 0.45f)});
        DrawCircle(static_cast<int>(p.x), static_cast<int>(p.y),
                   p.r,
                   Color{p.tint.r, p.tint.g, p.tint.b,
                         static_cast<unsigned char>(a)});
    }

    // ---------- Layer 5: dust (bright tiny dots) ----------
    for (size_t i = 0; i < kDustCount; ++i) {
        const auto& p = particles_[i];
        float a = 110.0f + 80.0f * (0.5f + 0.5f * std::sin(anim_time_ * 1.7f + p.phase));
        DrawCircle(static_cast<int>(p.x), static_cast<int>(p.y),
                   p.r,
                   Color{p.tint.r, p.tint.g, p.tint.b,
                         static_cast<unsigned char>(std::min(255.0f, a))});
    }

    // ---------- Layer 6: vignette ----------
    // Radial-ish vignette (top/bottom/sides gradients composited). Cheap
    // approximation that nicely frames the central content. Vignette
    // thickness scales by uiScale so it stays proportional on big windows.
    {
        const int vt = uiPx(sw, sh, 220);
        const int vb = uiPx(sw, sh, 200);
        const int vs = uiPx(sw, sh, 180);
        DrawRectangleGradientV(0, 0, sw, vt,
                               Color{0, 0, 0, 180}, Color{0, 0, 0, 0});
        DrawRectangleGradientV(0, sh - vb, sw, vb,
                               Color{0, 0, 0, 0}, Color{0, 0, 0, 200});
        DrawRectangleGradientH(0, 0, vs, sh,
                               Color{0, 0, 0, 130}, Color{0, 0, 0, 0});
        DrawRectangleGradientH(sw - vs, 0, vs, sh,
                               Color{0, 0, 0, 0}, Color{0, 0, 0, 130});
    }

    // Subtle top edge accent line -- warm tint to thread through the halo.
    {
        const int edge = std::max(2, uiPx(sw, sh, 3));
        DrawRectangleGradientH(0, 0, sw, edge,
                               Color{255, 200, 110, 0}, Color{255, 200, 110, 80});
        DrawRectangleGradientH(0, 0, sw, edge,
                               Color{255, 200, 110, 80}, Color{255, 200, 110, 0});
    }

    // ---------- Eyebrow tag above the title ----------
    {
        const char* eyebrow = "CELL  ROYALE";
        int ey      = static_cast<int>(sh * 0.10f);
        int e_size  = uiPx(sw, sh, 13);
        int ew      = MeasureText(eyebrow, e_size);
        int line_w  = uiPx(sw, sh, 60);
        int line_g  = uiPx(sw, sh, 10);
        int line_off= uiPx(sw, sh, 70);
        DrawRectangle((sw - ew) / 2 - line_off, ey + e_size / 2, line_w, 1,
                      Color{220, 200, 150, 180});
        DrawRectangle((sw + ew) / 2 + line_g,   ey + e_size / 2, line_w, 1,
                      Color{220, 200, 150, 180});
        DrawText(eyebrow, (sw - ew) / 2, ey, e_size,
                 Color{220, 200, 150, 230});
    }

    // ---------- Title ----------
    // FIXED font size (no per-frame jitter), but scaled by uiScale so
    // fullscreen at 4K shows it 3x as big as windowed 1280x720.
    const char* title = "CELL ROYALE";
    const int   t_size = uiPx(sw, sh, 96);
    const int   tw     = MeasureText(title, t_size);
    const int   ty     = static_cast<int>(sh * 0.16f);
    const int   shad_a = std::max(1, uiPx(sw, sh, 7));
    const int   shad_b = std::max(1, uiPx(sw, sh, 9));
    const int   shad_c = std::max(1, uiPx(sw, sh, 3));
    const int   shad_d = std::max(1, uiPx(sw, sh, 4));
    DrawText(title, (sw - tw) / 2 + shad_a, ty + shad_b, t_size, Color{0, 0, 0, 120});
    DrawText(title, (sw - tw) / 2 + shad_c, ty + shad_d, t_size, Color{0, 0, 0, 200});
    DrawText(title, (sw - tw) / 2,          ty,          t_size, Color{255, 230, 170, 255});

    // Thin gold underline below the title that fades in/out subtly.
    {
        float p = breathe(anim_time_, 5.5f);
        unsigned char a = static_cast<unsigned char>(120 + p * 60);
        const int ul_inset = uiPx(sw, sh, 20);
        const int ul_y     = ty + t_size + uiPx(sw, sh, 4);
        const int ul_h     = std::max(2, uiPx(sw, sh, 2));
        DrawRectangleGradientH((sw - tw) / 2 + ul_inset, ul_y,
                               tw - 2 * ul_inset, ul_h,
                               Color{255, 200, 110, 0},
                               Color{255, 200, 110, a});
        DrawRectangleGradientH((sw - tw) / 2 + ul_inset, ul_y,
                               tw - 2 * ul_inset, ul_h,
                               Color{255, 200, 110, a},
                               Color{255, 200, 110, 0});
    }

    // ---------- Tagline ----------
    {
        const char* tagline = "eat   --   grow   --   survive";
        int g_size = uiPx(sw, sh, 18);
        int gw     = MeasureText(tagline, g_size);
        int gy     = ty + t_size + uiPx(sw, sh, 24);
        DrawText(tagline, (sw - gw) / 2 + 1, gy + 1, g_size,
                 Color{0, 0, 0, 160});
        DrawText(tagline, (sw - gw) / 2,     gy,     g_size,
                 Color{195, 210, 230, 220});
    }

    // ---------- Buttons ----------
    const int btn_w = uiPx(sw, sh, 340);
    const int btn_h = uiPx(sw, sh,  76);
    const int btn_x = (sw - btn_w) / 2;
    int       btn_y = static_cast<int>(sh * 0.54f);

    MenuAction action = MenuAction::None;

    // Primary: VS AI (warm green)
    if (drawButton(Rectangle{(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h},
                   "VS AI", uiPx(sw, sh, 34),
                   Color{55, 145, 95, 255},
                   Color{255, 255, 255, 255})) {
        action = MenuAction::StartVsAI;
    }
    btn_y += btn_h + uiPx(sw, sh, 18);

    // Secondary: Royale -- opens the Local / Global sub-menu.
    if (drawButtonWithSub(
            Rectangle{(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h},
            "ROYALE", uiPx(sw, sh, 30),
            "multiplayer  --  local or global", uiPx(sw, sh, 13),
            Color{75, 90, 160, 255},
            Color{225, 230, 250, 255})) {
        action = MenuAction::ShowRoyaleMenu;
    }
    btn_y += btn_h + uiPx(sw, sh, 22);

    // Tertiary row: SETTINGS + TUTORIAL side-by-side.
    {
        const int gap   = uiPx(sw, sh, 12);
        const int sub_w = (btn_w - gap) / 2;
        const int sub_h = uiPx(sw, sh, 52);
        const int sub_fs= uiPx(sw, sh, 22);
        if (drawButton(
                Rectangle{(float)btn_x, (float)btn_y, (float)sub_w, (float)sub_h},
                "SETTINGS", sub_fs,
                Color{42, 50, 72, 255}, Color{220, 225, 245, 230})) {
            action = MenuAction::ShowSettings;
        }
        if (drawButton(
                Rectangle{(float)(btn_x + sub_w + gap), (float)btn_y,
                          (float)sub_w, (float)sub_h},
                "TUTORIAL", sub_fs,
                Color{42, 50, 72, 255}, Color{220, 225, 245, 230})) {
            action = MenuAction::ReplayIntro;
        }
    }

    // ---------- Lifetime stats panel (top-right) ----------
    {
        char line1[96], line2[160];
        std::snprintf(line1, sizeof(line1), "Level %u    %u XP",
                      save.level, save.total_xp);
        std::snprintf(line2, sizeof(line2),
                      "Best mass %.0f    Best combo x%u    %u game%s",
                      save.best_mass, save.best_combo,
                      save.games_played, save.games_played == 1 ? "" : "s");
        const int fs1     = uiPx(sw, sh, 18);
        const int fs2     = uiPx(sw, sh, 13);
        const int l1w     = MeasureText(line1, fs1);
        const int l2w     = MeasureText(line2, fs2);
        const int padx    = uiPx(sw, sh, 30);
        const int panel_w = std::max(l1w, l2w) + padx;
        const int panel_h = save.games_played > 0 ? uiPx(sw, sh, 64) : uiPx(sw, sh, 38);
        const int margin  = uiPx(sw, sh, 22);
        const int panel_x = sw - panel_w - margin;
        const int panel_y = margin;

        DrawRectangleGradientV(panel_x, panel_y, panel_w, panel_h,
                               Color{26, 32, 50, 220},
                               Color{14, 18, 30, 220});
        DrawRectangleRoundedLines(
            Rectangle{(float)panel_x, (float)panel_y,
                      (float)panel_w, (float)panel_h},
            0.18f, 6, Color{255, 220, 130, 60});
        DrawCircle(panel_x + uiPx(sw, sh, 10), panel_y + uiPx(sw, sh, 12),
                   std::max(2.0f, 3.0f * uiScale(sw, sh)),
                   Color{255, 200, 110, 230});
        DrawText(line1, panel_x + uiPx(sw, sh, 22), panel_y + uiPx(sw, sh, 8),
                 fs1, Color{255, 225, 160, 240});
        if (save.games_played > 0) {
            DrawText(line2, panel_x + uiPx(sw, sh, 22),
                     panel_y + uiPx(sw, sh, 36), fs2,
                     Color{200, 210, 230, 200});
        }
    }

    // ---------- Footer hint ----------
    {
        const char* foot = "press  ESC  to quit       --       ENTER / SPACE  for  VS AI";
        int fs = uiPx(sw, sh, 13);
        int fw = MeasureText(foot, fs);
        DrawText(foot, (sw - fw) / 2, sh - uiPx(sw, sh, 32), fs,
                 Color{140, 150, 170, 180});
    }

    // ---------- Keyboard escape + quick-start ----------
    if (IsKeyPressed(KEY_ESCAPE)) {
        action = MenuAction::Quit;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
        action = MenuAction::StartVsAI;
    }

    return action;
}

} // namespace cr
