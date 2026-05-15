#include "raylib.h"

#include "client/Client.h"
#include "client/IntroScreen.h"
#include "client/MainMenu.h"
#include "client/Renderer.h"   // setPaletteMode / setHighContrast
#include "client/SettingsScreen.h"
#include "client/UiWidgets.h"  // setHudTextScale
#include "core/Rng.h"
#include "core/Tuning.h"
#include "meta/SaveFile.h"
#include "platform/Input.h"
#include "platform/Paths.h"
#include "sim/Replay.h"
#include "sim/Simulation.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

void printSnapshotStats(const cr::Snapshot& s) {
    std::printf("[tick %5u] %zu cells, %zu food, rng=%016llx",
                static_cast<unsigned>(s.tick),
                s.cells.size(),
                s.food.size(),
                static_cast<unsigned long long>(s.rng_state));
    if (!s.cells.empty()) {
        const auto& c = s.cells.front();
        std::printf("  | cell#%u pos=(%.2f,%.2f) mass=%.1f",
                    static_cast<unsigned>(c.id), c.pos.x, c.pos.y, c.mass);
    }
    std::printf("\n");
}

int runHeadless(uint64_t seed, int total_ticks, const std::string& replay_save_path) {
    cr::Tuning tuning;
    cr::LoadTuningFromFile(tuning, "tuning.ini");

    cr::Simulation sim(seed, tuning);
    const cr::PlayerId player = 1;
    sim.world().spawnCell(player,
                          cr::Vec2{static_cast<float>(tuning.world_width)  * 0.5f,
                                   static_cast<float>(tuning.world_height) * 0.5f},
                          tuning.start_mass);

    cr::Replay                replay;
    std::vector<cr::CellSnap> initial;
    for (const auto& c : sim.world().cells()) {
        initial.push_back(cr::CellSnap{c.id, c.owner, c.pos, c.vel, c.mass});
    }
    replay.recordSetup(seed, tuning.world_width, tuning.world_height, std::move(initial));

    cr::Rng     cmd_rng(seed + 1);
    const float dt = 1.0f / 30.0f;

    std::printf("[cell_royale] headless: seed=%llu, ticks=%d\n",
                static_cast<unsigned long long>(seed), total_ticks);
    printSnapshotStats(sim.buildSnapshot());

    for (int i = 0; i < total_ticks; ++i) {
        if (i % 30 == 0) {
            cr::Command cmd;
            cmd.player  = player;
            cmd.tick    = sim.currentTick();
            cmd.payload = cr::MoveCmd{
                cr::Vec2{cmd_rng.rangeFloat(0.0f, static_cast<float>(tuning.world_width)),
                         cmd_rng.rangeFloat(0.0f, static_cast<float>(tuning.world_height))}};
            sim.queueCommand(cmd);
            replay.recordCommand(cmd);
        }
        sim.tick(dt);
        if ((i + 1) % 1000 == 0) {
            printSnapshotStats(sim.buildSnapshot());
        }
    }

    if (!replay_save_path.empty()) {
        if (replay.saveToFile(replay_save_path)) {
            std::printf("[cell_royale] replay saved: %s (%zu commands)\n",
                        replay_save_path.c_str(), replay.commands().size());
        } else {
            std::printf("[cell_royale] FAILED to save replay: %s\n", replay_save_path.c_str());
            return 1;
        }
    }
    return 0;
}

int runReplayHeadless(const std::string& replay_path) {
    cr::Tuning tuning;
    cr::LoadTuningFromFile(tuning, "tuning.ini");

    cr::Replay replay;
    if (!replay.loadFromFile(replay_path)) {
        std::printf("[cell_royale] FAILED to load replay: %s\n", replay_path.c_str());
        return 1;
    }
    std::printf("[cell_royale] replay loaded: seed=%llu world=%dx%d "
                "initial_cells=%zu commands=%zu\n",
                static_cast<unsigned long long>(replay.seed()),
                replay.worldWidth(), replay.worldHeight(),
                replay.initialCells().size(), replay.commands().size());

    cr::Simulation sim(replay.seed(), tuning);
    for (const auto& c : replay.initialCells()) {
        sim.world().spawnCell(c.owner, c.pos, c.mass);
    }
    for (const auto& cmd : replay.commands()) {
        sim.queueCommand(cmd);
    }

    cr::Tick max_tick = 0;
    for (const auto& cmd : replay.commands()) {
        if (cmd.tick > max_tick) max_tick = cmd.tick;
    }
    int total_ticks = static_cast<int>(max_tick) + 30;

    printSnapshotStats(sim.buildSnapshot());
    const float dt = 1.0f / 30.0f;
    for (int i = 0; i < total_ticks; ++i) {
        sim.tick(dt);
        if ((i + 1) % 1000 == 0) {
            printSnapshotStats(sim.buildSnapshot());
        }
    }
    printSnapshotStats(sim.buildSnapshot());
    return 0;
}

// ---------------------------------------------------------------------------
// Window mode: Fix-Your-Timestep loop + interpolated render + dev console.
// ---------------------------------------------------------------------------

struct WindowState {
    cr::Simulation* sim;
    cr::Client*     client;
    cr::Replay*     live_replay;
    cr::Tuning*     tuning;
    bool*           replay_recording;
    cr::EntityId*   player_cell_id;
    cr::PlayerId    player_id;
};

void runDevCommand(WindowState& s, const std::vector<std::string>& args) {
    auto& con = s.client->console();
    if (args.empty()) return;
    const std::string& cmd = args[0];

    auto needs = [&](size_t n) {
        if (args.size() >= n) return true;
        con.log("usage: " + cmd + " <arg>");
        return false;
    };

    if (cmd == "help") {
        con.log("commands: spawn_food N, set_mass N, god, slowmo F, pause, comet,");
        con.log("          reload_tuning, replay_save FILE, replay_load FILE,");
        con.log("          set_hold_to_move 0|1, set_invert_thumbs 0|1, force_touch 0|1,");
        con.log("          vol_master F, vol_sfx F, vol_music F, music_on, music_off, mute,");
        con.log("          clear, help");
    } else if (cmd == "clear") {
        con.clearOutput();
    } else if (cmd == "spawn_food" && needs(2)) {
        int n = std::atoi(args[1].c_str());
        for (int i = 0; i < n; ++i) {
            cr::Vec2 pos{s.sim->world().rng().rangeFloat(
                             0.0f, static_cast<float>(s.sim->world().width())),
                         s.sim->world().rng().rangeFloat(
                             0.0f, static_cast<float>(s.sim->world().height()))};
            s.sim->world().spawnFood(pos);
        }
        con.log("spawned " + std::to_string(n) + " food");
    } else if (cmd == "set_mass" && needs(2)) {
        float m = static_cast<float>(std::atof(args[1].c_str()));
        if (auto* c = s.sim->world().findCell(*s.player_cell_id)) {
            c->mass = m;
            con.log("mass = " + args[1]);
        } else {
            con.log("no player cell tracked");
        }
    } else if (cmd == "god") {
        if (auto* c = s.sim->world().findCell(*s.player_cell_id)) {
            c->god = !c->god;
            con.log(std::string("god mode = ") + (c->god ? "on" : "off"));
        } else {
            con.log("no player cell to bless");
        }
    } else if (cmd == "slowmo" && needs(2)) {
        float m = static_cast<float>(std::atof(args[1].c_str()));
        if (m <= 0.0f) m = 0.001f;
        s.client->setDtMultiplier(m);
        con.log("dt_mult = " + args[1]);
    } else if (cmd == "reload_tuning") {
        cr::Tuning t;
        if (cr::LoadTuningFromFile(t, "tuning.ini")) {
            *s.tuning = t;
            s.sim->setTuning(t);
            con.log("tuning.ini reloaded");
        } else {
            con.log("failed to reload tuning.ini");
        }
    } else if (cmd == "replay_save" && needs(2)) {
        if (s.live_replay->saveToFile(args[1])) {
            con.log("saved replay: " + args[1] + " (" +
                    std::to_string(s.live_replay->commands().size()) + " commands)");
        } else {
            con.log("failed to save: " + args[1]);
        }
    } else if (cmd == "replay_load" && needs(2)) {
        con.log("replay_load from console not yet wired -- use --replay-load CLI");
    } else if (cmd == "set_hold_to_move" && needs(2)) {
        int v = std::atoi(args[1].c_str());
        s.client->inputConfig().hold_to_move = (v != 0);
        con.log(std::string("hold_to_move = ") + (v ? "on" : "off"));
    } else if (cmd == "set_invert_thumbs" && needs(2)) {
        int v = std::atoi(args[1].c_str());
        s.client->inputConfig().invert_thumbs = (v != 0);
        con.log(std::string("invert_thumbs = ") + (v ? "on" : "off"));
    } else if (cmd == "force_touch" && needs(2)) {
        int v = std::atoi(args[1].c_str());
        cr::setForceTouch(v != 0);
        con.log(std::string("force_touch = ") + (v ? "on" : "off"));
    } else if (cmd == "pause") {
        s.client->togglePause();
        con.log(s.client->isPaused() ? "paused" : "unpaused");
    } else if (cmd == "comet") {
        // Force-spawns a crashing-comet world event on the next sim tick. Useful for
        // demoing the effect without waiting for the regular cadence. Spawn point /
        // direction are still RNG-driven and deterministic.
        s.sim->triggerCometSpawn();
        con.log("comet scheduled for next tick");
    } else if (cmd == "vol_master" && needs(2)) {
        float v = static_cast<float>(std::atof(args[1].c_str()));
        s.client->audio().setMasterVolume(v);
        con.log("vol_master = " + args[1]);
    } else if (cmd == "vol_sfx" && needs(2)) {
        float v = static_cast<float>(std::atof(args[1].c_str()));
        s.client->audio().setSfxVolume(v);
        con.log("vol_sfx = " + args[1]);
    } else if (cmd == "vol_music" && needs(2)) {
        float v = static_cast<float>(std::atof(args[1].c_str()));
        s.client->audio().setMusicVolume(v);
        con.log("vol_music = " + args[1]);
    } else if (cmd == "music_off") {
        s.client->audio().setMusicEnabled(false);
        con.log("music disabled");
    } else if (cmd == "music_on") {
        // Music is off by default in this build (procedural pad sounds buzzy).
        // music_on un-gates the toggle AND explicitly starts playback so console
        // users can audition the pad / a future real track.
        s.client->audio().setMusicEnabled(true);
        s.client->audio().playMusic();
        con.log("music enabled (procedural pad -- buzzy by design)");
    } else if (cmd == "mute") {
        s.client->audio().setMasterVolume(0.0f);
        con.log("muted (use vol_master 1 to restore)");
    } else {
        con.log("unknown command: " + cmd);
    }
}

// Outcome of a single match -- determines whether the outer Menu<->Match loop in
// runWindow goes back to the menu or exits the app.
enum class MatchOutcome {
    ReturnToMenu,   // player clicked MAIN MENU on Summary, or hit a future "leave" button
    WindowClosed,   // user closed the OS window during the match
};

// Runs one match start-to-finish. Builds a fresh sim + client, applies persistent state,
// runs the play loop, and writes updated persistent state back into `save` before
// returning. Designed to be called repeatedly from the menu loop with different seeds.
MatchOutcome runMatch(uint64_t seed, cr::Tuning& tuning, cr::SaveData& save) {
    cr::Simulation     sim(seed, tuning);
    const cr::PlayerId player = 1;
    cr::EntityId       player_cell = sim.world().spawnCell(
        player,
        cr::Vec2{static_cast<float>(tuning.world_width)  * 0.5f,
                 static_cast<float>(tuning.world_height) * 0.5f},
        tuning.start_mass);

    cr::Client client(player_cell, player);
    client.camera().snapTo(
        cr::Vec2{static_cast<float>(tuning.world_width)  * 0.5f,
                 static_cast<float>(tuning.world_height) * 0.5f},
        tuning.start_mass);
    client.onSnapshot(sim.buildSnapshot());

    // Push lifetime stats + settings into the new client so volume/input prefs and
    // best-ever counters are visible from frame 1.
    client.applyLoadedSave(save);

    // Live replay recording starts at match begin.
    cr::Replay                live_replay;
    std::vector<cr::CellSnap> initial;
    for (const auto& c : sim.world().cells()) {
        initial.push_back(cr::CellSnap{c.id, c.owner, c.pos, c.vel, c.mass});
    }
    live_replay.recordSetup(seed, tuning.world_width, tuning.world_height, std::move(initial));
    bool replay_recording = true;

    WindowState state{&sim, &client, &live_replay, &tuning,
                      &replay_recording, &player_cell, player};
    client.console().setHandler(
        [&state](const std::vector<std::string>& args) { runDevCommand(state, args); });
    client.console().log("Cell Royale v0.2.0 -- press ~ for console, 'help' for commands");

    const float    sim_dt        = 1.0f / 30.0f;
    double         accumulator   = 0.0;
    MatchOutcome   outcome       = MatchOutcome::WindowClosed;

    while (!WindowShouldClose()) {
        // Player clicked MAIN MENU on the Summary panel -- end this match cleanly.
        if (client.consumeReturnToMenuRequest()) {
            outcome = MatchOutcome::ReturnToMenu;
            break;
        }

        int   screen_w  = GetScreenWidth();
        int   screen_h  = GetScreenHeight();
        float frame_dt  = GetFrameTime();

        // Auto-pause when window loses focus -- keeps CPU/GPU/audio quiet in the
        // background and stops the sim from accumulating real time the player can't see.
        client.setAutoPaused(!IsWindowFocused());

        client.pollFrame(screen_w, screen_h, sim.currentTick());

        // Drain commands queued by Client and forward to sim + replay tape.
        auto cmds = client.takeCommands();
        for (const auto& c : cmds) {
            sim.queueCommand(c);
            if (replay_recording) live_replay.recordCommand(c);
        }

        // Per-frame feel layer update (shake decay, particles, popups, HUD, death cam).
        client.updateFrame(frame_dt, GetTime(), tuning);

        // Sim ticks -- skipped entirely while hitstop is freezing time.
        if (!client.isHitstopActive()) {
            accumulator += static_cast<double>(frame_dt) * client.effectiveDtMultiplier();
            int safety = 0;
            while (accumulator >= sim_dt && safety < 8) {
                sim.tick(sim_dt);
                client.onSnapshot(sim.buildSnapshot());
                client.onEvents(sim.takeEvents(), sim.world(), tuning);
                accumulator -= sim_dt;
                ++safety;
            }
            if (accumulator >= sim_dt) {
                // Long pause / breakpoint -- drop the backlog instead of spiraling.
                accumulator = 0.0;
            }
        }

        // Phase 8 respawn: when Client signals the player should come back, pick a clear
        // spot and spawn a fresh cell. Bots forget the player's previous peak mass so the
        // world doesn't keep dropping elite scaled threats while the player is at start_mass.
        if (client.consumeRespawnRequest()) {
            cr::Vec2 spawn{0.0f, 0.0f};
            const float margin = 500.0f;
            for (int attempt = 0; attempt < 8; ++attempt) {
                cr::Vec2 candidate{
                    sim.world().rng().rangeFloat(margin,
                                                 static_cast<float>(sim.world().width())  - margin),
                    sim.world().rng().rangeFloat(margin,
                                                 static_cast<float>(sim.world().height()) - margin),
                };
                bool clear = true;
                for (const auto& c : sim.world().cells()) {
                    if (cr::distance(c.pos, candidate)
                        < cr::cellRadius(c.mass) + 400.0f) {
                        clear = false; break;
                    }
                }
                if (clear) { spawn = candidate; break; }
                spawn = candidate; // fallback to last attempt
            }
            player_cell = sim.world().spawnCell(player, spawn, tuning.start_mass);
            client.setWatchedCell(player_cell);
            client.camera().snapTo(spawn, tuning.start_mass);
            client.onPlayerRespawned(GetTime());
            sim.director().resetPlayerTracking();
        }

        // Camera: death cam steals the camera target while it's active. Otherwise follow
        // the watched cell, retargeting to the player's largest piece if it died.
        if (client.deathCamActive()) {
            // For player-vs-player deaths the target is the killer cell. For comet
            // kills it's the comet's entity id; fall back to that if no cell matches.
            const cr::EntityId tgt = client.deathCamTarget();
            if (auto* killer = sim.world().findCell(tgt)) {
                client.camera().setTarget(killer->pos, killer->mass);
            } else if (auto* comet = sim.world().findComet(tgt)) {
                client.camera().setTarget(comet->pos, comet->radius * comet->radius / 9.0f);
            }
        } else {
            cr::Cell* watched = sim.world().findCell(player_cell);
            if (!watched) {
                cr::EntityId best  = cr::INVALID_ENTITY;
                float    best_mass = 0.0f;
                for (const auto& c : sim.world().cells()) {
                    if (c.owner == player && c.mass > best_mass) {
                        best_mass = c.mass;
                        best      = c.id;
                    }
                }
                if (best != cr::INVALID_ENTITY) {
                    player_cell = best;
                    client.setWatchedCell(best);
                    watched = sim.world().findCell(player_cell);
                }
            }
            if (watched) {
                client.camera().setTarget(watched->pos, watched->mass);
            }
        }
        client.camera().update(frame_dt);

        // During hitstop we render the last interpolated frame at alpha=1 so the frozen
        // state reads as "the impact is registering" rather than as choppy motion.
        float alpha = client.isHitstopActive()
                          ? 1.0f
                          : static_cast<float>(accumulator / sim_dt);

        BeginDrawing();
        ClearBackground(Color{18, 22, 30, 255});
        client.render(screen_w, screen_h, alpha, tuning, sim.world(), sim.currentTick());
        EndDrawing();
    }

    // Pull updated progression + lifetime stats + settings out of the client so the
    // outer loop has fresh data for the menu display and the on-disk save.
    save = client.snapshotForSave();
    return outcome;
}

// Top-level desktop loop: open the window once, then alternate between Main Menu and
// matches until the player quits. Save state lives at this level and is written once on
// exit (with a fresh snapshot from the most recently completed match).
int runWindow(uint64_t initial_seed) {
    cr::Tuning tuning;
    if (!cr::LoadTuningFromFile(tuning, "tuning.ini")) {
        std::printf("[cell_royale] tuning.ini not found -- using defaults\n");
    }
    cr::PrintTuning(tuning);

    // Ask raylib for a real-pixel framebuffer (Retina / HiDPI). On macOS raylib's
    // Apple branch handles everything transparently: GetScreenWidth/GetMousePosition
    // still return logical coords (1280-point space), but raylib's ortho projection
    // is set up so drawing in those logical coords rasterises into the native pixel
    // framebuffer (2560x1440 on 2x Retina). Result: crisp text/UI with zero extra
    // code. Non-Apple HiDPI may need explicit scaling -- revisit when we ship there.
    SetConfigFlags(FLAG_WINDOW_HIGHDPI);
    InitWindow(1280, 720, "Cell Royale v0.2.0");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL); // don't quit on ESC (menu / console handle it)

    // Save lives in the platform-correct user data dir so we don't pollute the install
    // directory and so it persists across reinstalls. macOS: ~/Library/Application Support/
    // CellRoyale; Linux: $XDG_DATA_HOME/cell_royale; Windows: %APPDATA%/CellRoyale.
    const std::string kSavePath = cr::userDataPath("save.bin");
    cr::SaveData save{};
    if (cr::loadFromFile(save, kSavePath)) {
        std::printf("[cell_royale] loaded save (%s): lvl %u, %u xp, %u games\n",
                    kSavePath.c_str(),
                    save.level, save.total_xp, save.games_played);
    } else {
        std::printf("[cell_royale] no save found at %s -- starting fresh\n",
                    kSavePath.c_str());
    }

    // Apply persisted FPS cap. SetTargetFPS(0) means uncapped per raylib.
    if (save.fps_cap == 0) {
        SetTargetFPS(0);
    } else {
        SetTargetFPS(static_cast<int>(save.fps_cap));
    }

    // Daily missions: roll a fresh set if we've crossed midnight (or this is a brand
    // new save with no missions yet). Completed flags clear with the new roll.
    const uint32_t today = cr::currentDay();
    if (save.last_mission_reset_day != today
        || save.daily_missions[0].kind == cr::MissionKind::None) {
        cr::rollDailyMissions(today, save.daily_missions);
        save.last_mission_reset_day = today;
        std::printf("[cell_royale] daily missions rolled for day %u\n",
                    static_cast<unsigned>(today));
    }

    cr::MainMenu       menu;
    cr::SettingsScreen settings;
    // Push the loaded accessibility state into the renderer-side globals so the
    // menu's bg cells (and the first match before applyLoadedSave runs) use them.
    cr::setPaletteMode(static_cast<cr::PaletteMode>(
        save.colorblind_mode <= 3 ? save.colorblind_mode : 0));
    cr::setHighContrast(save.high_contrast);
    cr::setHudTextScale(save.hud_text_scale);

    enum class AppPhase { Intro, Menu, Settings, Match, Quit };
    // First-run intro plays before the menu if this is a brand-new save (or a v1/v2/v3
    // save being migrated to v4 -- they all default first_run_complete to false on load
    // since the field didn't exist in their format).
    AppPhase phase = save.first_run_complete ? AppPhase::Menu : AppPhase::Intro;
    // The intro is constructed lazily (only if we actually need it) so non-first-run
    // launches don't pay the InitWindow-time sim spin-up cost.
    std::unique_ptr<cr::IntroScreen> intro;
    if (phase == AppPhase::Intro) {
        intro = std::make_unique<cr::IntroScreen>(tuning);
        std::printf("[cell_royale] first-run intro -- press any key to skip\n");
    }

    uint64_t next_match_seed = initial_seed;

    while (phase != AppPhase::Quit && !WindowShouldClose()) {
        if (phase == AppPhase::Intro) {
            float dt = GetFrameTime();
            bool  intro_done = intro->update(dt);
            BeginDrawing();
            intro->render(GetScreenWidth(), GetScreenHeight());
            EndDrawing();
            if (intro_done) {
                phase = AppPhase::Menu;
                save.first_run_complete = true;
                intro.reset();
                cr::swallowNextClick(); // mouse may have released over a menu button rect
            }
        } else if (phase == AppPhase::Menu) {
            float dt = GetFrameTime();
            int   sw = GetScreenWidth();
            int   sh = GetScreenHeight();
            menu.update(dt, sw, sh);

            BeginDrawing();
            ClearBackground(Color{18, 22, 30, 255});
            cr::MenuAction action = menu.render(sw, sh, save);
            EndDrawing();

            if (action == cr::MenuAction::Quit) {
                phase = AppPhase::Quit;
            } else if (action == cr::MenuAction::StartVsAI) {
                phase = AppPhase::Match;
                cr::swallowNextClick();
            } else if (action == cr::MenuAction::ShowSettings) {
                phase = AppPhase::Settings;
                cr::swallowNextClick();
            } else if (action == cr::MenuAction::ReplayIntro) {
                // User asked to rewatch the tutorial. Spin up a fresh IntroScreen and
                // bounce back to Menu when it ends -- don't touch first_run_complete.
                intro = std::make_unique<cr::IntroScreen>(tuning);
                phase = AppPhase::Intro;
                cr::swallowNextClick();
            }
            // StartRoyalePlaceholder: menu itself shows the toast, we stay in Menu.
        } else if (phase == AppPhase::Settings) {
            int sw = GetScreenWidth();
            int sh = GetScreenHeight();

            BeginDrawing();
            cr::SettingsAction sa = settings.render(sw, sh, save);
            EndDrawing();

            if (sa == cr::SettingsAction::Quit) {
                phase = AppPhase::Quit;
            } else if (sa == cr::SettingsAction::BackToMenu) {
                phase = AppPhase::Menu;
                cr::swallowNextClick();
            }
        } else { // Match
            MatchOutcome outcome = runMatch(next_match_seed, tuning, save);
            ++next_match_seed; // new seed per match for variety across the session
            phase = (outcome == MatchOutcome::ReturnToMenu)
                        ? AppPhase::Menu
                        : AppPhase::Quit;
            if (phase == AppPhase::Menu) {
                // The exit from match was triggered by a MAIN MENU button click. The
                // mouse-release event is still "fresh" for raylib this frame; swallow
                // it so it doesn't leak into the menu's button under the cursor.
                cr::swallowNextClick();
            }
        }
    }

    // Persist progression + settings on exit. Best-effort: if it fails we log and
    // continue; the previous .bak is still on disk.
    if (cr::saveToFile(save, kSavePath)) {
        std::printf("[cell_royale] save written to %s\n", kSavePath.c_str());
    } else {
        std::printf("[cell_royale] WARNING: failed to write save to %s\n",
                    kSavePath.c_str());
    }

    // Free renderer-owned GPU resources (lazy-loaded black-hole shader + 1x1 texture)
    // BEFORE CloseWindow destroys the GL context, otherwise we leak driver-side handles
    // and on some drivers raylib logs "delete after context destroyed" warnings.
    cr::unloadRendererGpuResources();
    CloseWindow();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    bool        headless    = false;
    int         total_ticks = 5000;
    uint64_t    seed        = 42;
    std::string replay_save;
    std::string replay_load;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--ticks" && i + 1 < argc) {
            total_ticks = std::atoi(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--replay-save" && i + 1 < argc) {
            replay_save = argv[++i];
        } else if (arg == "--replay-load" && i + 1 < argc) {
            replay_load = argv[++i];
        }
    }

    if (!replay_load.empty()) return runReplayHeadless(replay_load);
    if (headless)             return runHeadless(seed, total_ticks, replay_save);
    return runWindow(seed);
}
