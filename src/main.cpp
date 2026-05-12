#include "raylib.h"

#include "client/Client.h"
#include "core/Rng.h"
#include "core/Tuning.h"
#include "platform/Input.h"
#include "sim/Replay.h"
#include "sim/Simulation.h"

#include <cstdio>
#include <cstdlib>
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
        con.log("commands: spawn_food N, set_mass N, god, slowmo F, pause,");
        con.log("          reload_tuning, replay_save FILE, replay_load FILE,");
        con.log("          set_hold_to_move 0|1, set_invert_thumbs 0|1, force_touch 0|1,");
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
        con.log("god mode: not implemented until Phase 4 (no death yet)");
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
    } else {
        con.log("unknown command: " + cmd);
    }
}

int runWindow(uint64_t seed) {
    cr::Tuning tuning;
    if (!cr::LoadTuningFromFile(tuning, "tuning.ini")) {
        std::printf("[cell_royale] tuning.ini not found -- using defaults\n");
    }
    cr::PrintTuning(tuning);

    InitWindow(1280, 720, "Cell Royale v0.2.0");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL); // don't quit on ESC (console uses it)

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

    // Live replay recording starts at session begin and accumulates every queued command.
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

    while (!WindowShouldClose()) {
        int   screen_w  = GetScreenWidth();
        int   screen_h  = GetScreenHeight();
        float frame_dt  = GetFrameTime();

        client.pollFrame(screen_w, screen_h, sim.currentTick());

        // Drain commands queued by Client and forward to sim + replay tape.
        auto cmds = client.takeCommands();
        for (const auto& c : cmds) {
            sim.queueCommand(c);
            if (replay_recording) live_replay.recordCommand(c);
        }

        accumulator += static_cast<double>(frame_dt) * client.effectiveDtMultiplier();
        int safety = 0;
        while (accumulator >= sim_dt && safety < 8) {
            sim.tick(sim_dt);
            client.onSnapshot(sim.buildSnapshot());
            accumulator -= sim_dt;
            ++safety;
        }
        if (accumulator >= sim_dt) {
            // Long pause / breakpoint -- drop the backlog instead of spiraling.
            accumulator = 0.0;
        }

        if (auto* c = sim.world().findCell(player_cell)) {
            client.camera().setTarget(c->pos, c->mass);
        }
        client.camera().update(frame_dt);

        float alpha = static_cast<float>(accumulator / sim_dt);

        BeginDrawing();
        ClearBackground(Color{18, 22, 30, 255});
        client.render(screen_w, screen_h, alpha, tuning);

        // Minimal stats overlay. Phase 6 replaces this with the real HUD.
        char buf[128];
        std::snprintf(buf, sizeof(buf), "FPS %d  Tick %u",
                      GetFPS(), static_cast<unsigned>(sim.currentTick()));
        DrawText(buf, 12, screen_h - 22, 16, Color{200, 200, 200, 220});
        if (auto* c = sim.world().findCell(player_cell)) {
            const char* mode = cr::isUsingTouch() ? "touch" : "desktop";
            std::snprintf(buf, sizeof(buf),
                          "mass %.1f  pos (%.0f, %.0f)  zoom %.2f  dt_x%.2f  %s%s",
                          c->mass, c->pos.x, c->pos.y, client.camera().zoom(),
                          client.dtMultiplier(), mode,
                          client.isPaused() ? "  [PAUSED]" : "");
            DrawText(buf, 12, screen_h - 40, 16, Color{200, 200, 200, 220});
        }
        EndDrawing();
    }

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
