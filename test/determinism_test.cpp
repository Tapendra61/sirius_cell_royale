// Phase 1 DoD: "run sim with seed 42 + recorded commands twice, snapshots at tick 1000
// must be byte-identical."
//
// Exit code 0 on pass, 1 on fail. Output is human-readable but the script is the same:
// run sim A -> capture commands + snapshot. Run sim B with same seed + captured commands.
// Compare snapshots at every checkpoint.

#include "core/Rng.h"
#include "core/Snapshot.h"
#include "core/Tuning.h"
#include "sim/Replay.h"
#include "sim/Simulation.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace cr;

namespace {

constexpr uint64_t kSeed       = 42;
constexpr uint64_t kCmdSeed    = 43;
constexpr int      kTotalTicks = 1500;
constexpr float    kDt         = 1.0f / 30.0f;
constexpr int      kCheckpoint = 1000;
constexpr PlayerId kPlayer     = 1;

// Re-seed cells the same way every time so the world is reproducible without
// committing to a "scenario file" yet.
void seedPlayerCells(Simulation& sim) {
    const auto& t = sim.tuning();
    sim.world().spawnCell(kPlayer,
                          Vec2{static_cast<float>(t.world_width) * 0.5f,
                               static_cast<float>(t.world_height) * 0.5f},
                          t.start_mass);
}

int reportMismatch(const Snapshot& a, const Snapshot& b) {
    std::printf("[determinism_test] FAIL: snapshots differ at tick %u\n", a.tick);
    std::printf("  sim A: %zu cells, %zu food, rng=%016llx\n",
                a.cells.size(), a.food.size(),
                static_cast<unsigned long long>(a.rng_state));
    std::printf("  sim B: %zu cells, %zu food, rng=%016llx\n",
                b.cells.size(), b.food.size(),
                static_cast<unsigned long long>(b.rng_state));
    if (a.cells.size() == b.cells.size()) {
        for (size_t i = 0; i < a.cells.size(); ++i) {
            if (!(a.cells[i] == b.cells[i])) {
                std::printf("  cell[%zu] diverged: A pos=(%.6f,%.6f) m=%.6f vs "
                            "B pos=(%.6f,%.6f) m=%.6f\n",
                            i, a.cells[i].pos.x, a.cells[i].pos.y, a.cells[i].mass,
                            b.cells[i].pos.x, b.cells[i].pos.y, b.cells[i].mass);
                break;
            }
        }
    }
    return 1;
}

} // namespace

int main() {
    Tuning tuning; // defaults match tuning.ini Section 8 starting values

    // --- Sim A: record commands ---
    Simulation simA(kSeed, tuning);
    seedPlayerCells(simA);

    Rng              cmd_rng(kCmdSeed);
    std::vector<Command> recorded;
    Snapshot         snapA_at_checkpoint;

    for (int i = 0; i < kTotalTicks; ++i) {
        if (i % 30 == 0) {
            Command cmd;
            cmd.player  = kPlayer;
            cmd.tick    = simA.currentTick();
            cmd.payload = MoveCmd{
                Vec2{cmd_rng.rangeFloat(0.0f, static_cast<float>(tuning.world_width)),
                     cmd_rng.rangeFloat(0.0f, static_cast<float>(tuning.world_height))}};
            simA.queueCommand(cmd);
            recorded.push_back(cmd);
        }
        simA.tick(kDt);
        if (simA.currentTick() == kCheckpoint) {
            snapA_at_checkpoint = simA.buildSnapshot();
        }
    }
    Snapshot snapA_final = simA.buildSnapshot();

    // --- Sim B: replay the same commands ---
    Simulation simB(kSeed, tuning);
    seedPlayerCells(simB);
    for (const auto& cmd : recorded) simB.queueCommand(cmd);

    Snapshot snapB_at_checkpoint;
    for (int i = 0; i < kTotalTicks; ++i) {
        simB.tick(kDt);
        if (simB.currentTick() == kCheckpoint) {
            snapB_at_checkpoint = simB.buildSnapshot();
        }
    }
    Snapshot snapB_final = simB.buildSnapshot();

    // --- Compare ---
    std::printf("[determinism_test] sim A tick %u: %zu cells, %zu food, rng=%016llx\n",
                snapA_at_checkpoint.tick, snapA_at_checkpoint.cells.size(),
                snapA_at_checkpoint.food.size(),
                static_cast<unsigned long long>(snapA_at_checkpoint.rng_state));

    if (!(snapA_at_checkpoint == snapB_at_checkpoint)) {
        return reportMismatch(snapA_at_checkpoint, snapB_at_checkpoint);
    }
    if (!(snapA_final == snapB_final)) {
        return reportMismatch(snapA_final, snapB_final);
    }

    // --- Bonus: round-trip through replay file ---
    Replay replay;
    std::vector<CellSnap> initial;
    initial.push_back(CellSnap{
        1, kPlayer,
        Vec2{static_cast<float>(tuning.world_width) * 0.5f,
             static_cast<float>(tuning.world_height) * 0.5f},
        Vec2{0.0f, 0.0f}, tuning.start_mass});
    replay.recordSetup(kSeed, tuning.world_width, tuning.world_height, initial);
    for (const auto& c : recorded) replay.recordCommand(c);

    const char* tmp_path = "determinism_test.rpl";
    if (!replay.saveToFile(tmp_path)) {
        std::printf("[determinism_test] FAIL: could not save replay\n");
        return 1;
    }

    Replay loaded;
    if (!loaded.loadFromFile(tmp_path)) {
        std::printf("[determinism_test] FAIL: could not load replay\n");
        std::remove(tmp_path);
        return 1;
    }
    std::remove(tmp_path);

    if (loaded.seed() != kSeed
        || loaded.commands().size() != recorded.size()
        || loaded.initialCells().size() != initial.size()) {
        std::printf("[determinism_test] FAIL: replay round-trip mismatch\n");
        return 1;
    }
    for (size_t i = 0; i < recorded.size(); ++i) {
        if (!(loaded.commands()[i] == recorded[i])) {
            std::printf("[determinism_test] FAIL: replay command %zu differs after round-trip\n", i);
            return 1;
        }
    }

    std::printf("[determinism_test] PASS: %d ticks, %zu commands, snapshots bit-identical "
                "at tick %d and tick %d; replay round-trip ok\n",
                kTotalTicks, recorded.size(), kCheckpoint, kTotalTicks);
    return 0;
}
