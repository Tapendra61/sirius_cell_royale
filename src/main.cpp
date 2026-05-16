#include "raylib.h"

#include "ai/BotPersonality.h"  // kFirstBotPlayerId
#include "client/Client.h"
#include "client/IntroScreen.h"
#include "client/LocalLobby.h"
#include "client/MainMenu.h"
#include "client/Renderer.h"   // setPaletteMode / setHighContrast
#include "client/RoyaleMenu.h"
#include "client/SettingsScreen.h"
#include "client/Hud.h"        // cr::GamePhase
#include "client/UiWidgets.h"  // setHudTextScale
#include "core/Rng.h"
#include "core/Tuning.h"
#include "meta/SaveFile.h"
#include "platform/Input.h"
#include "platform/Paths.h"
#include "sim/Replay.h"
#include "sim/Rules.h"           // rollFoodMass (used by seed_food dev command)
#include "sim/Simulation.h"
#include "transport/Codec.h"
#include "transport/LocalDiscovery.h"
#include "transport/NetworkTransport.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <unordered_map>
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

// Which transport / sim ownership model the current match is running under. The
// dev console gates some commands by this (host-only mutations: spawn/kill,
// reset-bots, etc. are no-ops or refused on LocalClient since the sim there is
// just a passive renderer).
enum class MatchMode {
    SinglePlayer,
    LocalHost,
    LocalClient,
};

struct WindowState {
    cr::Simulation*       sim;
    cr::Client*           client;
    cr::Replay*           live_replay;
    cr::Tuning*           tuning;
    bool*                 replay_recording;
    cr::EntityId*         player_cell_id;
    cr::PlayerId          player_id;
    MatchMode             mode = MatchMode::SinglePlayer;
    // LocalHost-only: the network transport + the peer->player map so the
    // dev console can issue host actions like `kick PID`. Null on SinglePlayer
    // / LocalClient.
    cr::NetworkTransport* net_transport   = nullptr;
    std::unordered_map<void*, cr::PlayerId>* peer_to_player = nullptr;
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
        con.log("HOST-ONLY commands (refused on a multiplayer client):");
        con.log("  bots N            set bot target count (0 = clear all)");
        con.log("  tp X Y            teleport your cells to (X, Y)");
        con.log("  set_mass N        set the watched cell's mass");
        con.log("  god               toggle invuln on the watched cell");
        con.log("  comet             spawn a crashing-comet event now");
        con.log("  shower            spawn a comet-shower event now (main + 3..7 satellites)");
        con.log("  spawn_food N      drop N random food (food_target+=N)");
        con.log("  seed_food N [M]   drop N food, optional mass tier M (1,3,6,12,36)");
        con.log("  kick PID          (LocalHost only) disconnect peer + despawn their cells");
        con.log("CLIENT-FRIENDLY commands (work in any mode):");
        con.log("  slowmo F          dt multiplier (1=normal, 0.25=slowmo)");
        con.log("  pause             toggle local pause");
        con.log("  reload_tuning     re-read tuning.ini");
        con.log("  replay_save FILE  write the running replay tape to disk");
        con.log("  set_hold_to_move 0|1   move-on-mouse-held vs always");
        con.log("  set_invert_thumbs 0|1  swap left/right virtual sticks (touch)");
        con.log("  force_touch 0|1   force touch UI on desktop builds");
        con.log("  vol_master F      master volume (0..1)");
        con.log("  vol_sfx    F      sfx volume (0..1)");
        con.log("  vol_music  F      music volume (0..1)");
        con.log("  music_on / music_off / mute");
        con.log("  clear / help");
    } else if (cmd == "clear") {
        con.clearOutput();
    } else if (cmd == "spawn_food" && needs(2)) {
        if (s.mode == MatchMode::LocalClient) {
            con.log("spawn_food: host-only (client doesn't own the sim)");
        } else {
            int n = std::atoi(args[1].c_str());
            for (int i = 0; i < n; ++i) {
                cr::Vec2 pos{s.sim->world().rng().rangeFloat(
                                 0.0f, static_cast<float>(s.sim->world().width())),
                             s.sim->world().rng().rangeFloat(
                                 0.0f, static_cast<float>(s.sim->world().height()))};
                s.sim->world().spawnFood(pos);
            }
            con.log("spawned " + std::to_string(n) + " food");
        }
    } else if (cmd == "set_mass" && needs(2)) {
        // Host-only: mutates the authoritative sim. LocalClient's sim is empty so
        // a no-op there would be silently confusing; refuse it loudly instead.
        if (s.mode == MatchMode::LocalClient) {
            con.log("set_mass: host-only (client doesn't own the sim)");
        } else {
            float m = static_cast<float>(std::atof(args[1].c_str()));
            if (auto* c = s.sim->world().findCell(*s.player_cell_id)) {
                c->mass = m;
                con.log("mass = " + args[1]);
            } else {
                con.log("no player cell tracked");
            }
        }
    } else if (cmd == "god") {
        if (s.mode == MatchMode::LocalClient) {
            con.log("god: host-only (client doesn't own the sim)");
        } else if (auto* c = s.sim->world().findCell(*s.player_cell_id)) {
            c->god = !c->god;
            con.log(std::string("god mode = ") + (c->god ? "on" : "off"));
        } else {
            con.log("no player cell to bless");
        }
    } else if (cmd == "tp" && needs(3)) {
        // Host-only: teleport every cell the local player owns to (X, Y). Useful
        // for jumping next to a black hole / comet / specific bot for testing.
        if (s.mode == MatchMode::LocalClient) {
            con.log("tp: host-only (client doesn't own the sim)");
        } else {
            float x = static_cast<float>(std::atof(args[1].c_str()));
            float y = static_cast<float>(std::atof(args[2].c_str()));
            // Clamp into the playfield so a typo doesn't fling cells into the void
            // (the soft bounds would catch it, but explicit is friendlier).
            x = std::clamp(x, 0.0f, static_cast<float>(s.sim->world().width()));
            y = std::clamp(y, 0.0f, static_cast<float>(s.sim->world().height()));
            int moved = 0;
            for (auto& c : s.sim->world().cellsMut()) {
                if (c.owner != s.player_id) continue;
                c.pos        = cr::Vec2{x, y};
                c.vel        = cr::Vec2{0.0f, 0.0f};
                c.launch_vel = cr::Vec2{0.0f, 0.0f};
                c.target     = cr::Vec2{x, y};
                ++moved;
            }
            con.log("tp -> (" + std::to_string(static_cast<int>(x)) + ", "
                  + std::to_string(static_cast<int>(y)) + ")  ("
                  + std::to_string(moved) + " cells)");
        }
    } else if (cmd == "seed_food" && needs(2)) {
        // Host-only: drop N food at random positions. Optional second arg is the
        // mass tier (1, 3, 6, 12, or 36). Defaults to a tiered roll so the drop
        // mix matches the natural food distribution.
        if (s.mode == MatchMode::LocalClient) {
            con.log("seed_food: host-only (client doesn't own the sim)");
        } else {
            int   n    = std::atoi(args[1].c_str());
            float mass = (args.size() >= 3)
                            ? static_cast<float>(std::atof(args[2].c_str()))
                            : 0.0f; // 0 -> roll the tier per food
            if (n < 0) n = 0;
            // Cap the request so a stray typo (`seed_food 1000000`) doesn't
            // blow up the food vector. 5000 is well above food_target so any
            // sensible request fits.
            n = std::min(n, 5000);
            for (int i = 0; i < n; ++i) {
                cr::Vec2 pos{
                    s.sim->world().rng().rangeFloat(
                        0.0f, static_cast<float>(s.sim->world().width())),
                    s.sim->world().rng().rangeFloat(
                        0.0f, static_cast<float>(s.sim->world().height())),
                };
                float m = (mass > 0.0f) ? mass
                                        : cr::rules::rollFoodMass(s.sim->world().rng());
                s.sim->world().spawnFood(pos, m, cr::Vec2{0.0f, 0.0f},
                                         cr::INVALID_PLAYER);
            }
            char buf[96];
            if (mass > 0.0f) {
                std::snprintf(buf, sizeof(buf), "seeded %d food (mass %.0f)", n, mass);
            } else {
                std::snprintf(buf, sizeof(buf), "seeded %d food (mixed tiers)", n);
            }
            con.log(buf);
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
        // In SP this freezes the sim AND shows the pause overlay. In MP it
        // only toggles the overlay (effectiveDtMultiplier keeps the sim
        // ticking) -- handy as a "give me the menu without reaching for Esc"
        // command via the dev console.
        s.client->togglePause();
        con.log(s.client->isPaused() ? "menu open" : "menu closed");
    } else if (cmd == "comet") {
        // Force-spawns a crashing-comet world event on the next sim tick. Useful for
        // demoing the effect without waiting for the regular cadence. Spawn point /
        // direction are still RNG-driven and deterministic. Host-only.
        if (s.mode == MatchMode::LocalClient) {
            con.log("comet: host-only (client doesn't own the sim)");
        } else {
            s.sim->triggerCometSpawn();
            con.log("comet scheduled for next tick");
        }
    } else if (cmd == "shower") {
        // Force-spawns a comet-shower world event on the next sim tick: 1 main
        // (Orange) + 3..7 satellites (Red / Blue). Same RNG-driven path + scatter
        // as the regular cadence. Host-only.
        if (s.mode == MatchMode::LocalClient) {
            con.log("shower: host-only (client doesn't own the sim)");
        } else {
            s.sim->triggerCometShower();
            con.log("comet shower scheduled for next tick");
        }
    } else if (cmd == "kick" && needs(2)) {
        // Host-only: forcibly disconnect the peer that owns the given PlayerId.
        // The departed-peer cleanup path then despawns their cells. Refuses for
        // PlayerId=1 (the host's own slot) and for any PID with no matching peer
        // (e.g. someone tried to kick a bot or themselves).
        if (s.mode != MatchMode::LocalHost) {
            con.log("kick: host-only (only meaningful in LocalHost mode)");
        } else {
            int pid_in = std::atoi(args[1].c_str());
            if (pid_in <= 1) {
                con.log("kick: refuse to kick player " + args[1]
                      + " (1 is the host; bots aren't peers)");
            } else {
                cr::PlayerId target = static_cast<cr::PlayerId>(pid_in);
                cr::NetworkTransport::PeerHandle to_kick{};
                if (s.peer_to_player) {
                    for (const auto& [peer, pid] : *s.peer_to_player) {
                        if (pid == target) {
                            to_kick.enet_peer = peer;
                            break;
                        }
                    }
                }
                if (!to_kick.isValid()) {
                    con.log("kick: no peer with player_id=" + args[1]);
                } else {
                    s.net_transport->disconnectPeer(to_kick);
                    con.log("kick: requested disconnect for player_id=" + args[1]
                          + " (cells will despawn when ENet confirms)");
                }
            }
        }
    } else if (cmd == "bots" && needs(2)) {
        // Host-only: set the bot target count + immediately despawn excess bot
        // cells so the change reads on screen instantly. `bots 0` clears the world
        // of AI -- useful for testing multiplayer or just having a calm sandbox.
        // `bots 50` brings the swarm back to full strength. Clients can't run this
        // because they don't own the authoritative sim.
        if (s.mode == MatchMode::LocalClient) {
            con.log("bots: host-only command (client can't mutate the world)");
        } else {
            int n = std::atoi(args[1].c_str());
            if (n < 0) n = 0;
            // Update both the menu-owned Tuning and the Simulation's internal copy
            // (the Sim holds its own Tuning by value, mirrored at construction).
            s.tuning->bot_target_count = n;
            cr::Tuning sim_tuning = s.sim->tuning();
            sim_tuning.bot_target_count = n;
            s.sim->setTuning(sim_tuning);
            // Despawn existing bot cells so the change is immediate. The director
            // will respawn up to N over the next several seconds on its own cadence.
            auto& cells = s.sim->world().cellsMut();
            int removed = 0;
            for (auto it = cells.begin(); it != cells.end();) {
                if (it->owner >= cr::ai::kFirstBotPlayerId) {
                    it = cells.erase(it);
                    ++removed;
                } else {
                    ++it;
                }
            }
            con.log("bots target=" + std::to_string(n)
                  + " (despawned " + std::to_string(removed) + " bot cells)");
        }
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

// Optional connection inputs for LocalClient mode. Ignored for other modes. Default-
// constructed in single-player paths. For LocalHost the port comes from a small
// internal default until the lobby exposes a "pick a port" UI.
struct MatchNetworkConfig {
    std::string  host_address;     // LocalClient only: "host[:port]" string
    uint16_t     listen_port = 7456; // LocalHost only: UDP bind port

    // Host-only: match parameters chosen in the lobby. Applied to the tuning
    // at runMatch entry. Default values preserve the previous Royale defaults
    // so callers that don't set them get the same behaviour.
    int  match_duration_sec = 300;
    int  max_players        = 8;
    int  bot_count          = 0;
};

// Runs one match start-to-finish. Builds a fresh sim + client, applies persistent state,
// runs the play loop, and writes updated persistent state back into `save` before
// returning. Designed to be called repeatedly from the menu loop with different seeds.
//
// `mode` selects the transport role; `net_cfg` carries the address/port for network
// modes.
//
// `shared_transport` lets the caller (runWindow) own the NetworkTransport across the
// LocalLobby + Match phases so peers that joined during the lobby remain connected
// once the match starts. Non-null means "this transport is already host()'d /
// connect()'d; don't disconnect on exit." Null falls back to the legacy behaviour
// where runMatch owns the transport (SP, plus the fallback if anything ever calls
// runMatch directly without the lobby flow).
//
// `shared_peer_to_player` + `shared_known_peer_names` mirror the lobby-side maps so
// the host spawns cells at match start for every peer that joined during the lobby.
// Same null-fallback semantics as the transport.
MatchOutcome runMatch(uint64_t seed, cr::Tuning& tuning, cr::SaveData& save,
                      MatchMode mode = MatchMode::SinglePlayer,
                      MatchNetworkConfig net_cfg = {},
                      cr::NetworkTransport* shared_transport = nullptr,
                      std::unordered_map<void*, cr::PlayerId>* shared_peer_to_player = nullptr,
                      std::unordered_map<cr::PlayerId, std::string>* shared_known_peer_names = nullptr,
                      const cr::codec::WelcomeMsg* preconsumed_welcome = nullptr,
                      cr::PlayerId next_peer_pid_seed = 2,
                      bool transport_already_announcing = false) {
    // Snapshot the caller's tuning so we can restore at the single return
    // point below. Mode-specific overrides (bots / match duration) and the
    // per-match Mutation both modify `tuning` in place; the saved copy is the
    // ground truth we'll restore to.
    const cr::Tuning saved_tuning = tuning;

    // Mode-specific tuning overrides. Royale modes (LocalHost / LocalClient)
    // pull bot count + match duration from the host's lobby panel
    // (net_cfg.bot_count / net_cfg.match_duration_sec); SinglePlayer respects
    // tuning.ini.
    if (mode == MatchMode::LocalHost) {
        tuning.bot_target_count = std::max(0, net_cfg.bot_count);
        // 0 in the settings panel means "endless" which the sim's match-end
        // gate already understands (duration <= 0 disables the timer).
        tuning.match_duration_sec = std::max(0, net_cfg.match_duration_sec);
    } else if (mode == MatchMode::LocalClient) {
        // Client doesn't tick its own sim, so the bot target doesn't matter
        // here; we still clamp to 0 so any stray local logic (post-disconnect
        // SP fall-through, dev console) doesn't surprise us. Match duration
        // arrives from the host inside each snapshot, so we leave tuning's
        // value at the SP default.
        tuning.bot_target_count = 0;
    }

    // ---- Per-match Mutation ----
    // Roll a world trait (3x viruses, comet storm, etc.) deterministically
    // from the match seed. Host + SP pick locally; LocalClient takes the
    // host's roll from the preconsumed Welcome so both sides apply the same
    // Tuning modifications (client-side prediction uses base_speed etc., so
    // the kinds must match exactly). Applied BEFORE Simulation construction
    // so the sim sees the mutated tuning from tick 1.
    cr::MutationKind active_mutation = cr::MutationKind::None;
    if (mode == MatchMode::LocalClient) {
        if (preconsumed_welcome != nullptr) {
            active_mutation = preconsumed_welcome->mutation_kind;
        }
        // No preconsumed welcome: leave at None for now. The inline welcome
        // handler in the main loop will applyMutation() once the host's
        // welcome packet arrives -- close enough since the client's local
        // Simulation never ticks (only client-side prediction reads tuning).
    } else {
        active_mutation = cr::pickRandomMutation(seed);
    }
    cr::applyMutation(tuning, active_mutation);
    {
        const cr::MutationInfo& mi = cr::mutationInfoFor(active_mutation);
        std::printf("[match] mutation: %s -- %s (seed=%llu)\n",
                    mi.name, mi.description,
                    static_cast<unsigned long long>(seed));
    }

    cr::Simulation sim(seed, tuning);

    // Player slot + initial cell. SinglePlayer / LocalHost spawn locally; LocalClient
    // defers the spawn to the host -- the welcome packet will tell us our slot + cell.
    // If runWindow already consumed the match-start welcome during the lobby phase
    // (preconsumed_welcome non-null), seed our player + cell from it now so the
    // Client object can be constructed with the correct ids on frame 1.
    cr::PlayerId player      = 1;
    cr::EntityId player_cell = cr::INVALID_ENTITY;
    if (mode == MatchMode::LocalClient && preconsumed_welcome != nullptr) {
        player      = preconsumed_welcome->player_id;
        player_cell = preconsumed_welcome->cell_id;
    }
    if (mode != MatchMode::LocalClient) {
        player_cell = sim.world().spawnCell(
            player,
            cr::Vec2{static_cast<float>(tuning.world_width)  * 0.5f,
                     static_cast<float>(tuning.world_height) * 0.5f},
            tuning.start_mass);
    }

    cr::Client client(player_cell,
                      (mode == MatchMode::LocalClient && preconsumed_welcome == nullptr)
                          ? cr::INVALID_PLAYER
                          : player);
    // Changes pause semantics in MP: Esc still opens the overlay menu, but
    // the sim keeps ticking underneath. Also picks the right pause-overlay
    // button labels via the role -- SP shows MAIN MENU, MP host shows
    // END MATCH, MP client shows DISCONNECT.
    client.setMultiplayerActive(mode != MatchMode::SinglePlayer);
    cr::Hud::PauseRole pause_role = cr::Hud::PauseRole::SinglePlayer;
    if (mode == MatchMode::LocalHost)   pause_role = cr::Hud::PauseRole::MpHost;
    if (mode == MatchMode::LocalClient) pause_role = cr::Hud::PauseRole::MpClient;
    client.setPauseRole(pause_role);

    // Drive the HUD's mutation banner. The banner timeline uses GetTime() as
    // a monotonic clock, so we pass it here as the "match start" timestamp.
    // For LocalClient without a preconsumed welcome, active_mutation is still
    // None at this point -- the inline welcome handler below will call
    // setMutation again with the host's roll once it arrives.
    client.hud().setMutation(active_mutation, GetTime());

    client.camera().snapTo(
        cr::Vec2{static_cast<float>(tuning.world_width)  * 0.5f,
                 static_cast<float>(tuning.world_height) * 0.5f},
        tuning.start_mass);
    // SinglePlayer / LocalHost can prime the renderer with an initial snapshot from
    // the local sim. LocalClient waits for the host's first snapshot to arrive.
    if (mode != MatchMode::LocalClient) {
        client.onSnapshot(sim.buildSnapshot());
    }

    // Push lifetime stats + settings into the new client so volume/input prefs and
    // best-ever counters are visible from frame 1.
    client.applyLoadedSave(save);

    // Live replay recording: only the authoritative sim writes a replay. Clients
    // would record their own (partial) command history but it'd never replay the
    // host's RNG / world state, so skip.
    cr::Replay                live_replay;
    bool                      replay_recording = (mode != MatchMode::LocalClient);
    if (replay_recording) {
        std::vector<cr::CellSnap> initial;
        for (const auto& c : sim.world().cells()) {
            initial.push_back(cr::CellSnap{c.id, c.owner, c.pos, c.vel, c.mass});
        }
        live_replay.recordSetup(seed, tuning.world_width, tuning.world_height,
                                std::move(initial));
    }

    // ---- Network setup ----
    // Transport + peer tables. If the caller (runWindow) supplied a shared
    // transport, peer_to_player, and known_peer_names, we use those so peers
    // that joined during the lobby stay connected through the match. Otherwise
    // fall back to local instances (SP path or any caller that bypasses the
    // lobby).
    cr::NetworkTransport  owned_transport;
    cr::NetworkTransport& net_transport = (shared_transport != nullptr)
                                              ? *shared_transport
                                              : owned_transport;
    std::unordered_map<void*, cr::PlayerId>       owned_peer_to_player;
    std::unordered_map<void*, cr::PlayerId>&      peer_to_player =
        (shared_peer_to_player != nullptr) ? *shared_peer_to_player
                                           : owned_peer_to_player;
    std::unordered_map<cr::PlayerId, std::string>  owned_known_peer_names;
    std::unordered_map<cr::PlayerId, std::string>& known_peer_names =
        (shared_known_peer_names != nullptr) ? *shared_known_peer_names
                                             : owned_known_peer_names;
    const bool using_shared_transport = (shared_transport != nullptr);
    // Host-side: the next PlayerId to assign to a joining peer. Starts past
    // any peer the lobby already accepted so we don't collide.
    cr::PlayerId next_peer_player_id = next_peer_pid_seed;
    for (const auto& [peer_ptr, pid] : peer_to_player) {
        if (pid >= next_peer_player_id) next_peer_player_id = pid + 1;
    }
    // LAN discovery: only the host runs an announcer; clients learn about
    // hosts via the JoinBrowsing screen (which owns its own discovery client
    // listener, separate from this runMatch state).
    cr::LocalDiscovery discovery;
    double last_announce_sec = 0.0;

    switch (mode) {
        case MatchMode::SinglePlayer:
            // No transport setup needed.
            break;
        case MatchMode::LocalHost:
            if (using_shared_transport) {
                // The lobby already bound the host port + started the LAN
                // announcer. Just take over the existing transport.
                std::printf("[net] resuming hosted match on existing transport "
                            "(peers=%d)\n",
                            net_transport.peerCount());
            } else if (!net_transport.host(net_cfg.listen_port)) {
                client.console().log("[net] failed to bind host port; running solo");
            } else {
                // Spin up the LAN announcer alongside the game socket. Best-
                // effort: a failure here just means the host isn't auto-
                // discoverable; manual "host:port" entry still works.
                char host_name[32];
                std::snprintf(host_name, sizeof(host_name),
                              "Cell Royale (port %u)",
                              static_cast<unsigned>(net_cfg.listen_port));
                if (!discovery.startHost(net_cfg.listen_port, host_name)) {
                    client.console().log("[net] LAN announcer failed to bind; "
                                          "joiners must enter address manually");
                }
            }
            break;
        case MatchMode::LocalClient:
            if (using_shared_transport) {
                std::printf("[net] resuming client match on existing transport\n");
            } else if (!net_transport.connect(net_cfg.host_address)) {
                client.console().log("[net] failed to connect; check host address");
            }
            break;
    }
    (void)transport_already_announcing; // currently inferred from using_shared_transport

    // Dev console state. Constructed AFTER the network bits so it can hold
    // pointers to net_transport + peer_to_player for host-only commands like
    // `kick`.
    WindowState state{&sim, &client, &live_replay, &tuning,
                      &replay_recording, &player_cell, player, mode,
                      &net_transport, &peer_to_player};
    client.console().setHandler(
        [&state](const std::vector<std::string>& args) { runDevCommand(state, args); });
    client.console().log("Cell Royale v0.2.0 -- press ~ for console, 'help' for commands");

    // Helper: find the watched cell in the snapshot (used by camera follow on
    // LocalClient, where the local sim's world is empty).
    auto findCellPosMassInSnap = [](const cr::Snapshot& s, cr::EntityId id,
                                     cr::Vec2& out_pos, float& out_mass) -> bool {
        for (const auto& c : s.cells) {
            if (c.id == id) {
                out_pos  = c.pos;
                out_mass = c.mass;
                return true;
            }
        }
        return false;
    };

    // Host-side helper: pick a clear spawn position for a fresh cell. Same algorithm
    // as the local respawn path lower in this function; duplicated here so peer-join
    // spawning doesn't have to wait for the main loop's respawn block.
    auto pickClearSpawn = [&]() -> cr::Vec2 {
        constexpr float kMargin = 500.0f;
        cr::Vec2 chosen{tuning.world_width * 0.5f, tuning.world_height * 0.5f};
        for (int attempt = 0; attempt < 8; ++attempt) {
            cr::Vec2 candidate{
                sim.world().rng().rangeFloat(kMargin,
                                             static_cast<float>(sim.world().width())  - kMargin),
                sim.world().rng().rangeFloat(kMargin,
                                             static_cast<float>(sim.world().height()) - kMargin),
            };
            bool clear = true;
            for (const auto& c : sim.world().cells()) {
                if (cr::distance(c.pos, candidate)
                    < cr::cellRadius(c.mass) + 400.0f) {
                    clear = false; break;
                }
            }
            if (clear) { chosen = candidate; break; }
        }
        return chosen;
    };

    // ---- Match-start spawn pass (host with shared transport) ----
    // Peers that joined during the lobby phase are in peer_to_player already, but
    // their cells haven't been spawned yet (the lobby Welcome carried cell_id =
    // INVALID). Now that the match is starting, spawn a cell for each, register
    // their name with the client, and ship a fresh Welcome with the real cell_id.
    // The Welcome doubles as the client's "match has started" trigger -- on
    // receiving a Welcome with a valid cell_id, the joiner transitions out of
    // their lobby's JoinWaiting screen into AppPhase::Match.
    if (mode == MatchMode::LocalHost && using_shared_transport) {
        for (auto& [peer_ptr, pid] : peer_to_player) {
            cr::Vec2 spawn_at = pickClearSpawn();
            cr::EntityId new_cell = sim.world().spawnCell(
                pid, spawn_at, tuning.start_mass);
            // Make sure the local client's name table knows about this peer.
            auto name_it = known_peer_names.find(pid);
            if (name_it != known_peer_names.end()) {
                client.setPlayerName(pid, name_it->second);
            }
            cr::codec::WelcomeMsg w;
            w.player_id     = pid;
            w.cell_id       = new_cell;
            w.host_name     = save.player_name;
            w.mutation_kind = active_mutation;
            cr::NetworkTransport::PeerHandle ph;
            ph.enet_peer = peer_ptr;
            net_transport.sendWelcomeTo(ph, w);
            // Catch the joining peer up on every OTHER peer's name we know.
            // This duplicates lobby-phase sends but it's idempotent on the
            // client and guarantees they're not missing anyone after match
            // start.
            for (const auto& [other_pid, other_name] : known_peer_names) {
                if (other_pid == pid) continue;
                cr::codec::PeerInfoMsg pi;
                pi.player_id = other_pid;
                pi.name      = other_name;
                net_transport.sendPeerInfoTo(ph, pi);
            }
            std::printf("[net] match start: spawned cell for peer player=%u cell=%u\n",
                        static_cast<unsigned>(pid),
                        static_cast<unsigned>(new_cell));
        }
        // Push every known name through the local client so the host's HUD
        // shows the right labels from frame 1.
        for (const auto& [pid, name] : known_peer_names) {
            client.setPlayerName(pid, name);
        }
        // Lag-fix: blast an initial snapshot to every peer immediately so
        // their Interpolator has data on frame 1 of the match (otherwise
        // they sit on an empty world for ~33ms while waiting for the host's
        // first sim tick). Refresh local client's snapshot too so the host's
        // own HUD reflects the new peer cells.
        if (!peer_to_player.empty()) {
            cr::Snapshot bootstrap = sim.buildSnapshot();
            client.onSnapshot(bootstrap);
            net_transport.sendSnapshot(std::move(bootstrap));
        }
    }

    // ---- Match-start welcome apply (client with shared transport) ----
    // If runWindow already consumed the match-start welcome, apply its
    // bookkeeping now (the player + cell ids were already used to construct
    // the client above, so all that remains is registering host + self names).
    if (mode == MatchMode::LocalClient && preconsumed_welcome != nullptr) {
        if (!preconsumed_welcome->host_name.empty()) {
            client.setPlayerName(/*host slot*/ 1, preconsumed_welcome->host_name);
        }
        client.setPlayerName(preconsumed_welcome->player_id, save.player_name);
        // Also re-apply any names the lobby phase collected via PeerInfo so
        // the killfeed / leaderboard / nameplate are populated on frame 1.
        for (const auto& [pid, name] : known_peer_names) {
            client.setPlayerName(pid, name);
        }
        std::printf("[net] match start: applying preconsumed welcome player=%u cell=%u\n",
                    static_cast<unsigned>(preconsumed_welcome->player_id),
                    static_cast<unsigned>(preconsumed_welcome->cell_id));
    }

    // ---- Client-side prediction (LocalClient only) ----
    // Tracks the locally-predicted position of the watched cell so motion is
    // visible the moment the player moves the cursor -- instead of waiting for
    // the host's ack to arrive (~50ms RTT). Each frame the prediction advances
    // toward the latest MoveCmd target at the same seek speed the host's sim
    // uses. When a snapshot lands we reconcile (small drift -> lerp toward
    // authoritative pos; large drift -> snap, e.g. after a split / virus / blast).
    bool     predict_active   = false;
    cr::Vec2 predict_pos{0.0f, 0.0f};
    cr::Vec2 predict_target{0.0f, 0.0f};
    float    predict_mass     = tuning.start_mass;
    cr::EntityId predict_cell = cr::INVALID_ENTITY;

    const float    sim_dt        = 1.0f / 30.0f;
    double         accumulator   = 0.0;
    MatchOutcome   outcome       = MatchOutcome::WindowClosed;

    while (!WindowShouldClose()) {
        // Player clicked MAIN MENU on the Summary panel (or the DISCONNECT /
        // END MATCH button on the MP pause overlay) -- end this match cleanly.
        if (client.consumeReturnToMenuRequest()) {
            outcome = MatchOutcome::ReturnToMenu;
            break;
        }
        // LocalClient: if the host vanished (disconnect / quit / network drop),
        // bounce us out instead of sitting on a stale snapshot forever. Logs
        // once (the transport sets the flag from a one-time DISCONNECT event).
        if (mode == MatchMode::LocalClient && net_transport.hostDisconnected()) {
            std::printf("[net] host disconnected -- returning to lobby\n");
            outcome = MatchOutcome::ReturnToMenu;
            break;
        }

        int   screen_w  = GetScreenWidth();
        int   screen_h  = GetScreenHeight();
        float frame_dt  = GetFrameTime();

        // Auto-pause when window loses focus -- keeps CPU/GPU/audio quiet in the
        // background and stops the sim from accumulating real time the player can't see.
        // We DON'T auto-pause in multiplayer modes: the host has to keep ticking so
        // peers receive snapshots, and the client has to keep polling so the
        // host's snapshots don't queue up in ENet's recv buffer indefinitely. This
        // also makes two-instances-on-one-machine loopback testing work (otherwise
        // whichever window has focus would pause the other).
        const bool skip_auto_pause = (mode != MatchMode::SinglePlayer);
        client.setAutoPaused(skip_auto_pause ? false : !IsWindowFocused());

        // Drain ENet events (CONNECT/DISCONNECT/RECEIVE) and decode any inbound
        // packets into the transport's typed queues. Cheap when no peers are
        // connected; safe to call when SinglePlayer (no-op without an enet host).
        if (mode != MatchMode::SinglePlayer) {
            net_transport.poll();
        }

        // Periodic LAN announce on the host side. Once per ~1s is enough for
        // the JOIN screen's polling cadence + dedupe window.
        if (mode == MatchMode::LocalHost
            && discovery.mode() == cr::LocalDiscovery::Mode::Host) {
            const double now = GetTime();
            if (now - last_announce_sec >= 1.0) {
                discovery.announceNow();
                last_announce_sec = now;
            }
        }

        // ---- Host: accept new peers ----
        // For each peer that just connected, allocate a PlayerId, spawn a cell at a
        // clear spot, and ship a welcome so they know which cell to watch. The
        // peer pointer is also stored in peer_to_player so we can despawn their
        // cells when they disconnect (or get kicked). Late-joiners beyond the
        // max-players cap are rejected (ENet handshake completes, then we drop).
        if (mode == MatchMode::LocalHost) {
            const int hard_cap =
                (net_cfg.max_players <= 0)
                    ? cr::MatchSettings::kHardPlayerCap
                    : std::min(net_cfg.max_players,
                               cr::MatchSettings::kHardPlayerCap);
            cr::NetworkTransport::PeerHandle ph;
            while (net_transport.pollNewPeer(ph)) {
                const int slots_used =
                    1 + static_cast<int>(peer_to_player.size());
                if (slots_used >= hard_cap) {
                    std::printf("[net] late-joiner rejected (full: %d/%d)\n",
                                slots_used, hard_cap);
                    net_transport.disconnectPeer(ph);
                    continue;
                }
                cr::PlayerId new_pid  = next_peer_player_id++;
                cr::Vec2     spawn_at = pickClearSpawn();
                cr::EntityId new_cell = sim.world().spawnCell(
                    new_pid, spawn_at, tuning.start_mass);
                // Welcome includes the host's display name so the joining
                // peer's HUD shows it immediately. Their own name arrives
                // later via ClientHello.
                cr::codec::WelcomeMsg welcome;
                welcome.player_id     = new_pid;
                welcome.cell_id       = new_cell;
                welcome.host_name     = save.player_name;
                welcome.mutation_kind = active_mutation;
                net_transport.sendWelcomeTo(ph, welcome);
                // Catch the joining peer up on every OTHER peer's name we
                // already know -- ship one PeerInfo per known peer so the
                // new peer's player_names_ table is complete from frame 1.
                for (const auto& [pid, name] : known_peer_names) {
                    if (pid == new_pid) continue;
                    cr::codec::PeerInfoMsg pi;
                    pi.player_id = pid;
                    pi.name      = name;
                    net_transport.sendPeerInfoTo(ph, pi);
                }
                peer_to_player[ph.enet_peer] = new_pid;
                std::printf("[net] spawned cell for new peer (player=%u, cell=%u)\n",
                            static_cast<unsigned>(new_pid),
                            static_cast<unsigned>(new_cell));
            }

            // Drain ClientHello messages -- one arrives per peer shortly
            // after their welcome. v2 ClientHello carries the peer's
            // self-reported PlayerId so we can attribute it deterministically
            // (the old "first peer without a name" heuristic raced if two
            // peers connected in the same tick).
            cr::codec::ClientHelloMsg hello;
            while (net_transport.pollClientHello(hello)) {
                if (hello.player_id == cr::INVALID_PLAYER) continue;
                // Sanity: the reported pid must be one we actually allocated.
                bool known_slot = false;
                for (const auto& [peer, pid] : peer_to_player) {
                    if (pid == hello.player_id) { known_slot = true; break; }
                }
                if (!known_slot) {
                    std::fprintf(stderr,
                                 "[net] dropping hello for unknown pid=%u\n",
                                 static_cast<unsigned>(hello.player_id));
                    continue;
                }
                known_peer_names[hello.player_id] = hello.name;
                client.setPlayerName(hello.player_id, hello.name);
                // Broadcast the new peer's name to EVERYONE (including the
                // joining peer itself -- harmless, they just re-set their
                // own map entry to the same value).
                cr::codec::PeerInfoMsg pi;
                pi.player_id = hello.player_id;
                pi.name      = hello.name;
                net_transport.broadcastPeerInfo(pi);
                std::printf("[net] registered peer name: player=%u \"%s\"\n",
                            static_cast<unsigned>(hello.player_id),
                            hello.name.c_str());
            }

            // ---- Host: clean up departed peers ----
            // Drain DISCONNECT events; for each one, look up the leaving PlayerId
            // in our map and despawn every cell that player owns. Skipped if the
            // peer never made it through the welcome flow (shouldn't happen, but
            // map.find handles it cleanly).
            while (net_transport.pollDepartedPeer(ph)) {
                auto it = peer_to_player.find(ph.enet_peer);
                if (it == peer_to_player.end()) continue;
                const cr::PlayerId departing = it->second;
                peer_to_player.erase(it);
                known_peer_names.erase(departing);
                auto& cells = sim.world().cellsMut();
                int   removed = 0;
                for (auto cit = cells.begin(); cit != cells.end();) {
                    if (cit->owner == departing) {
                        cit = cells.erase(cit);
                        ++removed;
                    } else {
                        ++cit;
                    }
                }
                std::printf("[net] cleaned up departed peer (player=%u, despawned %d cells)\n",
                            static_cast<unsigned>(departing), removed);
            }
        }

        // ---- Client: consume welcome ----
        // After the welcome arrives we know our PlayerId + cell id. Until then the
        // client has placeholder watched values; the world is empty so input
        // commands fire harmlessly (they target INVALID_PLAYER which the host
        // ignores).
        if (mode == MatchMode::LocalClient) {
            cr::codec::WelcomeMsg w;
            if (net_transport.consumeWelcome(w)) {
                client.setWatchedCell(w.cell_id);
                client.setWatchedPlayer(w.player_id);
                player        = w.player_id;
                player_cell   = w.cell_id;
                // Register the host's name (player slot 1) so our killfeed /
                // leaderboard / nameplate show it. Also send our own name up
                // so the host (and via PeerInfo, the other peers) learn it.
                if (!w.host_name.empty()) {
                    client.setPlayerName(/*host slot*/ 1, w.host_name);
                }
                client.setPlayerName(w.player_id, save.player_name);
                // Late application of the per-match Mutation -- this branch
                // only fires when the client entered runMatch WITHOUT a
                // preconsumed welcome (rare; normal flow preconsumes during
                // the lobby phase). Local sim is already constructed by now,
                // but client-side prediction still reads from `tuning` so
                // catching up here keeps speed / split thresholds in sync
                // with the host.
                if (active_mutation == cr::MutationKind::None
                    && w.mutation_kind != cr::MutationKind::None) {
                    active_mutation = w.mutation_kind;
                    cr::applyMutation(tuning, active_mutation);
                    // Refresh the HUD banner so the player sees the reveal
                    // at first-paint time (rather than missing it because the
                    // initial setMutation call ran with None).
                    client.hud().setMutation(active_mutation, GetTime());
                    const cr::MutationInfo& mi = cr::mutationInfoFor(active_mutation);
                    std::printf("[match] mutation applied from welcome: %s -- %s\n",
                                mi.name, mi.description);
                }
                cr::codec::ClientHelloMsg hello;
                hello.player_id = w.player_id;
                hello.name      = save.player_name;
                net_transport.sendClientHelloToHost(hello);
                std::printf("[net] client received welcome: player_id=%u cell_id=%u host=\"%s\"\n",
                            static_cast<unsigned>(w.player_id),
                            static_cast<unsigned>(w.cell_id),
                            w.host_name.c_str());
            }
            // Drain PeerInfo updates -- the host sends these for every other
            // peer when we join, plus a broadcast when any peer announces a
            // new name. Apply them to the local name map.
            cr::codec::PeerInfoMsg pi;
            while (net_transport.pollPeerInfo(pi)) {
                client.setPlayerName(pi.player_id, pi.name);
            }
        }

        client.pollFrame(screen_w, screen_h, sim.currentTick());

        // Drain commands queued by Client and forward to sim + replay tape.
        auto cmds = client.takeCommands();
        for (const auto& c : cmds) {
            // LocalClient: don't queue into the local sim (we're not ticking it).
            // Send up to the host instead; the host queues into its sim.
            if (mode == MatchMode::LocalClient) {
                net_transport.sendCommand(c);
                // Capture move targets for client-side prediction. The next-frame
                // advance step pushes predict_pos toward predict_target at the
                // same cellSpeed the host's sim uses.
                if (auto* m = std::get_if<cr::MoveCmd>(&c.payload)) {
                    predict_target = m->target;
                }
            } else {
                sim.queueCommand(c);
                if (replay_recording) live_replay.recordCommand(c);
            }
        }

        // Advance the client-side prediction toward the latest move target. Matches
        // rules::stepCells's seek + anti-jitter clamp so the predicted position
        // tracks the host's authoritative motion bit-for-bit when there's no
        // launch_vel involved (splits / blasts are reconciled when the snapshot
        // arrives -- the prediction snaps to the new pos if drift exceeds the
        // threshold below).
        if (mode == MatchMode::LocalClient && predict_active
            && client.phase() == cr::GamePhase::Playing) {
            cr::Vec2 to_target = predict_target - predict_pos;
            float dist = cr::length(to_target);
            if (dist > 1e-3f) {
                cr::Vec2 dir   = to_target * (1.0f / dist);
                float    r     = std::max(1.0f, cr::cellRadius(predict_mass));
                float    speed = tuning.base_speed
                               * std::pow(30.0f / r, tuning.speed_falloff);
                float    step  = speed * frame_dt;
                if (step > dist) step = dist; // don't overshoot
                predict_pos = predict_pos + dir * step;
            }
        }

        // LocalHost: drain commands received from peers via the network transport
        // and queue them into the local sim. The transport decoded the bytes during
        // poll() above so they're already typed Command objects here.
        if (mode == MatchMode::LocalHost) {
            cr::Command remote_cmd;
            while (net_transport.pollCommand(remote_cmd)) {
                sim.queueCommand(remote_cmd);
                if (replay_recording) live_replay.recordCommand(remote_cmd);
            }
        }

        // Per-frame feel layer update (shake decay, particles, popups, HUD, death cam).
        client.updateFrame(frame_dt, GetTime(), tuning);

        // ---- Sim advancement ----
        // SinglePlayer / LocalHost: tick the authoritative sim and (for host)
        //                           broadcast the resulting snapshot + events.
        // LocalClient            : DO NOT tick the local sim; the world state comes
        //                           from received snapshots which we feed straight
        //                           into the Interpolator below.
        if (mode != MatchMode::LocalClient && !client.isHitstopActive()) {
            accumulator += static_cast<double>(frame_dt) * client.effectiveDtMultiplier();
            int safety = 0;
            while (accumulator >= sim_dt && safety < 8) {
                sim.tick(sim_dt);
                cr::Snapshot snap = sim.buildSnapshot();
                client.onSnapshot(snap);
                std::vector<cr::GameEvent> evs = sim.takeEvents();
                client.onEvents(evs, sim.world(), tuning);
                if (mode == MatchMode::LocalHost) {
                    net_transport.sendSnapshot(snap);
                    for (const auto& e : evs) net_transport.sendEvent(e);
                }
                accumulator -= sim_dt;
                ++safety;
            }
            if (accumulator >= sim_dt) {
                // Long pause / breakpoint -- drop the backlog instead of spiraling.
                accumulator = 0.0;
            }
        }

        // LocalClient: feed every received snapshot to the renderer's Interpolator.
        // Same for events -- they drive particles / audio / killfeed / etc. The
        // local sim is empty so onEvents path that looks up world cells will return
        // null for most fields; the visual-only paths still fire because they rely
        // on event data not world state.
        //
        // The accumulator drives the renderer's interpolation alpha (alpha = accum /
        // sim_dt). Since we don't tick a local sim, we advance the accumulator with
        // wall-clock time and reset it whenever a fresh snapshot arrives -- alpha
        // then sweeps 0 -> 1 between consecutive snapshots, the same way it does
        // when the local sim is ticking authoritatively. Without this the alpha
        // stays at 0 forever and the renderer shows the prev snapshot with no
        // motion -- the cause of the "jumpy / laggy" client movement.
        if (mode == MatchMode::LocalClient) {
            int snaps_received = 0;
            cr::Snapshot snap;
            while (net_transport.pollSnapshot(snap)) {
                // Client-side prediction reconciliation. Find the watched cell
                // in this snapshot; the snap's pos is the host's authoritative
                // truth. We compare against our local prediction:
                //   * If we haven't initialised the predictor yet, accept the
                //     snap's pos and mass verbatim.
                //   * If drift is "small" (< 80 px), blend toward the host
                //     gently so a tiny perpetual offset doesn't accumulate.
                //   * If drift is "large" (>= 80 px), snap to the host -- a
                //     split / virus / blast just rearranged things in a way
                //     our linear seek couldn't predict.
                // After reconciling we overwrite the snap's watched-cell pos
                // with predict_pos so the Interpolator + camera see the
                // predicted (lag-free) position uniformly.
                if (player_cell != cr::INVALID_ENTITY) {
                    for (auto& cs : snap.cells) {
                        if (cs.id != player_cell) continue;
                        // Re-init whenever we're tracking a fresh cell -- the
                        // welcome -> respawn-adoption flow can change
                        // player_cell without going through our explicit reset.
                        if (!predict_active || predict_cell != cs.id) {
                            predict_pos    = cs.pos;
                            predict_target = cs.pos;
                            predict_mass   = cs.mass;
                            predict_cell   = cs.id;
                            predict_active = true;
                        } else {
                            cr::Vec2 drift = cs.pos - predict_pos;
                            float dsq = cr::lengthSq(drift);
                            constexpr float kSnapDistSq = 80.0f * 80.0f;
                            if (dsq > kSnapDistSq) {
                                predict_pos = cs.pos; // snap hard
                            } else {
                                // 25%/frame correction = barely visible drift
                                // washout over a few frames.
                                predict_pos = cr::lerp(predict_pos, cs.pos, 0.25f);
                            }
                            predict_mass = cs.mass;
                        }
                        cs.pos = predict_pos;
                        break;
                    }
                }
                client.onSnapshot(snap);
                ++snaps_received;
            }
            if (snaps_received > 0) {
                // A fresh snapshot lands at "alpha = 0" and we begin interpolating
                // toward it. We pin accumulator to 0 even if multiple snaps arrived
                // in one frame -- we always render against the latest pair.
                accumulator = 0.0;
            } else {
                // No snapshot this frame: advance the alpha so motion stays smooth
                // between snapshots. Cap at sim_dt so alpha doesn't run past 1
                // (which would overshoot during temporary network stalls).
                accumulator = std::min(accumulator
                                       + static_cast<double>(frame_dt),
                                       static_cast<double>(sim_dt));
            }
            std::vector<cr::GameEvent> evs;
            cr::GameEvent ev;
            while (net_transport.pollEvent(ev)) {
                evs.push_back(std::move(ev));
            }
            if (!evs.empty()) {
                client.onEvents(evs, sim.world(), tuning);
            }
        }

        // Respawn flow. Three paths:
        //   SinglePlayer / LocalHost: spawn locally and immediately mark the
        //     client back to Playing. (Host's own respawn -- direct mutation of
        //     the authoritative sim.)
        //   LocalClient: send a RespawnCmd up to the host; the host's sim runs
        //     doRespawn, the new cell shows up in the next snapshot, and the
        //     `re-acquire` block below transitions the client back to Playing.
        if (client.consumeRespawnRequest()) {
            if (mode == MatchMode::LocalClient) {
                cr::Command rc;
                rc.player  = player;
                rc.tick    = 0; // host applies on receipt; tick is ignored
                rc.payload = cr::RespawnCmd{};
                net_transport.sendCommand(rc);
            } else {
                cr::Vec2 spawn = pickClearSpawn();
                player_cell = sim.world().spawnCell(player, spawn, tuning.start_mass);
                client.setWatchedCell(player_cell);
                client.camera().snapTo(spawn, tuning.start_mass);
                client.onPlayerRespawned(GetTime());
                sim.director().resetPlayerTracking();
            }
        }

        // LocalClient: once the host's snapshot reflects a fresh cell for our
        // PlayerId, adopt it and transition back to Playing. This handles the
        // wait-for-snapshot half of the respawn protocol. We look up the largest
        // cell owned by `player` -- the host's spawn always lands at start_mass,
        // and the player has exactly one cell at this point, so the lookup is
        // unambiguous.
        if (mode == MatchMode::LocalClient
            && client.phase() == cr::GamePhase::Respawning
            && client.interpolator().hasCurr()) {
            const auto& snap = client.interpolator().curr();
            cr::EntityId best  = cr::INVALID_ENTITY;
            float        best_mass = 0.0f;
            for (const auto& c : snap.cells) {
                if (c.owner == player && c.mass > best_mass) {
                    best_mass = c.mass;
                    best      = c.id;
                }
            }
            if (best != cr::INVALID_ENTITY) {
                player_cell = best;
                client.setWatchedCell(best);
                client.onPlayerRespawned(GetTime());
                // Reset prediction so the next snapshot re-initialises it for the
                // freshly-spawned cell. Without this, predict_pos would still be
                // pinned to the death location and the new cell would visibly
                // teleport into place once the snap reconciliation runs.
                predict_active = false;
                predict_cell   = best;
                std::printf("[net] client respawn complete (new cell=%u)\n",
                            static_cast<unsigned>(best));
            }
        }

        // Interpolation alpha for this frame. Computed *before* camera follow so the
        // LocalClient camera can track the cell's interpolated position (where the
        // renderer is actually drawing it) instead of the latest snapshot's pos
        // (where the cell will be one tick from now). Used for the renderer below
        // too.
        float alpha = client.isHitstopActive()
                          ? 1.0f
                          : static_cast<float>(accumulator / sim_dt);

        // Camera: death cam steals the camera target while it's active. Otherwise follow
        // the watched cell, retargeting to the player's largest piece if it died.
        // Local sim path uses sim.world(); LocalClient reads from the renderer's
        // interpolated snapshot since its local sim is empty.
        if (client.deathCamActive()) {
            // For player-vs-player deaths the target is the killer cell. For comet
            // kills it's the comet's entity id; fall back to that if no cell matches.
            const cr::EntityId tgt = client.deathCamTarget();
            if (mode == MatchMode::LocalClient && client.interpolator().hasCurr()) {
                cr::Vec2 latest_pos; float m;
                if (findCellPosMassInSnap(client.interpolator().curr(), tgt,
                                          latest_pos, m)) {
                    // Interpolated lookup so the killer focus doesn't strobe.
                    client.camera().setTarget(
                        client.interpolator().cellPos(tgt, alpha), m);
                } else {
                    // Maybe a comet in the snapshot
                    for (const auto& cm : client.interpolator().curr().comets) {
                        if (cm.id == tgt) {
                            client.camera().setTarget(cm.pos,
                                cm.radius * cm.radius / 9.0f);
                            break;
                        }
                    }
                }
            } else {
                if (auto* killer = sim.world().findCell(tgt)) {
                    client.camera().setTarget(killer->pos, killer->mass);
                } else if (auto* comet = sim.world().findComet(tgt)) {
                    client.camera().setTarget(comet->pos,
                        comet->radius * comet->radius / 9.0f);
                }
            }
        } else if (mode == MatchMode::LocalClient) {
            // Client-side follow via the interpolated snapshot pair. Find the
            // watched cell; if it's gone (died on the host), fall back to the
            // largest cell that belongs to our player slot and adopt that as the
            // new watched.
            if (client.interpolator().hasCurr()) {
                const auto& snap = client.interpolator().curr();
                cr::Vec2 latest_pos; float mass;
                bool found = findCellPosMassInSnap(snap, player_cell,
                                                   latest_pos, mass);
                if (!found) {
                    cr::EntityId best  = cr::INVALID_ENTITY;
                    float    best_mass = 0.0f;
                    for (const auto& c : snap.cells) {
                        if (c.owner == player && c.mass > best_mass) {
                            best_mass = c.mass;
                            best      = c.id;
                        }
                    }
                    if (best != cr::INVALID_ENTITY) {
                        player_cell = best;
                        client.setWatchedCell(best);
                        found = findCellPosMassInSnap(snap, best, latest_pos, mass);
                    }
                }
                if (found) {
                    // cellPos() interpolates prev<->curr by alpha, matching the
                    // visible cell position. The camera now travels in lockstep
                    // with the rendered cell instead of one tick ahead.
                    client.camera().setTarget(
                        client.interpolator().cellPos(player_cell, alpha), mass);
                }
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

        BeginDrawing();
        ClearBackground(Color{18, 22, 30, 255});
        client.render(screen_w, screen_h, alpha, tuning, sim.world(), sim.currentTick());
        EndDrawing();
    }

    // Pull updated progression + lifetime stats + settings out of the client so the
    // outer loop has fresh data for the menu display and the on-disk save.
    save = client.snapshotForSave();
    // Tear down the transport only when we own it. The shared-transport path
    // hands lifecycle back to runWindow which decides when to disconnect
    // (typically right after this returns so the next lobby visit starts
    // fresh).
    if (!using_shared_transport) {
        net_transport.disconnect();
    }
    // Restore the outer tuning so a subsequent match doesn't inherit either
    // the mode overrides (bot_target_count / match_duration_sec) or the
    // per-match Mutation modifications (food_target, virus_count, base_speed,
    // etc.). Wholesale restore -- idempotent for SP, cheap for any mode.
    tuning = saved_tuning;
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
    cr::RoyaleMenu     royale_menu;
    cr::LocalLobby     local_lobby;
    cr::SettingsScreen settings;
    // Push the loaded accessibility state into the renderer-side globals so the
    // menu's bg cells (and the first match before applyLoadedSave runs) use them.
    cr::setPaletteMode(static_cast<cr::PaletteMode>(
        save.colorblind_mode <= 3 ? save.colorblind_mode : 0));
    cr::setHighContrast(save.high_contrast);
    cr::setHudTextScale(save.hud_text_scale);

    enum class AppPhase {
        Intro,
        Menu,
        RoyaleMenu,
        LocalLobby,
        Settings,
        Match,
        Quit,
    };
    // What kind of match the outer loop should launch the next time it enters
    // AppPhase::Match. The lobby code writes this when it hands off, and we read it
    // (then reset to SinglePlayer) inside the Match branch.
    MatchMode          next_match_mode = MatchMode::SinglePlayer;
    MatchNetworkConfig next_match_net{};
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

    // ---- Lobby-level multiplayer state ----
    // Lives across the LocalLobby <-> Match phase boundary so peers that joined
    // during the lobby keep their connection (and PlayerId) once the match
    // begins. runMatch is handed pointers to all three when it starts a Local
    // mode -- on return, runWindow tears them down (clean state for the next
    // lobby visit).
    cr::NetworkTransport                          lobby_transport;
    std::unordered_map<void*, cr::PlayerId>       lobby_peer_to_player;
    std::unordered_map<cr::PlayerId, std::string> lobby_known_peer_names;
    cr::LocalDiscovery                            lobby_host_announcer; // host-side
    bool         lobby_socket_open       = false; // true iff transport is host()/connect()'d
    bool         lobby_is_host           = false; // role we entered with
    cr::PlayerId lobby_next_player_id    = 2;     // next free slot (host)
    cr::PlayerId lobby_my_player_id      = cr::INVALID_PLAYER; // filled by welcome (client)
    double       lobby_last_announce_sec = 0.0;
    std::optional<cr::codec::WelcomeMsg> lobby_pending_match_welcome;
    cr::LobbySubState lobby_prev_substate = cr::LobbySubState::Picker;

    // Tear down the lobby's transport + LAN announcer + peer state. Called on
    // BACK / disconnect events and after runMatch returns so the next lobby
    // visit starts from a known-clean state.
    auto closeLobbyTransport = [&]() {
        if (!lobby_socket_open) return;
        std::printf("[lobby] closing transport (was host=%d, peers=%d)\n",
                    lobby_is_host ? 1 : 0, lobby_transport.peerCount());
        lobby_transport.disconnect();
        lobby_host_announcer.stop();
        lobby_socket_open       = false;
        lobby_is_host           = false;
        lobby_peer_to_player.clear();
        lobby_known_peer_names.clear();
        lobby_next_player_id    = 2;
        lobby_my_player_id      = cr::INVALID_PLAYER;
        lobby_pending_match_welcome.reset();
        lobby_last_announce_sec = 0.0;
    };

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
                next_match_mode = MatchMode::SinglePlayer;
                next_match_net  = {};
                phase = AppPhase::Match;
                cr::swallowNextClick();
            } else if (action == cr::MenuAction::ShowRoyaleMenu) {
                phase = AppPhase::RoyaleMenu;
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
        } else if (phase == AppPhase::RoyaleMenu) {
            float dt = GetFrameTime();
            int   sw = GetScreenWidth();
            int   sh = GetScreenHeight();
            royale_menu.update(dt, sw, sh);

            BeginDrawing();
            ClearBackground(Color{18, 22, 30, 255});
            cr::RoyaleMenuAction ra = royale_menu.render(sw, sh);
            EndDrawing();

            if (ra == cr::RoyaleMenuAction::Quit) {
                phase = AppPhase::Quit;
            } else if (ra == cr::RoyaleMenuAction::BackToMainMenu) {
                phase = AppPhase::Menu;
                cr::swallowNextClick();
            } else if (ra == cr::RoyaleMenuAction::ShowLocalLobby) {
                local_lobby.reset();
                phase = AppPhase::LocalLobby;
                cr::swallowNextClick();
            }
        } else if (phase == AppPhase::LocalLobby) {
            float dt = GetFrameTime();
            int   sw = GetScreenWidth();
            int   sh = GetScreenHeight();
            local_lobby.update(dt, sw, sh);

            // ---- Drive the live socket every lobby frame ----
            // Poll the transport so CONNECT/DISCONNECT/RECEIVE events get
            // surfaced. peerCount() updates inside poll(). Cheap when the
            // socket is closed (no-op).
            if (lobby_socket_open) {
                lobby_transport.poll();

                // Periodic LAN announce on the host side. Same cadence as
                // the in-match announcer (1Hz).
                if (lobby_is_host
                    && lobby_host_announcer.mode() == cr::LocalDiscovery::Mode::Host) {
                    const double now = GetTime();
                    if (now - lobby_last_announce_sec >= 1.0) {
                        lobby_host_announcer.announceNow();
                        lobby_last_announce_sec = now;
                    }
                }

                // Host-side: accept new peers, hand them a lobby Welcome (with
                // cell_id = INVALID so the joiner knows we're still in the
                // lobby), and add them to the peer map. Max-players cap is
                // enforced here -- connections beyond the limit are accepted
                // (ENet handshake) then immediately dropped with a log.
                if (lobby_is_host) {
                    cr::NetworkTransport::PeerHandle ph;
                    while (lobby_transport.pollNewPeer(ph)) {
                        const auto& settings = local_lobby.matchSettings();
                        const int hard_cap = (settings.max_players <= 0)
                            ? cr::MatchSettings::kHardPlayerCap
                            : std::min(settings.max_players,
                                       cr::MatchSettings::kHardPlayerCap);
                        // host counts as 1 (slot 1); peers fill 2..N.
                        const int current_count =
                            1 + static_cast<int>(lobby_peer_to_player.size());
                        if (current_count >= hard_cap) {
                            std::printf(
                                "[lobby] rejecting peer (lobby full: %d/%d)\n",
                                current_count, hard_cap);
                            lobby_transport.disconnectPeer(ph);
                            continue;
                        }

                        cr::PlayerId new_pid = lobby_next_player_id++;
                        lobby_peer_to_player[ph.enet_peer] = new_pid;
                        cr::codec::WelcomeMsg w;
                        w.player_id = new_pid;
                        w.cell_id   = cr::INVALID_ENTITY; // lobby-phase marker
                        w.host_name = save.player_name;
                        lobby_transport.sendWelcomeTo(ph, w);
                        // Catch the new peer up on every name we already know.
                        for (const auto& [pid, name] : lobby_known_peer_names) {
                            if (pid == new_pid) continue;
                            cr::codec::PeerInfoMsg pi;
                            pi.player_id = pid;
                            pi.name      = name;
                            lobby_transport.sendPeerInfoTo(ph, pi);
                        }
                        std::printf("[lobby] peer joined player=%u (%d/%d slots)\n",
                                    static_cast<unsigned>(new_pid),
                                    current_count + 1, hard_cap);
                    }

                    // ClientHello from peers carries their self-reported
                    // PlayerId (from their lobby Welcome) + display name.
                    cr::codec::ClientHelloMsg hello;
                    while (lobby_transport.pollClientHello(hello)) {
                        if (hello.player_id == cr::INVALID_PLAYER) continue;
                        bool known_slot = false;
                        for (const auto& [peer, pid] : lobby_peer_to_player) {
                            if (pid == hello.player_id) { known_slot = true; break; }
                        }
                        if (!known_slot) {
                            std::fprintf(stderr,
                                         "[lobby] dropping hello for unknown pid=%u\n",
                                         static_cast<unsigned>(hello.player_id));
                            continue;
                        }
                        lobby_known_peer_names[hello.player_id] = hello.name;
                        // Broadcast PeerInfo to everyone (including the joiner
                        // -- harmless echo of their own name).
                        cr::codec::PeerInfoMsg pi;
                        pi.player_id = hello.player_id;
                        pi.name      = hello.name;
                        lobby_transport.broadcastPeerInfo(pi);
                        std::printf("[lobby] peer name registered: player=%u \"%s\"\n",
                                    static_cast<unsigned>(hello.player_id),
                                    hello.name.c_str());
                    }

                    // Departed peers: drop their entry. (Cells aren't spawned
                    // yet so there's nothing to despawn.)
                    while (lobby_transport.pollDepartedPeer(ph)) {
                        auto it = lobby_peer_to_player.find(ph.enet_peer);
                        if (it == lobby_peer_to_player.end()) continue;
                        cr::PlayerId departed = it->second;
                        lobby_peer_to_player.erase(it);
                        lobby_known_peer_names.erase(departed);
                        std::printf("[lobby] peer departed player=%u\n",
                                    static_cast<unsigned>(departed));
                    }
                }

                // Client-side: consume welcomes from the host. The lobby
                // welcome (cell_id == INVALID) tells us we're in the lobby
                // and gives us our PlayerId + the host's name. The match
                // welcome (cell_id != INVALID) is our signal to transition
                // into AppPhase::Match.
                if (!lobby_is_host) {
                    cr::codec::WelcomeMsg w;
                    while (lobby_transport.consumeWelcome(w)) {
                        if (w.cell_id == cr::INVALID_ENTITY) {
                            // Lobby join. Record self + host names. Send our
                            // own name up so the host can broadcast it.
                            // If the user hasn't typed a name in Settings,
                            // fall back to "Player <id>" so the host has
                            // SOMETHING to display instead of treating us as
                            // "Joining..." forever.
                            std::string effective_name = save.player_name;
                            if (effective_name.empty()) {
                                char buf[32];
                                std::snprintf(buf, sizeof(buf), "Player %u",
                                              static_cast<unsigned>(w.player_id));
                                effective_name = buf;
                            }
                            lobby_my_player_id = w.player_id;
                            lobby_known_peer_names[w.player_id] = effective_name;
                            lobby_known_peer_names[1] =
                                w.host_name.empty() ? std::string("Host") : w.host_name;
                            local_lobby.setRemoteHostName(
                                w.host_name.empty() ? std::string("Host") : w.host_name);
                            cr::codec::ClientHelloMsg hello;
                            hello.player_id = w.player_id;
                            hello.name      = effective_name;
                            lobby_transport.sendClientHelloToHost(hello);
                            std::printf("[lobby] lobby welcome: player_id=%u host=\"%s\" sending name=\"%s\"\n",
                                        static_cast<unsigned>(w.player_id),
                                        w.host_name.c_str(),
                                        effective_name.c_str());
                        } else {
                            // Match start! Stash the welcome and trigger the
                            // phase transition below.
                            lobby_pending_match_welcome = w;
                            std::printf("[lobby] match-start welcome: player=%u cell=%u\n",
                                        static_cast<unsigned>(w.player_id),
                                        static_cast<unsigned>(w.cell_id));
                        }
                    }
                    // PeerInfo updates for other peers in the lobby.
                    cr::codec::PeerInfoMsg pi;
                    while (lobby_transport.pollPeerInfo(pi)) {
                        lobby_known_peer_names[pi.player_id] = pi.name;
                    }

                    // Detect host disappearance (closed window, kicked us).
                    if (lobby_transport.hostDisconnected()) {
                        std::printf("[lobby] host disconnected -- back to picker\n");
                        closeLobbyTransport();
                        local_lobby.reset();
                    }
                }
            }

            // ---- Build the per-frame player list ----
            std::vector<cr::LobbyPlayerRow> rows;
            if (lobby_is_host) {
                cr::LobbyPlayerRow me;
                me.id      = 1;
                me.name    = save.player_name.empty() ? std::string("Player 1")
                                                      : save.player_name;
                me.is_self = true;
                me.is_host = true;
                rows.push_back(me);
                for (const auto& [pid, name] : lobby_known_peer_names) {
                    if (pid == 1) continue;
                    cr::LobbyPlayerRow r;
                    r.id      = pid;
                    if (name.empty()) {
                        // Defensive: shouldn't happen in normal flow (the
                        // client always sends at least "Player <id>") but
                        // if it ever does, show a placeholder that's clear
                        // the player is connected, not stuck mid-handshake.
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "Player %u",
                                      static_cast<unsigned>(pid));
                        r.name = buf;
                    } else {
                        r.name = name;
                    }
                    r.is_self = false;
                    r.is_host = false;
                    rows.push_back(r);
                }
            } else {
                // Client view: show host + ourselves + any other known peers.
                for (const auto& [pid, name] : lobby_known_peer_names) {
                    cr::LobbyPlayerRow r;
                    r.id      = pid;
                    r.name    = name.empty() ? std::string("Player") : name;
                    r.is_self = (pid == lobby_my_player_id);
                    r.is_host = (pid == 1);
                    rows.push_back(r);
                }
            }
            local_lobby.setPlayerList(std::move(rows));

            // Status lines for the sub-state panels.
            if (lobby_is_host && lobby_socket_open) {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "listening on udp/7456  |  %d peer%s connected  |  share your local IP",
                              lobby_transport.peerCount(),
                              lobby_transport.peerCount() == 1 ? "" : "s");
                local_lobby.setHostStatus(buf);
            }
            if (!lobby_is_host && lobby_socket_open) {
                char buf[160];
                if (lobby_known_peer_names.empty()) {
                    std::snprintf(buf, sizeof(buf),
                                  "connecting to %s...",
                                  local_lobby.joinTargetAddress().c_str());
                } else {
                    std::snprintf(buf, sizeof(buf),
                                  "in lobby -- waiting for host to start...");
                }
                local_lobby.setJoinStatus(buf);
            }

            BeginDrawing();
            ClearBackground(Color{18, 22, 30, 255});
            cr::LocalLobbyAction la = local_lobby.render(sw, sh);
            EndDrawing();

            // ---- Drive transitions based on lobby actions / sub-state changes ----
            cr::LobbySubState cur = local_lobby.subState();

            // BeginHosting: user pressed HOST in the picker. Open the socket
            // + start the LAN announcer now so peers can join while the user
            // waits in HostWaiting.
            if (la == cr::LocalLobbyAction::BeginHosting) {
                if (!lobby_transport.host(7456)) {
                    std::printf("[lobby] failed to bind udp/7456\n");
                    closeLobbyTransport();
                    local_lobby.reset();
                } else {
                    lobby_socket_open       = true;
                    lobby_is_host           = true;
                    lobby_next_player_id    = 2;
                    // Register host's name under player_id=1 so the player
                    // list renders it.
                    if (!save.player_name.empty()) {
                        lobby_known_peer_names[1] = save.player_name;
                    }
                    // LAN announcer is best-effort.
                    char hostname[32];
                    std::snprintf(hostname, sizeof(hostname),
                                  "%s's game",
                                  save.player_name.empty() ? "Cell Royale"
                                                           : save.player_name.c_str());
                    lobby_host_announcer.startHost(7456, hostname);
                    std::printf("[lobby] hosting on udp/7456 (LAN announcer: %s)\n",
                                lobby_host_announcer.mode() == cr::LocalDiscovery::Mode::Host
                                    ? "on" : "off");
                }
            }
            // BeginJoining: user clicked a JOIN row or pressed JOIN by IP.
            // Open a client transport and wait for the host's welcome.
            else if (la == cr::LocalLobbyAction::BeginJoining) {
                if (!lobby_transport.connect(local_lobby.joinTargetAddress())) {
                    std::printf("[lobby] failed to connect to %s\n",
                                local_lobby.joinTargetAddress().c_str());
                    closeLobbyTransport();
                    local_lobby.reset();
                } else {
                    lobby_socket_open = true;
                    lobby_is_host     = false;
                    std::printf("[lobby] connecting to %s\n",
                                local_lobby.joinTargetAddress().c_str());
                }
            }
            // Backing out closes the transport.
            else if (la == cr::LocalLobbyAction::LeaveHostingLobby
                     || la == cr::LocalLobbyAction::LeaveJoiningLobby) {
                closeLobbyTransport();
            }

            // ---- Dispatch lobby actions ----
            if (la == cr::LocalLobbyAction::Quit) {
                closeLobbyTransport();
                phase = AppPhase::Quit;
            } else if (la == cr::LocalLobbyAction::BackToRoyaleMenu) {
                closeLobbyTransport();
                phase = AppPhase::RoyaleMenu;
                cr::swallowNextClick();
            } else if (la == cr::LocalLobbyAction::StartLocalHost) {
                // Host clicked START GAME -- hand off to runMatch reusing the
                // live transport + peer maps. lobby_pending_match_welcome
                // stays empty for the host (it's a client-only signal).
                next_match_mode = MatchMode::LocalHost;
                next_match_net  = {};
                next_match_net.listen_port = 7456;
                // Carry the host's lobby-panel choices into runMatch's tuning
                // overrides.
                const auto& s = local_lobby.matchSettings();
                next_match_net.match_duration_sec = s.match_duration_sec;
                next_match_net.max_players        = s.max_players;
                next_match_net.bot_count          = s.bot_count;
                phase = AppPhase::Match;
                cr::swallowNextClick();
            }

            // Client: when the match-start welcome arrives (cell_id != INVALID),
            // transition to Match. Done here (outside the la switch) because
            // welcomes are consumed by the transport drain above and the
            // pending welcome is the trigger.
            if (lobby_pending_match_welcome.has_value() && !lobby_is_host
                && phase == AppPhase::LocalLobby) {
                next_match_mode             = MatchMode::LocalClient;
                next_match_net              = {};
                next_match_net.host_address = local_lobby.joinTargetAddress();
                phase = AppPhase::Match;
                cr::swallowNextClick();
            }

            lobby_prev_substate = cur;
            (void)lobby_prev_substate; // currently unused; kept for future transition logic
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
            // Capture which mode this match runs under and reset to single-player for
            // the next launch so a follow-up VS-AI from the main menu doesn't keep a
            // stale LocalHost flag.
            MatchMode          mode_now = next_match_mode;
            MatchNetworkConfig net_now  = next_match_net;
            next_match_mode = MatchMode::SinglePlayer;
            next_match_net  = {};

            // If the lobby opened a live transport, pass it down so runMatch reuses
            // the connection (peers stay connected across the lobby->match boundary).
            const bool use_shared = lobby_socket_open
                                    && (mode_now == MatchMode::LocalHost
                                        || mode_now == MatchMode::LocalClient);
            cr::NetworkTransport* tport_ptr = use_shared ? &lobby_transport : nullptr;
            auto* peer_map_ptr   = use_shared ? &lobby_peer_to_player : nullptr;
            auto* name_map_ptr   = use_shared ? &lobby_known_peer_names : nullptr;
            const cr::codec::WelcomeMsg* prew_ptr =
                (use_shared && mode_now == MatchMode::LocalClient
                 && lobby_pending_match_welcome.has_value())
                    ? &lobby_pending_match_welcome.value()
                    : nullptr;

            MatchOutcome outcome = runMatch(next_match_seed, tuning, save,
                                            mode_now, net_now,
                                            tport_ptr, peer_map_ptr, name_map_ptr,
                                            prew_ptr, lobby_next_player_id, use_shared);
            ++next_match_seed; // new seed per match for variety across the session

            // Tear down lobby-level transport now that the match is done. Next
            // lobby visit starts with a fresh socket (and any stale peers will
            // need to re-join).
            closeLobbyTransport();

            if (outcome == MatchOutcome::ReturnToMenu) {
                // After a Local match we drop the user back to the lobby; after a
                // SinglePlayer match they go all the way back to the main menu.
                phase = (mode_now == MatchMode::SinglePlayer)
                            ? AppPhase::Menu
                            : AppPhase::LocalLobby;
                if (phase == AppPhase::LocalLobby) {
                    local_lobby.reset();
                }
                // The exit from match was triggered by a MAIN MENU button click. The
                // mouse-release event is still "fresh" for raylib this frame; swallow
                // it so it doesn't leak into a button under the cursor in the new
                // screen.
                cr::swallowNextClick();
            } else {
                phase = AppPhase::Quit;
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
    bool        headless      = false;
    int         total_ticks   = 5000;
    uint64_t    seed          = 42;
    bool        seed_explicit = false; // user passed --seed?
    std::string replay_save;
    std::string replay_load;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--ticks" && i + 1 < argc) {
            total_ticks = std::atoi(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            seed          = std::strtoull(argv[++i], nullptr, 10);
            seed_explicit = true;
        } else if (arg == "--replay-save" && i + 1 < argc) {
            replay_save = argv[++i];
        } else if (arg == "--replay-load" && i + 1 < argc) {
            replay_load = argv[++i];
        }
    }

    if (!replay_load.empty()) return runReplayHeadless(replay_load);
    if (headless)             return runHeadless(seed, total_ticks, replay_save);

    // GUI launch (runWindow): if the user didn't pin a specific seed, draw a
    // fresh one from the wall clock so every launch produces a different
    // world layout (food spawn, viruses, blackholes, wormholes, geysers).
    // Otherwise the first match of every launch is byte-identical -- which
    // was the bug behind "wormholes always spawn in the same place".
    // Headless / replay paths keep their deterministic seed so the
    // determinism test + replay round-trip aren't disturbed.
    if (!seed_explicit) {
        const auto now_ns = std::chrono::system_clock::now().time_since_epoch().count();
        seed = static_cast<uint64_t>(now_ns);
        // Sprinkle in argv-derived entropy so launches in the same wall
        // clock tick still differ across builds / CI clones.
        for (int i = 0; i < argc; ++i) {
            for (const char* p = argv[i]; *p; ++p) {
                seed = (seed * 1099511628211ull) ^ static_cast<uint8_t>(*p);
            }
        }
        std::printf("[cell_royale] randomised match seed: %llu\n",
                    static_cast<unsigned long long>(seed));
    }
    return runWindow(seed);
}
