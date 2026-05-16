#include "IntroScreen.h"

#include "core/Command.h"

#include <algorithm>
#include <cstdio>

namespace cr {

namespace {

// Seed for the intro's mini-sim. Deterministic so the demo plays the same way every
// time we hit it -- if it ever looks weird in playtest, we can reproduce it.
constexpr uint64_t kIntroSeed     = 0xC311'2025ULL;
// Tuned for the new 19s script. The cell splits at 4s (halving its mass),
// then drifts + ejects + eats for ~8s before the Mass Blast (Q) fires at
// 12.8s. Q requires >= 300 mass on the caster. With base 700 -> 350 post-
// split, even after ~8s of decay (~4/s) + 2 ejects (-36) we land near 280
// from decay alone but food intake keeps the largest cell comfortably above
// 300 so the blast fires reliably. Doubles as "the demo cell starts big",
// which reads as the cinematic-trailer mood we want here anyway.
constexpr int      kIntroStartMass = 700;

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

    // Each row: (when, what, where for Move). Movement targets are offsets
    // from the world centre, picked to give a curving exploration path
    // through food clusters. The script is sequenced to line up with the
    // text overlays in render() -- each ability fires ~0.3s BEFORE its
    // overlay appears, so the visual cue lands first and the text confirms
    // what the player just saw.
    const ScriptCmd rows[] = {
        // ----- 0-4s: drift + eat food (MOVE beat) -----
        {secToTick(0.5f),  CmdKind::Move,  Vec2{c.x + 800.0f,  c.y - 500.0f}},
        {secToTick(2.0f),  CmdKind::Move,  Vec2{c.x + 1400.0f, c.y + 600.0f}},

        // ----- 4-6s: SPLIT beat -----
        {secToTick(4.0f),  CmdKind::Split, {}},
        {secToTick(4.5f),  CmdKind::Move,  Vec2{c.x + 2200.0f, c.y + 200.0f}},

        // ----- 6-8s: DASH beat -----
        {secToTick(6.5f),  CmdKind::Dash,  {}},
        {secToTick(7.5f),  CmdKind::Move,  Vec2{c.x + 1800.0f, c.y - 900.0f}},

        // ----- 8-11s: EJECT (W) beat. Drift, then eject twice so the
        //              player clearly sees the small pellets shoot out.
        {secToTick(9.0f),  CmdKind::Eject, {}},
        {secToTick(9.3f),  CmdKind::Eject, {}},
        {secToTick(9.8f),  CmdKind::Move,  Vec2{c.x + 600.0f,  c.y - 1500.0f}},

        // ----- 11-14s: MASS BLAST (Q) beat -----
        // Move toward a denser bot cluster so the blast actually pushes
        // things. The world centre tends to have plenty of bots given the
        // default tuning.
        {secToTick(11.0f), CmdKind::Move,  Vec2{c.x,           c.y + 200.0f}},
        {secToTick(12.8f), CmdKind::Blast, {}},

        // ----- 14-19s: final drift; the "good luck" + Royale teaser beats
        //               play over this. Slow curve back through the centre.
        {secToTick(14.0f), CmdKind::Move,  Vec2{c.x - 800.0f,  c.y - 300.0f}},
        {secToTick(15.5f), CmdKind::Move,  Vec2{c.x - 1500.0f, c.y + 400.0f}},
        {secToTick(17.0f), CmdKind::Move,  Vec2{c.x - 700.0f,  c.y + 900.0f}},
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
            case CmdKind::Eject: cmd.payload = EjectCmd{};              break;
            case CmdKind::Blast: cmd.payload = BlastCmd{};              break;
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
    // Match the in-match backdrop: solid clear in the gradient's top tone,
    // then the screen-space gradient + vignette painted on top before the
    // world camera transform begins. Keeps the intro consistent with the
    // gameplay frame's "framed" look instead of a flat dark rectangle.
    ClearBackground(Color{12, 14, 22, 255});
    renderer_.drawScreenBackdrop(sw, sh);

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
    // Each entry: (start, end, main text, optional small eyebrow above, font
    // size). Alpha fades in/out at the edges of each window so transitions
    // are gentle. Times line up with the gameplay script above:
    //   0.0  - 2.5  : "CELL ROYALE" -- branded title
    //   2.7  - 4.2  : "EAT FOOD TO GROW" -- player drifts through food
    //   4.3  - 6.3  : "SPLIT" -- split fires at 4.0s
    //   6.6  - 8.6  : "DASH" -- dash fires at 6.5s
    //   8.9  - 10.8 : "EJECT MASS" -- eject pellets fire at 9.0 / 9.3s
    //   11.2 - 13.0 : (drift toward bots, no overlay -- breathing room)
    //   13.0 - 14.8 : "MASS BLAST" -- blast fires at 12.8s, overlay confirms
    //   15.0 - 16.8 : "PLAY ROYALE FOR LAN MULTIPLAYER"
    //   17.2 - 19.0 : "GOOD LUCK." -- final beat
    struct Overlay {
        float       start, end;
        const char* main;     // main line (large)
        const char* eyebrow;  // small uppercase eyebrow ABOVE the main line
        const char* sub;      // small subtitle BELOW the main line
        int         size;
        Color       color;
    };
    const Color kAccent = Color{255, 220, 140, 255};   // warm gold
    const Color kCyan   = Color{170, 220, 255, 255};   // multiplayer accent
    const Color kWhite  = Color{240, 240, 245, 255};
    const Overlay overlays[] = {
        // Title -- bigger and with a quiet eyebrow "the agar arena".
        {0.0f,  2.5f,  "CELL ROYALE",        "agar arena",         nullptr,             68, kAccent},
        // Core gameplay loop.
        {2.7f,  4.2f,  "EAT TO GROW",        "objective",          "your cursor leads", 36, kWhite},
        // Movement / abilities (each has its key as a subtitle).
        {4.3f,  6.3f,  "SPLIT",              "ability",            "SPACE",             36, kWhite},
        {6.6f,  8.6f,  "DASH",               "ability",            "SHIFT  --  4s cooldown", 36, kWhite},
        {8.9f,  10.8f, "EJECT MASS",         "ability",            "W  --  feed viruses, push enemies", 32, kWhite},
        {13.0f, 14.8f, "MASS BLAST",         "ability",            "Q  --  6s cooldown, requires 300 mass", 32, kWhite},
        // Multiplayer teaser.
        {15.0f, 16.8f, "BATTLE ON A LAN",    "new!",               "main menu  >  ROYALE  >  LOCAL",    30, kCyan},
        // Closing beat.
        {17.2f, 19.0f, "GOOD LUCK.",         nullptr,              nullptr,             40, kAccent},
    };

    for (const Overlay& o : overlays) {
        if (elapsed_sec_ < o.start || elapsed_sec_ > o.end) continue;
        const float dur     = o.end - o.start;
        const float fade    = std::min(0.35f, dur * 0.30f);
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
        int ty = static_cast<int>(sh * 0.36f);

        // Soft glow halo behind the main line -- breathes alpha based on
        // the overlay's fade-in/out so it builds + decays with the text
        // instead of being a constant blob. Cyan for the MP teaser, warm
        // gold for the others.
        Color halo = (o.color.b > o.color.r)
            ? Color{120, 180, 240, static_cast<unsigned char>(alpha * 70)}
            : Color{255, 200, 110, static_cast<unsigned char>(alpha * 70)};
        DrawCircle(sw / 2, ty + o.size / 2, std::max(140, tw),
                   Color{halo.r, halo.g, halo.b,
                         static_cast<unsigned char>(halo.a * 0.45f)});
        DrawCircle(sw / 2, ty + o.size / 2, std::max(90, tw * 2 / 3), halo);

        // Eyebrow (small uppercase tag above the main line) + flanking
        // accent rules so the text feels "branded" instead of floating.
        if (o.eyebrow) {
            int eb_sz = std::max(11, o.size / 6);
            int eb_w  = MeasureText(o.eyebrow, eb_sz);
            int eb_y  = ty - eb_sz - 18;
            DrawRectangle(sw / 2 - eb_w / 2 - 60, eb_y + eb_sz / 2,
                          50, 1, Color{halo.r, halo.g, halo.b, a});
            DrawRectangle(sw / 2 + eb_w / 2 + 10, eb_y + eb_sz / 2,
                          50, 1, Color{halo.r, halo.g, halo.b, a});
            DrawText(o.eyebrow, sw / 2 - eb_w / 2, eb_y, eb_sz,
                     Color{halo.r, halo.g, halo.b,
                           static_cast<unsigned char>(a * 0.95f)});
        }

        // Main line -- two-pass shadow + body.
        DrawText(o.main, tx + 4, ty + 5, o.size,
                 Color{0, 0, 0, static_cast<unsigned char>(a * 0.45f)});
        DrawText(o.main, tx + 2, ty + 2, o.size, shadow);
        DrawText(o.main, tx,     ty,     o.size, body);

        // Subtitle BELOW the main line.
        if (o.sub) {
            int subsz = std::max(13, o.size / 3);
            int sub_w = MeasureText(o.sub, subsz);
            DrawText(o.sub, sw / 2 - sub_w / 2 + 1,
                     ty + o.size + 14 + 1, subsz,
                     Color{0, 0, 0, static_cast<unsigned char>(a * 0.6f)});
            DrawText(o.sub, sw / 2 - sub_w / 2,
                     ty + o.size + 14, subsz,
                     Color{210, 220, 235,
                           static_cast<unsigned char>(a * 0.92f)});
        }
    }

    // Progress bar at the very bottom -- a thin line that fills over the
    // course of the intro. Gives the player a visual cue for "how much
    // longer" without nagging text.
    {
        const float prog = std::clamp(elapsed_sec_ / kDurationSec, 0.0f, 1.0f);
        const int bar_w = static_cast<int>(sw * 0.50f);
        const int bar_x = (sw - bar_w) / 2;
        const int bar_y = sh - 18;
        DrawRectangle(bar_x, bar_y, bar_w, 2, Color{255, 220, 140, 35});
        DrawRectangleGradientH(bar_x, bar_y - 1,
                               static_cast<int>(bar_w * prog), 4,
                               Color{255, 220, 140, 0},
                               Color{255, 220, 140, 180});
    }

    // "Press any key to skip" hint -- only after the title fades, low key.
    if (elapsed_sec_ > 2.5f && elapsed_sec_ < kDurationSec - kFadeOutDurationSec) {
        const char* hint = "press any key to skip";
        const int   h_size = 14;
        int hint_w = MeasureText(hint, h_size);
        DrawText(hint, sw / 2 - hint_w / 2, sh - 38, h_size,
                 Color{180, 190, 210, 150});
    }
}

} // namespace cr
