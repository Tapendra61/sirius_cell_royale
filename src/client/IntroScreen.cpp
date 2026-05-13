#include "IntroScreen.h"

#include "core/Command.h"

#include <algorithm>
#include <cstdio>

namespace cr {

namespace {

// Seed for the intro's mini-sim. Deterministic so the demo plays the same way every
// time we hit it -- if it ever looks weird in playtest, we can reproduce it.
constexpr uint64_t kIntroSeed     = 0xC311'2025ULL;
constexpr int      kIntroStartMass = 360; // above default split threshold (~300)

constexpr float kSimDt = 1.0f / 30.0f;
constexpr int   kTicksPerSec = 30;

} // namespace

IntroScreen::IntroScreen(const Tuning& tuning)
    : tuning_(tuning),
      sim_(kIntroSeed, tuning) {
    // Spawn the demo player near the centre with enough mass that the scripted Split
    // is guaranteed to fire (relying on natural eating-to-threshold inside a 15s
    // window is too RNG-sensitive for a single-take "trailer").
    const Vec2 center{static_cast<float>(tuning_.world_width)  * 0.5f,
                      static_cast<float>(tuning_.world_height) * 0.5f};
    player_cell_ = sim_.world().spawnCell(player_id_, center,
                                          static_cast<float>(kIntroStartMass));

    camera_.snapTo(center, static_cast<float>(kIntroStartMass));
    interp_.push(sim_.buildSnapshot());

    buildScript();
}

void IntroScreen::buildScript() {
    // Times are wall-clock seconds. queue_tick is the sim tick at which we queue the
    // command (commands take effect on the *next* tick). Movement targets are world
    // coords -- the camera follows the player, so these read as "drift in this
    // direction".
    auto secToTick = [](float sec) {
        return static_cast<Tick>(sec * kTicksPerSec);
    };

    const Vec2 c{static_cast<float>(tuning_.world_width)  * 0.5f,
                 static_cast<float>(tuning_.world_height) * 0.5f};

    // Each row: (when, what, where for Move). Movement targets are offsets from the
    // world centre, picked to give a curving exploration path through food clusters.
    const ScriptCmd rows[] = {
        {secToTick(0.5f),  CmdKind::Move,  Vec2{c.x + 800.0f,  c.y - 500.0f}},
        {secToTick(2.0f),  CmdKind::Move,  Vec2{c.x + 1400.0f, c.y + 600.0f}},
        {secToTick(4.0f),  CmdKind::Split, {}},
        {secToTick(4.5f),  CmdKind::Move,  Vec2{c.x + 2200.0f, c.y + 200.0f}},
        {secToTick(6.5f),  CmdKind::Dash,  {}},
        {secToTick(7.5f),  CmdKind::Move,  Vec2{c.x + 1800.0f, c.y - 900.0f}},
        {secToTick(9.5f),  CmdKind::Move,  Vec2{c.x - 600.0f,  c.y - 1200.0f}},
        {secToTick(11.5f), CmdKind::Move,  Vec2{c.x - 1500.0f, c.y - 300.0f}},
        {secToTick(13.0f), CmdKind::Move,  Vec2{c.x - 900.0f,  c.y + 900.0f}},
    };
    script_.assign(std::begin(rows), std::end(rows));
}

void IntroScreen::queuePendingCommands() {
    const Tick now = sim_.currentTick();
    while (next_cmd_idx_ < static_cast<int>(script_.size())
           && script_[next_cmd_idx_].queue_tick <= now) {
        const ScriptCmd& sc = script_[next_cmd_idx_++];
        Command cmd;
        cmd.player = player_id_;
        cmd.tick   = now;
        switch (sc.kind) {
            case CmdKind::Move:  cmd.payload = MoveCmd{sc.move_target}; break;
            case CmdKind::Split: cmd.payload = SplitCmd{};              break;
            case CmdKind::Dash:  cmd.payload = DashCmd{};               break;
        }
        sim_.queueCommand(cmd);
    }
}

bool IntroScreen::update(float frame_dt) {
    if (done_) return true;

    // Skip on any input. Mouse click + common keys cover desktop; touch falls through
    // via the standard mouse-button mapping on macOS so this still works on iPads etc.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
        || IsKeyPressed(KEY_SPACE)
        || IsKeyPressed(KEY_ENTER)
        || IsKeyPressed(KEY_ESCAPE)) {
        done_ = true;
        return true;
    }

    elapsed_sec_ += frame_dt;
    if (elapsed_sec_ >= kDurationSec) {
        done_ = true;
        return true;
    }

    // Sim is ticked at the fixed 30Hz timestep just like the main match loop. We
    // track the leftover accumulator and use it as the render-time interpolation
    // alpha so motion is smooth at 60+ fps instead of jumping each sim tick.
    accumulator_ += static_cast<double>(frame_dt);
    int safety = 0;
    while (accumulator_ >= kSimDt && safety < 8) {
        queuePendingCommands();
        sim_.tick(kSimDt);
        interp_.push(sim_.buildSnapshot());
        sim_.takeEvents(); // drain; nobody listens
        accumulator_ -= kSimDt;
        ++safety;
    }
    if (accumulator_ >= kSimDt) accumulator_ = 0.0;
    render_alpha_ = static_cast<float>(accumulator_ / kSimDt);

    // Camera follows the player's largest piece (handles the split case).
    EntityId  follow = player_cell_;
    float     follow_mass = static_cast<float>(kIntroStartMass);
    {
        float best_mass = 0.0f;
        for (const auto& c : sim_.world().cells()) {
            if (c.owner == player_id_ && c.mass > best_mass) {
                best_mass = c.mass;
                follow    = c.id;
            }
        }
        if (best_mass > 0.0f) follow_mass = best_mass;
    }
    if (const Cell* fc = sim_.world().findCell(follow)) {
        camera_.setTarget(fc->pos, fc->mass);
    } else {
        // Player got eaten? Stay focused on world centre rather than yanking around.
        camera_.setTarget(Vec2{static_cast<float>(tuning_.world_width)  * 0.5f,
                               static_cast<float>(tuning_.world_height) * 0.5f},
                          follow_mass);
    }
    camera_.update(frame_dt);

    return false;
}

void IntroScreen::render(int sw, int sh) {
    ClearBackground(Color{18, 22, 30, 255});

    // World.
    Camera2D cam = camera_.toCamera2D(sw, sh);
    BeginMode2D(cam);
    const float inv_zoom    = (cam.zoom > 0.0001f) ? 1.0f / cam.zoom : 1.0f;
    const float view_half_w = static_cast<float>(sw) * 0.5f * inv_zoom;
    const float view_half_h = static_cast<float>(sh) * 0.5f * inv_zoom;
    constexpr float margin = 64.0f;
    const Vec2 view_min{cam.target.x - view_half_w - margin,
                        cam.target.y - view_half_h - margin};
    const Vec2 view_max{cam.target.x + view_half_w + margin,
                        cam.target.y + view_half_h + margin};
    renderer_.drawWorld(interp_, tuning_, render_alpha_, view_min, view_max,
                        player_cell_, player_id_, /*watched_level=*/1);
    EndMode2D();

    // Fade-in at start, fade-out at end. Both ~0.6s. Renders as a black rectangle
    // with an animated alpha so the cut to menu doesn't pop.
    float fade_alpha = 0.0f;
    if (elapsed_sec_ < kFadeInDurationSec) {
        fade_alpha = 1.0f - (elapsed_sec_ / kFadeInDurationSec);
    } else if (elapsed_sec_ > kDurationSec - kFadeOutDurationSec) {
        fade_alpha = (elapsed_sec_ - (kDurationSec - kFadeOutDurationSec))
                   / kFadeOutDurationSec;
    }
    if (fade_alpha > 0.0f) {
        unsigned char a = static_cast<unsigned char>(
            std::clamp(fade_alpha, 0.0f, 1.0f) * 255.0f);
        DrawRectangle(0, 0, sw, sh, Color{0, 0, 0, a});
    }

    // --- Scripted text overlays ---
    // Each entry: (start, end, optional small subtitle, main text, font size). Alpha
    // fades in over the first 0.3s and out over the last 0.3s of the window so
    // transitions are gentle. Times line up with the gameplay script above:
    //   0.0 - 2.0  : "CELL ROYALE" title
    //   2.4 - 4.3  : "EAT FOOD TO GROW" -- player is eating during this window
    //   4.3 - 6.3  : "SPLIT (SPACE)" -- split fires at 4.0s in the script
    //   6.6 - 8.6  : "DASH (SHIFT)" -- dash fires at 6.5s
    //   9.0 - 11.0 : "GROW BIG. EAT BOTS." -- generic gameplay tagline
    //   13.5- 15.0 : "GOOD LUCK." -- final beat into menu
    struct Overlay {
        float       start, end;
        const char* main;
        const char* sub;     // optional small subtitle above the main line
        int         size;
        Color       color;
    };
    const Color kAccent = Color{255, 215, 130, 255};
    const Color kWhite  = Color{240, 240, 245, 255};
    const Overlay overlays[] = {
        {0.0f,  2.0f,  "CELL ROYALE",         nullptr,         64, kAccent},
        {2.4f,  4.3f,  "EAT FOOD TO GROW",    nullptr,         36, kWhite},
        {4.3f,  6.3f,  "SPLIT  (space)",      nullptr,         36, kWhite},
        {6.6f,  8.6f,  "DASH  (shift)",       nullptr,         36, kWhite},
        {9.0f,  11.0f, "GROW BIG.  EAT BOTS.", nullptr,        32, kWhite},
        {13.0f, 15.0f, "GOOD LUCK.",          nullptr,         36, kAccent},
    };

    for (const Overlay& o : overlays) {
        if (elapsed_sec_ < o.start || elapsed_sec_ > o.end) continue;
        const float dur     = o.end - o.start;
        const float fade    = std::min(0.3f, dur * 0.25f);
        const float t       = elapsed_sec_ - o.start;
        float alpha = 1.0f;
        if (t < fade)             alpha = t / fade;
        else if (t > dur - fade)  alpha = (dur - t) / fade;
        alpha = std::clamp(alpha, 0.0f, 1.0f);

        unsigned char a = static_cast<unsigned char>(alpha * 240.0f);
        Color body{o.color.r, o.color.g, o.color.b, a};
        Color shadow{0, 0, 0, static_cast<unsigned char>(a * 0.7f)};
        int tw = MeasureText(o.main, o.size);
        int tx = sw / 2 - tw / 2;
        int ty = sh / 3;
        DrawText(o.main, tx + 3, ty + 3, o.size, shadow);
        DrawText(o.main, tx,     ty,     o.size, body);
        if (o.sub) {
            int subsz = std::max(12, o.size / 3);
            int sub_w = MeasureText(o.sub, subsz);
            DrawText(o.sub, sw / 2 - sub_w / 2,
                     ty - subsz - 4, subsz,
                     Color{200, 210, 230, a});
        }
    }

    // "Press any key to skip" hint -- only after the title fades, low key.
    if (elapsed_sec_ > 2.0f && elapsed_sec_ < kDurationSec - kFadeOutDurationSec) {
        const char* hint = "press any key to skip";
        const int   h_size = 16;
        int hint_w = MeasureText(hint, h_size);
        DrawText(hint, sw / 2 - hint_w / 2, sh - 36, h_size,
                 Color{180, 190, 210, 160});
    }
}

} // namespace cr
