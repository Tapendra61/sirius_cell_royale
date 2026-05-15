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
#include "transport/Codec.h"

#include <cstdint>
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

    // --- Codec round-trip: Snapshot, Command, GameEvent ---
    // Verifies that the byte-buffer codec used by the network transport produces
    // bit-identical objects after encode + decode. If this fails the LAN multiplayer
    // path can't be trusted to deliver the same state the host computed locally.
    {
        std::vector<uint8_t> buf;

        // Snapshot: use the checkpoint snapshot since it has cells, food, viruses,
        // pickups, and (depending on cadence) black holes -- maximum coverage.
        if (!cr::codec::encodeSnapshot(snapA_at_checkpoint, buf)) {
            std::printf("[determinism_test] FAIL: codec::encodeSnapshot returned false\n");
            return 1;
        }
        Snapshot decoded_snap;
        if (!cr::codec::decodeSnapshot(buf.data(), buf.size(), decoded_snap)) {
            std::printf("[determinism_test] FAIL: codec::decodeSnapshot returned false\n");
            return 1;
        }
        if (!(decoded_snap == snapA_at_checkpoint)) {
            std::printf("[determinism_test] FAIL: snapshot codec round-trip differs\n");
            std::printf("  encoded %zu bytes; original %zu cells, decoded %zu cells\n",
                        buf.size(), snapA_at_checkpoint.cells.size(),
                        decoded_snap.cells.size());
            return 1;
        }

        // Command: round-trip each recorded MoveCmd plus one synthesized
        // Split/Eject/Dash/Blast so all five variants are exercised.
        std::vector<Command> cmd_set = recorded;
        auto pushBare = [&](CommandPayload p) {
            Command c;
            c.player = kPlayer;
            c.tick   = 0;
            c.payload = std::move(p);
            cmd_set.push_back(std::move(c));
        };
        pushBare(SplitCmd{});
        pushBare(EjectCmd{});
        pushBare(DashCmd{});
        pushBare(BlastCmd{});
        for (const auto& c : cmd_set) {
            buf.clear();
            if (!cr::codec::encodeCommand(c, buf)) {
                std::printf("[determinism_test] FAIL: codec::encodeCommand returned false\n");
                return 1;
            }
            Command decoded_cmd;
            if (!cr::codec::decodeCommand(buf.data(), buf.size(), decoded_cmd)) {
                std::printf("[determinism_test] FAIL: codec::decodeCommand returned false\n");
                return 1;
            }
            if (!(decoded_cmd == c)) {
                std::printf("[determinism_test] FAIL: command codec round-trip differs\n");
                return 1;
            }
        }

        // Event: construct one of each variant by hand and round-trip. We don't use
        // sim-produced events here because the sim doesn't always emit every variant
        // within 1500 ticks of the test scenario.
        std::vector<GameEvent> event_set = {
            GameEvent{AbsorbEvent{1, 2, Vec2{100.0f, 200.0f}, 12.5f}},
            GameEvent{DeathEvent{kPlayer, 99, 7, /*prey_personality=*/3,
                                  /*predator_personality=*/0}},
            GameEvent{SplitEvent{kPlayer, 5, 6}},
            GameEvent{CritEvent{4, Vec2{50.0f, 60.0f}, 28.0f}},
            GameEvent{NearMissEvent{8, 9, Vec2{-30.0f, 0.0f}}},
            GameEvent{PickupCollectedEvent{10, kPlayer, PickupKind::Shield,
                                            Vec2{500.0f, 500.0f}}},
            GameEvent{BlastEvent{11, kPlayer, Vec2{1000.0f, 1000.0f}, 700.0f}},
            GameEvent{CometEvent{12, CometEvent::Active,
                                  Vec2{2500.0f, -100.0f}, Vec2{0.0f, 1.0f}}},
        };
        for (const auto& ev : event_set) {
            buf.clear();
            if (!cr::codec::encodeEvent(ev, buf)) {
                std::printf("[determinism_test] FAIL: codec::encodeEvent returned false "
                            "for variant %zu\n", ev.index());
                return 1;
            }
            GameEvent decoded_ev;
            if (!cr::codec::decodeEvent(buf.data(), buf.size(), decoded_ev)) {
                std::printf("[determinism_test] FAIL: codec::decodeEvent returned false "
                            "for variant %zu\n", ev.index());
                return 1;
            }
            // GameEvent doesn't currently have a `= default` operator== on every
            // alternative, so we round-trip the decoded value through encodeEvent
            // again and compare bytes. Byte equality is strictly stronger than
            // struct equality for this codec (it would catch padding-only drift too).
            std::vector<uint8_t> buf2;
            if (!cr::codec::encodeEvent(decoded_ev, buf2)) {
                std::printf("[determinism_test] FAIL: re-encode after decode returned "
                            "false for variant %zu\n", ev.index());
                return 1;
            }
            if (buf != buf2) {
                std::printf("[determinism_test] FAIL: event codec round-trip differs "
                            "(variant %zu)\n", ev.index());
                return 1;
            }
        }
    }

    std::printf("[determinism_test] PASS: %d ticks, %zu commands, snapshots bit-identical "
                "at tick %d and tick %d; replay round-trip ok; codec round-trip ok\n",
                kTotalTicks, recorded.size(), kCheckpoint, kTotalTicks);
    return 0;
}
