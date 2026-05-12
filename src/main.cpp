#include "raylib.h"

#include "core/Rng.h"
#include "core/Tuning.h"
#include "sim/Replay.h"
#include "sim/Simulation.h"

#include <cstdio>
#include <cstdlib>
#include <string>
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

    cr::Replay                 replay;
    std::vector<cr::CellSnap>  initial;
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

int runReplay(const std::string& replay_path) {
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

    if (!replay_load.empty()) {
        return runReplay(replay_load);
    }
    if (headless) {
        return runHeadless(seed, total_ticks, replay_save);
    }

    // Default: window mode. The render path lands in Phase 2.
    cr::Tuning tuning;
    if (cr::LoadTuningFromFile(tuning, "tuning.ini")) {
        std::printf("[cell_royale] Loaded tuning.ini\n");
    } else {
        std::printf("[cell_royale] Failed to load tuning.ini -- using defaults\n");
    }
    cr::PrintTuning(tuning);

    InitWindow(1280, 720, "Cell Royale v0.1.0");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(Color{20, 24, 32, 255});
        DrawText("Cell Royale v0.1.0", 20, 20, 24, RAYWHITE);
        DrawText("Run with --headless to drive the sim from the console.",
                 20, 56, 16, GRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
