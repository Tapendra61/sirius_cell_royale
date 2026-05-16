#include "Client.h"

#include "UiWidgets.h"
#include "ai/BotPersonality.h" // letterForTag
#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <variant>

namespace cr {

namespace {
// Quadratic XP curve. L1 = 0, L2 = 100, L3 = 400, L4 = 900, L5 = 1600, L10 = 8100.
// Each level takes more XP than the last so progression slows naturally.
int xpRequired(int level) {
    if (level <= 1) return 0;
    int delta = level - 1;
    return delta * delta * 100;
}
} // namespace

Client::Client(EntityId watched_cell, PlayerId watched_player)
    : watched_cell_(watched_cell),
      watched_player_(watched_player),
      particles_(4096) {
    run_start_sec_ = GetTime();
    // Renderer holds a process-wide player-name table that survives across
    // matches; wipe it at construction so the previous match's names don't
    // leak in on the next one.
    cr::clearPlayerNames();
    // Procedural ambient music is intentionally NOT auto-started -- the synthesised
    // pad sounds buzzy; wire in a real music asset and call audio_.playMusic() to
    // turn it back on. The dev console `music_on` command will also start it for
    // manual testing.
}

Client::~Client() {
    // raylib's GPU resources can only be unloaded while the window/GL context is alive.
    // If main called CloseWindow() before we destruct, skip and let the OS reclaim.
    if (world_rt_.id != 0 && IsWindowReady()) {
        UnloadRenderTexture(world_rt_);
    }
}

void Client::pollFrame(int screen_w, int screen_h, Tick current_tick) {
    const bool was_open = console_.isOpen();
    console_.poll();
    if (console_.isOpen()) return;
    if (was_open) return;

    Camera2D   cam = camera_.toCamera2D(screen_w, screen_h);
    InputState s   = pollInput(cam, screen_w, screen_h, input_config_);

    // ---- Phase-specific input (death/summary swallow gameplay actions) ----
    if (phase_ == GamePhase::DeathCam) {
        bool skip = s.splitPressed || s.dashPressed || s.ejectPressed || s.pausePressed
                 || IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        if (skip) death_cam_.remaining = 0.0f; // cuts to Summary next updateFrame
        return;
    }
    if (phase_ == GamePhase::Summary) {
        // SPACE is the keyboard shortcut for "play again now" (documented in the
        // Hud hint). We deliberately DON'T trip on mouse-press here -- the player
        // is clicking a button (PLAY AGAIN or MAIN MENU); the button's own click
        // handler will fire on release. Tripping on press would cannibalise the
        // MAIN MENU click and respawn before they ever release.
        if (s.splitPressed) summary_remaining_ = 0.0f;
        return;
    }
    if (phase_ == GamePhase::Respawning) {
        return; // waiting for main to spawn the cell
    }
    if (phase_ == GamePhase::MatchEnd) {
        // Match is over. Winner overlay has focus; the auto-return countdown
        // is the only path forward. Mouse-click skips the rest of the
        // countdown so impatient players don't have to wait.
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
            || IsKeyPressed(KEY_SPACE)
            || IsKeyPressed(KEY_ENTER)) {
            match_end_remaining_sec_ = 0.0f;
        }
        return;
    }

    // ---- Playing phase ----
    // Esc opens the pause overlay in every mode. In single-player the sim also
    // freezes (effectiveDtMultiplier returns 0). In multiplayer the sim KEEPS
    // ticking -- pausing the host stops snapshots for everyone; pausing a
    // client just diverges their view; neither is desirable. So in MP the
    // overlay is "just a menu" that gates LOCAL input + offers a Disconnect,
    // not a freeze. Implementation: the paused_ flag still flips here in MP,
    // but effectiveDtMultiplier ignores it (see Client::effectiveDtMultiplier).
    if (s.pausePressed) {
        togglePause();
        return;
    }
    if (paused_) return;

    if (s.moveActive) {
        queued_.push_back(Command{watched_player_, current_tick, MoveCmd{s.worldMoveTarget}});
    }
    if (s.splitPressed) {
        queued_.push_back(Command{watched_player_, current_tick, SplitCmd{}});
        audio_.playSplit();
    }
    if (s.ejectPressed) {
        queued_.push_back(Command{watched_player_, current_tick, EjectCmd{}});
        audio_.playEject();
    }
    if (s.dashPressed) {
        queued_.push_back(Command{watched_player_, current_tick, DashCmd{}});
        audio_.playDash();
    }
    if (s.blastPressed) {
        // The sim's doBlast handles min-mass + cooldown gating; we just queue. The
        // BlastEvent it emits drives the local feel layer (particles / shake / audio).
        queued_.push_back(Command{watched_player_, current_tick, BlastCmd{}});
    }
}

std::vector<Command> Client::takeCommands() {
    std::vector<Command> out;
    out.swap(queued_);
    return out;
}

float Client::effectiveDtMultiplier() const {
    // Pause-freeze only applies in single-player. Multiplayer's "pause" is
    // really an overlay -- the sim must keep ticking so the host produces
    // snapshots and clients keep draining their inbound queue. auto_paused_
    // (focus loss) is itself already gated to SP at the runMatch level, so
    // we don't need to special-case it here -- but we keep the check for
    // robustness in case that gating ever changes.
    if (!multiplayer_active_ && (paused_ || auto_paused_)) return 0.0f;
    float m = dt_mult_;
    if (death_cam_.active) m *= 0.3f;
    return m;
}

void Client::applyLoadedSave(const SaveData& s) {
    total_xp_        = static_cast<int>(s.total_xp);
    level_           = static_cast<int>(s.level);
    games_played_    = static_cast<int>(s.games_played);
    best_mass_ever_  = s.best_mass;
    best_combo_ever_ = static_cast<int>(s.best_combo);
    audio_.setMasterVolume(s.master_volume);
    audio_.setSfxVolume(s.sfx_volume);
    audio_.setMusicVolume(s.music_volume);
    audio_.setMusicEnabled(s.music_enabled);
    input_config_.hold_to_move  = s.hold_to_move;
    input_config_.invert_thumbs = s.invert_thumbs;
    // Daily missions. Progress is per-match -- reset to 0 here so the new match starts
    // tracking fresh. Completed flag persists from save so already-cleared missions
    // stay cleared for the rest of the day.
    last_mission_reset_day_ = s.last_mission_reset_day;
    for (int i = 0; i < kMissionCount; ++i) {
        missions_[i]          = s.daily_missions[i];
        missions_[i].progress = 0;
    }
    match_food_eaten_   = 0;
    match_crits_landed_ = 0;
    match_peak_mass_    = 0.0f;
    match_mission_xp_   = 0;
    // v3 accessibility settings: push to the systems that respect them.
    shake_.setScale(s.screen_shake_scale);
    setPaletteMode(static_cast<PaletteMode>(
        s.colorblind_mode <= 3 ? s.colorblind_mode : 0));
    setHighContrast(s.high_contrast);
    // FPS cap is applied by main.cpp at startup / the Settings live preview; we just
    // remember the current value so snapshotForSave() can round-trip it.
    fps_cap_ = s.fps_cap;
    // v4 -- HUD text size accessibility multiplier + first-run flag passthrough.
    setHudTextScale(s.hud_text_scale);
    first_run_complete_ = s.first_run_complete;
    // v5 -- register the watched player's name so killfeed / leaderboard /
    // nameplates show it immediately in SinglePlayer. Multiplayer hosts also
    // ship their own name to peers via the network welcome flow.
    if (!s.player_name.empty()) {
        setPlayerName(watched_player_, s.player_name);
    }
}

SaveData Client::snapshotForSave() const {
    SaveData s;
    // v1 fields
    s.total_xp      = static_cast<uint32_t>(std::max(0, total_xp_));
    s.level         = static_cast<uint32_t>(std::max(1, level_));
    s.games_played  = static_cast<uint32_t>(std::max(0, games_played_));
    s.best_mass     = best_mass_ever_;
    s.best_combo    = static_cast<uint32_t>(std::max(0, best_combo_ever_));
    s.master_volume = audio_.masterVolume();
    s.sfx_volume    = audio_.sfxVolume();
    s.music_volume  = audio_.musicVolume();
    s.hold_to_move  = input_config_.hold_to_move;
    s.invert_thumbs = input_config_.invert_thumbs;
    // v2 fields
    s.music_enabled = audio_.isMusicEnabled();
    s.last_mission_reset_day = last_mission_reset_day_;
    for (int i = 0; i < kMissionCount; ++i) {
        s.daily_missions[i]          = missions_[i];
        s.daily_missions[i].progress = 0; // progress is per-match -- never persisted
    }
    // v3 fields. These were silently dropped before this fix: snapshotForSave was
    // returning struct-default zeros for them, so accessibility settings appeared to
    // "revert to default" after every match.
    s.screen_shake_scale = shake_.scale();
    s.colorblind_mode    = static_cast<uint8_t>(currentPaletteMode());
    s.high_contrast      = currentHighContrast();
    s.fps_cap            = fps_cap_;
    // v4 fields. hud_text_scale is read from the UiWidgets global where it lives;
    // first_run_complete is owned by main.cpp's outer save and only touched there
    // (we round-trip it through Client by storing/returning the same value).
    s.hud_text_scale     = currentHudTextScale();
    s.first_run_complete = first_run_complete_;
    // v5 -- player name. Pulled from the registry so a name set in Settings (or
    // typed at the lobby) persists across matches. The watched player's slot
    // is the source of truth here.
    auto it = player_names_.find(watched_player_);
    if (it != player_names_.end()) s.player_name = it->second;
    return s;
}

void Client::setPlayerName(PlayerId pid, const std::string& name) {
    if (name.empty()) {
        player_names_.erase(pid);
    } else {
        player_names_[pid] = name;
    }
    // Mirror into the Renderer's process-wide table so cell nameplates pick
    // up the name without any extra plumbing through drawWorld.
    cr::setPlayerName(pid, name.c_str());
}

void Client::clearPlayerNames() {
    player_names_.clear();
    cr::clearPlayerNames();
}

std::string Client::playerLabel(PlayerId pid, uint8_t personality_tag,
                                size_t max_len) const {
    auto it = player_names_.find(pid);
    if (it != player_names_.end() && !it->second.empty()) {
        // Truncate if longer than max_len so tight HUD elements (killfeed,
        // nameplate) don't overflow.
        if (it->second.size() <= max_len) return it->second;
        return it->second.substr(0, max_len);
    }
    // Fallback: <letter><id> matches the existing display in every HUD
    // element that doesn't know a name (pre-name behaviour).
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%c%u",
                  ai::letterForTag(personality_tag),
                  static_cast<unsigned>(pid));
    return std::string(buf);
}

void Client::onEvents(const std::vector<GameEvent>& events, World& world,
                      const Tuning& tuning) {
    const double now_sec = GetTime();

    // Audibility gate for non-player events. The world is 16k x 16k with ~50 bots
    // constantly eating golden food / each other; without a distance filter the player
    // hears phantom chomps and virus pops happening far off-screen. Radius scales with
    // zoom so it grows as the player grows (and sees more).
    const Vec2  cam_pos      = camera_.position();
    const float zoom         = std::max(camera_.zoom(), 0.05f);
    const float audible_r    = 1500.0f / zoom;
    const float audible_r_sq = audible_r * audible_r;
    auto within_earshot = [&](Vec2 p) {
        const float dx = p.x - cam_pos.x;
        const float dy = p.y - cam_pos.y;
        return dx * dx + dy * dy <= audible_r_sq;
    };

    // Cell-by-id lookup that works in any mode. SinglePlayer / LocalHost: the
    // local sim's world has the cell. LocalClient: the local world is empty so
    // findCell returns null; fall back to the latest snapshot which carries all
    // cells the host knows about. Without this fallback, every is_player check
    // returned false on a client (because pred = nullptr), which silently gated
    // out all the audio paths that distinguish "I ate" from "someone else ate"
    // (e.g. eating common food = no sound at all).
    struct CellRef {
        PlayerId owner = INVALID_PLAYER;
        Vec2     pos{0.0f, 0.0f};
        float    mass = 0.0f;
        bool     found = false;
    };
    auto lookupCell = [&](EntityId id) -> CellRef {
        CellRef r;
        if (const Cell* c = world.findCell(id)) {
            r.owner = c->owner;
            r.pos   = c->pos;
            r.mass  = c->mass;
            r.found = true;
            return r;
        }
        if (interp_.hasCurr()) {
            for (const auto& cs : interp_.curr().cells) {
                if (cs.id == id) {
                    r.owner = cs.owner;
                    r.pos   = cs.pos;
                    r.mass  = cs.mass;
                    r.found = true;
                    return r;
                }
            }
        }
        return r;
    };

    // ReachMass mission: tracks the player's largest total mass during this match.
    // Computed once per onEvents (i.e., once per sim tick) rather than per event.
    // Uses the snapshot when the local world is empty (LocalClient) so the
    // mission still progresses for the client's own player.
    float player_total_mass = 0.0f;
    if (!world.cells().empty()) {
        for (const auto& c : world.cells()) {
            if (c.owner == watched_player_) player_total_mass += c.mass;
        }
    } else if (interp_.hasCurr()) {
        for (const auto& cs : interp_.curr().cells) {
            if (cs.owner == watched_player_) player_total_mass += cs.mass;
        }
    }
    if (player_total_mass > match_peak_mass_) {
        match_peak_mass_ = player_total_mass;
        bumpMissionProgress(MissionKind::ReachMass,
                            static_cast<int>(match_peak_mass_));
    }

    for (const auto& ev : events) {
        std::visit([&](auto&& e) {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, AbsorbEvent>) {
                CellRef pred  = lookupCell(e.predator);
                Color   color = pred.found ? colorForPlayer(pred.owner) : RAYWHITE;
                particles_.spawnAbsorbBurst(e.at, e.mass_gained, color);

                // Skip "+1" food micro-popups (would spam) but show pellets / cell absorbs.
                if (e.mass_gained >= 5.0f) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "+%d", static_cast<int>(e.mass_gained));
                    popups_.spawn(e.at, buf, color);
                }

                // Player-specific: combo, hitstop, screen shake. Trauma is *only* added
                // for player absorbs -- otherwise 50 bots all eating food at start of
                // match pin the screen-shake to max for the entire opening frenzy.
                const bool is_player = pred.found && pred.owner == watched_player_;
                if (is_player) {
                    shake_.addTrauma(std::min(0.5f, e.mass_gained * 0.012f));
                    hud_.onPlayerAbsorb(e.mass_gained, now_sec, tuning.combo_window_sec);
                    if (e.mass_gained >= tuning.hitstop_threshold_mass) {
                        hitstop_.trigger(tuning.hitstop_duration_sec);
                        // Same threshold as hitstop -- the meaty-eat moment also gets a
                        // brief RGB fringe pulse to reinforce "important moment."
                        chroma_.addShift(0.35f);
                    }
                }
                // Player chomps always play; bot chomps only if visible/audible. Without
                // this, the player hears chomps from off-screen bot vs bot eats across
                // the entire 16k world.
                if (is_player || within_earshot(e.at)) {
                    audio_.playAbsorb(e.mass_gained, hud_.combo(), is_player);
                }
                // Mission progress: distinguish food eats from cell absorbs by mass.
                // Food caps at ~30 (gold) and cells start at 100, so 50 is a safe split.
                if (is_player && e.mass_gained < 50.0f) {
                    ++match_food_eaten_;
                    bumpMissionProgress(MissionKind::EatFood, match_food_eaten_);
                }
            } else if constexpr (std::is_same_v<T, DeathEvent>) {
                // Killfeed entry first -- show every inter-player kill so the world
                // feels populated; player-involved kills get a gold bracket accent
                // (set via involves_player) so they stand out among bot-vs-bot rows.
                {
                    // playerLabel() prefers the registered display name; falls
                    // back to "<letter><id>" when none is known. Truncate to
                    // 16 chars so longer names don't overflow the killfeed
                    // backdrop strip.
                    auto fmtName = [this](char* buf, size_t n, PlayerId pid, uint8_t tag) {
                        std::string label = playerLabel(pid, tag, /*max_len=*/16);
                        std::snprintf(buf, n, "%s", label.c_str());
                    };
                    const bool involves_player =
                        (e.predator_player == watched_player_)
                     || (e.player          == watched_player_);
                    KillfeedEntry kf;
                    if (e.predator_player == INVALID_PLAYER) {
                        // Environmental death (e.g. crashing comet). Show a distinct
                        // label + a hot orange tint instead of a per-player color.
                        std::snprintf(kf.predator, sizeof(kf.predator), "COMET");
                        kf.pred_color = Color{255, 140, 60, 255};
                    } else {
                        fmtName(kf.predator, sizeof(kf.predator),
                                e.predator_player, e.predator_personality);
                        kf.pred_color = colorForPlayer(e.predator_player);
                    }
                    fmtName(kf.prey, sizeof(kf.prey),
                            e.player, e.prey_personality);
                    kf.prey_color      = colorForPlayer(e.player);
                    kf.spawned_sec     = now_sec;
                    kf.involves_player = involves_player;
                    hud_.pushKillfeed(kf);
                }

                // e.player is the OWNER of the cell that just died. DeathEvent fires
                // PER CELL, not per player -- with multi-cell splits the player can
                // lose one piece and still be in the match. Only treat it as a real
                // death when no player cells remain in the world.
                if (e.player != watched_player_) return;

                // "Are any of my cells still alive?" -- consult world if present,
                // otherwise scan the snapshot (LocalClient path). DeathEvent fires
                // for the cell that just died, so the host's next snapshot reflects
                // its removal; that's the snap we read here. NB: this is also why
                // the partial-death "lose a piece" path below shows a small burst
                // only when at least one of our cells survives.
                auto playerAlive = [&]() -> bool {
                    if (!world.cells().empty()) {
                        for (const auto& c : world.cells()) {
                            if (c.owner == watched_player_) return true;
                        }
                        return false;
                    }
                    if (interp_.hasCurr()) {
                        for (const auto& cs : interp_.curr().cells) {
                            if (cs.owner == watched_player_) return true;
                        }
                    }
                    return false;
                };
                if (playerAlive()) {
                    // Lost a piece but the run continues. Lightweight feedback.
                    CellRef killed = lookupCell(e.by);
                    if (killed.found) {
                        particles_.spawnDeathBurst(killed.pos, 60.0f,
                                                   colorForPlayer(watched_player_));
                    }
                    shake_.addTrauma(0.35f);
                    return;
                }

                // Full death: all player cells are gone. Phase 8 run-end sequence.
                Vec2  pos{0.0f, 0.0f};
                float mass = 100.0f;
                CellRef dead = lookupCell(watched_cell_);
                if (dead.found) {
                    pos  = dead.pos;
                    mass = dead.mass;
                }
                particles_.spawnDeathBurst(pos, mass, colorForPlayer(watched_player_));
                shake_.addTrauma(0.9f);
                hud_.onPlayerDeath();
                audio_.playPlayerDeath();
                death_cam_.active    = true;
                death_cam_.target    = e.by;
                death_cam_.remaining = DeathCam::kDuration;

                // Summary uses PEAK mass during the run (already tracked for the
                // ReachMass mission) rather than the mass of the last piece to die,
                // which would always understate how big the player got.
                phase_                   = GamePhase::DeathCam;
                summary_.final_mass      = static_cast<int>(match_peak_mass_);
                summary_.best_combo      = hud_.bestCombo();
                summary_.time_alive_sec  = static_cast<float>(now_sec - run_start_sec_);
            } else if constexpr (std::is_same_v<T, RecombineEvent>) {
                // Same-owner merge. Soft inward implosion + audio for the
                // player; light shake/chroma so the merge lands as a
                // tactile beat without disrupting input. Earshot-gated for
                // non-watched players so 50 bots auto-recombining off-
                // screen doesn't carpet-bomb the mix.
                const Color color = colorForPlayer(e.player);
                particles_.spawnRecombineRing(e.at, e.new_mass, color);
                const bool is_player = (e.player == watched_player_);
                const bool audible   = is_player || within_earshot(e.at);
                if (audible) {
                    // Reuse the single-absorb sound -- it has the right
                    // "thunk + settle" character for a merge. Combo count
                    // 0 -> no pitch-up, by_player=is_player gates volume.
                    audio_.playAbsorb(e.new_mass * 0.5f, 0, is_player);
                }
                if (is_player) {
                    // Trauma scaled by mass so a 200-mass merge is barely a
                    // nudge but an 8000-mass mega-merge has presence.
                    const float t = std::clamp(0.05f + e.new_mass / 14000.0f,
                                               0.05f, 0.22f);
                    shake_.addTrauma(t);
                    chroma_.addShift(0.10f);
                }
            } else if constexpr (std::is_same_v<T, SplitEvent>) {
                CellRef from = lookupCell(e.from);
                if (from.found) {
                    particles_.spawnSplitPuff(from.pos, colorForPlayer(e.player));
                }
                // SplitEvents with INVALID_ENTITY child indicate a virus explosion.
                // Player virus pops always audible; bot virus pops only if visible --
                // otherwise 60 viruses + 50 bots produce a constant pop fog from off-screen.
                const bool is_player    = (e.player == watched_player_);
                const bool is_virus_pop = (e.into == INVALID_ENTITY);
                const bool audible      = is_player
                                          || (from.found && within_earshot(from.pos));
                if (is_virus_pop) {
                    if (audible) audio_.playVirusPop();
                    // Virus pops are violent -- give them a stronger extra
                    // burst on top of the standard puff, scaled by mass so a
                    // 5000-mass pop looks bigger than a 500-mass one. Player
                    // virus pops also shake the screen briefly (modest
                    // trauma -- they're more common than blasts).
                    if (from.found) {
                        particles_.spawnAbsorbBurst(from.pos, 60.0f,
                                                    colorForPlayer(e.player));
                    }
                    if (is_player) {
                        shake_.addTrauma(0.30f);
                        chroma_.addShift(0.30f);
                    }
                } else {
                    // Manual (Space-key) split. Audio + light shake/chroma
                    // for the player's own splits so the action feels
                    // responsive; multi-cell splits stack the trauma but
                    // the per-event amount is small enough that even a
                    // quad-split caps below the blast threshold.
                    if (audible) audio_.playSplit();
                    if (is_player) {
                        shake_.addTrauma(0.14f);
                        chroma_.addShift(0.12f);
                    }
                }
            } else if constexpr (std::is_same_v<T, NearMissEvent>) {
                // hunter is the bigger cell that couldn't quite eat.
                CellRef hunter = lookupCell(e.hunter);
                CellRef prey   = lookupCell(e.prey);
                bool    player_involved = false;
                if (hunter.found && hunter.owner == watched_player_) {
                    hud_.onPlayerNearMissPrey();
                    particles_.spawnNearMissSparks(e.at, Color{255, 220, 80, 255});
                    player_involved = true;
                }
                if (prey.found && prey.owner == watched_player_) {
                    hud_.onPlayerNearMissAttacker();
                    shake_.addTrauma(0.25f);
                    particles_.spawnNearMissSparks(e.at, Color{255, 80, 80, 255});
                    player_involved = true;
                }
                if (player_involved) audio_.playNearMiss();
            } else if constexpr (std::is_same_v<T, CritEvent>) {
                // CritEvent fires AFTER AbsorbEvent for the same eat, so the regular
                // burst / popup / combo / chomp have already happened. This is the
                // "slot-machine payout" layer on top. Trauma only fires for player crits
                // (same reason as AbsorbEvent -- bot crits would shake too much).
                CellRef pred = lookupCell(e.predator);
                const bool is_player = pred.found && pred.owner == watched_player_;
                particles_.spawnAbsorbBurst(e.at, e.mass_gained * 0.6f,
                                            Color{255, 220, 60, 255});
                popups_.spawn(e.at, "CRIT!", Color{255, 230, 80, 255}, 32.0f);
                if (is_player) {
                    shake_.addTrauma(0.5f);
                    hud_.onCrit();
                    audio_.playCrit();
                    chroma_.addShift(0.6f);
                    ++match_crits_landed_;
                    bumpMissionProgress(MissionKind::LandCrits, match_crits_landed_);
                }
            } else if constexpr (std::is_same_v<T, BlastEvent>) {
                // Shockwave from the 4th ability. Big particle burst + ring at the
                // blast origin, screen shake + chroma flash if it's the player's blast.
                const Color color = colorForPlayer(e.player);
                particles_.spawnBlastBurst(e.at, e.radius, color);
                if (e.player == watched_player_) {
                    shake_.addTrauma(0.6f);
                    chroma_.addShift(0.45f);
                    audio_.playBlast();
                    popups_.spawn(e.at, "BLAST!", color, 28.0f);
                } else if (within_earshot(e.at)) {
                    // Enemy blast nearby -- audible but no shake.
                    audio_.playBlast();
                }
            } else if constexpr (std::is_same_v<T, PickupCollectedEvent>) {
                // Visual + audio confirmation, only for the watched player so the
                // world isn't littered with toast popups from 50 bots also grabbing
                // pickups. Bots get the effect but no player-facing notification.
                if (e.player != watched_player_) return;

                const char* label = "PICKUP";
                Color       hue   = Color{255, 255, 255, 255};
                switch (e.kind) {
                    case PickupKind::Shield:
                        label = "SHIELD!"; hue = Color{120, 220, 255, 255}; break;
                    case PickupKind::Magnet:
                        label = "MAGNET!"; hue = Color{255, 170,  80, 255}; break;
                    case PickupKind::Stealth:
                        label = "STEALTH!"; hue = Color{200, 130, 240, 255}; break;
                    case PickupKind::None: return;
                }
                particles_.spawnAbsorbBurst(e.at, 40.0f, hue);
                popups_.spawn(e.at, label, hue, 26.0f);
                // Reuse the crit sting for now -- distinct enough to feel rewarding
                // without needing a new procedural sound.
                audio_.playCrit();
                chroma_.addShift(0.25f);
            } else if constexpr (std::is_same_v<T, CometEvent>) {
                // World event: a crashing comet sweeps across the map. Three phases
                // signal different feedback. Telegraph: warning sound + HUD banner.
                // Active: heavier roar + brief chroma flash. Despawn: no-op (the
                // particles already faded).
                switch (e.phase) {
                    case CometEvent::Telegraph:
                        audio_.playCometWarn();
                        comet_banner_remaining_ = kCometBannerSec;
                        break;
                    case CometEvent::Active:
                        audio_.playCometStrike();
                        chroma_.addShift(0.30f);
                        // Tiny screen shake to sell the impact moment world-wide.
                        shake_.addTrauma(0.20f);
                        break;
                    case CometEvent::Despawn:
                        break;
                }
            } else if constexpr (std::is_same_v<T, MatchEndEvent>) {
                // Match clock hit zero. Capture the winner info so the
                // overlay can render it, transition to MatchEnd phase, and
                // start the auto-return countdown. Only fires once -- the
                // sim emits MatchEndEvent exactly once per match.
                if (!match_end_active_) {
                    // Look up the winner's personality_tag from the latest
                    // snapshot so the fallback label (when they don't have
                    // a registered name) shows the right letter for a bot
                    // winner ("H7", "A3", etc.) instead of the human "P".
                    uint8_t winner_tag = 0;
                    if (interp_.hasCurr()) {
                        for (const auto& cs : interp_.curr().cells) {
                            if (cs.owner == e.winner_player) {
                                winner_tag = cs.personality_tag;
                                break;
                            }
                        }
                    }
                    match_end_active_        = true;
                    match_end_winner_        = e.winner_player;
                    match_end_winner_mass_   = e.winner_mass;
                    match_end_winner_name_   = playerLabel(e.winner_player,
                                                            winner_tag,
                                                            /*max_len=*/16);
                    match_end_remaining_sec_ = kMatchEndOverlaySec;
                    phase_                   = GamePhase::MatchEnd;
                    // Stop any active pause overlay -- the match-end takes
                    // focus, and the world is winding down for everyone.
                    paused_ = false;
                }
            }
        }, ev);
    }
}

void Client::updateFrame(float frame_dt, double now_sec, const Tuning& tuning) {
    shake_.update(frame_dt);
    hitstop_.update(frame_dt);
    particles_.update(frame_dt);
    popups_.update(frame_dt);
    chroma_.update(frame_dt);
    hud_.update(frame_dt, now_sec, tuning.combo_window_sec);
    audio_.update();

    // Crashing-comet banner countdown. Pure visual; doesn't affect the sim.
    if (comet_banner_remaining_ > 0.0f) {
        comet_banner_remaining_ = std::max(0.0f, comet_banner_remaining_ - frame_dt);
    }

    // Match-end overlay countdown. After the configured display window the
    // client requests a return to the menu (the outer loop's existing flow
    // routes that to MainMenu for SP or LocalLobby for MP). Idempotent --
    // setting the flag multiple times is a no-op until the outer loop
    // consumes it.
    if (match_end_active_ && match_end_remaining_sec_ > 0.0f) {
        match_end_remaining_sec_ =
            std::max(0.0f, match_end_remaining_sec_ - frame_dt);
        if (match_end_remaining_sec_ <= 0.0f) {
            return_to_menu_pending_ = true;
        }
    }

    // Continuous black-hole bubble particle spray. Each frame, each hole emits one
    // dark-red bubble at a random point on its pull-ring edge with velocity toward
    // the centre. Purely visual -- particles_ has its own RNG so this doesn't touch
    // sim determinism.
    if (interp_.hasCurr()) {
        for (const auto& b : interp_.curr().blackholes) {
            particles_.spawnBlackHoleBubble(b.pos, b.pull_radius);
        }
        // Active-comet ember spray. The shader trail draws the long smooth streak;
        // this layer adds 6 fiery bubble particles per frame at the comet's surface
        // so the body of the fireball reads as actively shedding sparks. Skipped
        // during the telegraph window (the comet hasn't begun travelling yet).
        for (const auto& cm : interp_.curr().comets) {
            if (cm.telegraph_norm < 1.0f) continue;
            particles_.spawnCometEmber(cm.pos, cm.vel, cm.radius);
        }
    }

    if (death_cam_.active) {
        death_cam_.remaining -= frame_dt;
        if (death_cam_.remaining <= 0.0f) {
            death_cam_.active = false;
        }
    }

    // Duck the ambient music while the death cam is up, then restore. The roadmap
    // calls for 30% volume during death cam; otherwise full.
    audio_.setMusicDuck(death_cam_.active ? 0.3f : 1.0f);

    // SurviveSec mission progress is just time alive in this match (until death).
    if (phase_ == GamePhase::Playing) {
        int seconds_alive = static_cast<int>(now_sec - run_start_sec_);
        if (seconds_alive > 0) {
            bumpMissionProgress(MissionKind::SurviveSec, seconds_alive);
        }
    }

    // ---- Game phase transitions (Phase 8 restart loop) ----
    if (phase_ == GamePhase::DeathCam && !death_cam_.active) {
        // Compute XP, apply level-ups, build the summary view. Mission XP earned this
        // run is folded in here (rather than bumped directly during mission completion)
        // so the panel's "+X XP" line includes it.
        int xp = std::max(0, summary_.final_mass - 100) / 10
               + summary_.best_combo * 5
               + static_cast<int>(summary_.time_alive_sec * 0.1f)
               + match_mission_xp_;
        xp = std::min(xp, 1500);
        summary_.xp_earned   = xp;
        summary_.level_before = level_;
        total_xp_           += xp;
        while (total_xp_ >= xpRequired(level_ + 1)) ++level_;
        summary_.level_after          = level_;
        summary_.total_xp             = total_xp_;
        summary_.xp_for_current_level = xpRequired(level_);
        summary_.xp_for_next_level    = xpRequired(level_ + 1);
        summary_remaining_            = 3.0f;
        phase_                        = GamePhase::Summary;
        // Snapshot today's missions so the Summary panel can render them without
        // grabbing them from the live Client state.
        for (int i = 0; i < kMissionCount; ++i) {
            summary_.missions[i] = missions_[i];
        }
        // Update persistent lifetime stats so they survive a crash via the next save.
        if (summary_.final_mass > static_cast<int>(best_mass_ever_)) {
            best_mass_ever_ = static_cast<float>(summary_.final_mass);
        }
        if (summary_.best_combo > best_combo_ever_) {
            best_combo_ever_ = summary_.best_combo;
        }
        ++games_played_;
    } else if (phase_ == GamePhase::Summary) {
        // Summary phase no longer auto-transitions. The player took the time to die;
        // give them time to read the stats. The transition to Respawning only fires
        // when something explicitly sets summary_remaining_ to 0 -- the PLAY AGAIN
        // button (Hud.cpp) or the SPACE / mouse-click shortcut (pollFrame).
        summary_.remaining_sec = summary_remaining_;
        if (summary_remaining_ <= 0.0f) {
            phase_           = GamePhase::Respawning;
            respawn_pending_ = true;
        }
    }
}

bool Client::consumeRespawnRequest() {
    if (!respawn_pending_) return false;
    respawn_pending_ = false;
    return true;
}

bool Client::consumeReturnToMenuRequest() {
    if (!return_to_menu_pending_) return false;
    return_to_menu_pending_ = false;
    return true;
}

void Client::bumpMissionProgress(MissionKind kind, int new_progress) {
    for (auto& m : missions_) {
        if (m.kind != kind || m.completed) continue;
        if (new_progress <= m.progress) continue;
        m.progress = std::min(new_progress, m.target);
        if (m.progress >= m.target) {
            m.completed = true;
            // Accumulate the reward into a per-match bucket; folded into xp_earned
            // (and thus total_xp_) at the DeathCam -> Summary transition so the player
            // visibly sees the mission XP on the panel.
            match_mission_xp_ += kMissionXpReward;
            // Float a mission-complete toast near the camera; it lands in world space
            // but the camera is centered on the player so it appears at the player.
            char goal_buf[80];
            char popup_buf[128];
            std::snprintf(popup_buf, sizeof(popup_buf),
                          "MISSION!  %s",
                          missionGoalText(m, goal_buf, sizeof(goal_buf)));
            popups_.spawn(camera_.position(), popup_buf,
                          Color{255, 220, 80, 255}, 28.0f);
        }
    }
}

void Client::onPlayerRespawned(double now_sec) {
    phase_         = GamePhase::Playing;
    run_start_sec_ = now_sec;
    hud_.onPlayerRespawn();
    summary_ = MatchSummary{};
}

void Client::render(int screen_w, int screen_h, float alpha, const Tuning& tuning,
                    const World& world, Tick tick) {
    // Camera + screen shake (shake is added to target so the whole scene jiggles).
    // screen_w/h come from GetScreenWidth/GetScreenHeight which return logical points
    // (raylib's macOS HighDPI handling auto-maps logical coords to the pixel framebuffer
    // via its ortho projection, so we don't need to scale here ourselves).
    Camera2D cam = camera_.toCamera2D(screen_w, screen_h);
    Vec2 shake = shake_.sampleOffset(tuning.screen_shake_max);
    cam.target.x += shake.x;
    cam.target.y += shake.y;

    // World-space view AABB for renderer-side frustum culling. At zoom 1 with 1280x720
    // that covers ~921k px² of a 16000² world; most entities won't be visible.
    const float inv_zoom    = (cam.zoom > 0.0001f) ? 1.0f / cam.zoom : 1.0f;
    const float view_half_w = static_cast<float>(screen_w) * 0.5f * inv_zoom;
    const float view_half_h = static_cast<float>(screen_h) * 0.5f * inv_zoom;
    constexpr float kCullMargin = 64.0f;
    const Vec2 view_min{cam.target.x - view_half_w - kCullMargin,
                        cam.target.y - view_half_h - kCullMargin};
    const Vec2 view_max{cam.target.x + view_half_w + kCullMargin,
                        cam.target.y + view_half_h + kCullMargin};

    if (chroma_.active()) {
        // Off-screen pass: render the world layer to a texture that contains the bg
        // color baked in, then composite to the backbuffer in 3 additive draws (R/G/B
        // channels) with horizontal offset. The sum of the 3 tinted draws at zero offset
        // reproduces the original color; at the channel offsets, edges fringe red/blue.
        if (world_rt_.id == 0 || world_rt_w_ != screen_w || world_rt_h_ != screen_h) {
            if (world_rt_.id != 0) UnloadRenderTexture(world_rt_);
            world_rt_   = LoadRenderTexture(screen_w, screen_h);
            world_rt_w_ = screen_w;
            world_rt_h_ = screen_h;
        }

        BeginTextureMode(world_rt_);
        ClearBackground(Color{12, 14, 22, 255}); // matches backdrop's top tone
        // Screen-space backdrop (gradient + vignette) baked into the RT
        // before world rendering. Keeps the gradient stable as the camera
        // moves and survives the chroma channel composite below.
        renderer_.drawScreenBackdrop(screen_w, screen_h);
        BeginMode2D(cam);
        renderer_.drawWorld(interp_, tuning, alpha, view_min, view_max,
                            watched_cell_, watched_player_, level_);
        particles_.draw();
        popups_.draw();
        EndMode2D();
        EndTextureMode();

        // Backbuffer was cleared to bg by main; override to BLACK so the additive sum
        // reconstructs exact RT pixels (additive on top of bg would double-brighten).
        ClearBackground(BLACK);

        // raylib render textures are GL-flipped; pass negative height to un-flip.
        const float shift = chroma_.currentOffsetPx();
        const Rectangle src{0, 0, static_cast<float>(world_rt_.texture.width),
                                  -static_cast<float>(world_rt_.texture.height)};
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTextureRec(world_rt_.texture, src, Vector2{+shift, 0.0f},
                       Color{255,   0,   0, 255});
        DrawTextureRec(world_rt_.texture, src, Vector2{ 0.0f,  0.0f},
                       Color{  0, 255,   0, 255});
        DrawTextureRec(world_rt_.texture, src, Vector2{-shift, 0.0f},
                       Color{  0,   0, 255, 255});
        EndBlendMode();
    } else {
        // Non-chroma path: the backbuffer has already been cleared by main
        // to a flat dark color. Paint the backdrop in screen space first
        // (gradient + vignette) so the world draws on top of a framed
        // surface instead of a flat fill.
        renderer_.drawScreenBackdrop(screen_w, screen_h);
        BeginMode2D(cam);
        renderer_.drawWorld(interp_, tuning, alpha, view_min, view_max,
                            watched_cell_, watched_player_, level_);
        particles_.draw();
        popups_.draw();
        EndMode2D();
    }

    // Screen-space overlays.
    renderTouchOverlay(screen_w, screen_h, input_config_);

    // Minimap (bottom-right corner). Drawn before the HUD so the killfeed (top-right)
    // and the minimap don't collide -- they're at opposite corners but rendering order
    // makes the HUD pause/summary panels overlay on top if they ever did overlap. Only
    // drawn while there's a world to show (skip during raw startup before the first
    // snapshot).
    if (phase_ == GamePhase::Playing || phase_ == GamePhase::DeathCam) {
        renderer_.drawMinimap(interp_, world.width(), world.height(),
                              view_min, view_max,
                              screen_w, screen_h, watched_player_);
    }

    // ---- Q-ability (Mass Blast) cooldown bar, top-right corner ----
    // Reads the player's effective blast cooldown from the latest snapshot (works
    // uniformly across SinglePlayer / LocalHost / LocalClient -- the local world
    // is empty in the client path, but the snapshot always carries authoritative
    // state). The fill colour shifts red -> orange -> yellow as the bar approaches
    // ready, with a steady-state bright glow when ready. Dimmed when the player's
    // biggest cell is below the min-cast mass threshold.
    (void)tick;     // current code derives fill_norm from snapshot, not local tick
    (void)world;    // see comment above -- the snapshot is authoritative for HUD
    if (phase_ == GamePhase::Playing && interp_.hasCurr()) {
        float fill_norm   = 1.0f;
        float biggest_mass = 0.0f;
        for (const auto& cs : interp_.curr().cells) {
            if (cs.owner != watched_player_) continue;
            // Player has multiple cells: the effective cooldown is the *smallest*
            // norm (i.e. the longest-remaining cooldown) since blast can only be
            // cast from the largest cell, and any cell whose blast is still
            // counting down blocks the ability.
            fill_norm    = std::min(fill_norm, cs.blast_cooldown_norm);
            biggest_mass = std::max(biggest_mass, cs.mass);
        }
        const bool ready          = (fill_norm >= 1.0f);
        const bool below_min_mass = (biggest_mass < tuning.blast_min_mass);

        // Layout: top-right corner, clear of the combo counter and killfeed.
        const int bar_w   = 220;
        const int bar_h   = 18;
        const int margin  = 16;
        const int label_w = 24;
        const int bar_x   = screen_w - bar_w - margin;
        const int bar_y   = margin;
        const int label_x = bar_x - label_w - 4;

        // Backdrop + dark fill track. Slight inset border so the bar has a clean edge
        // against busy backgrounds.
        DrawRectangle(bar_x - 2, bar_y - 2, bar_w + 4, bar_h + 4, Color{0, 0, 0, 190});
        DrawRectangle(bar_x, bar_y, bar_w, bar_h, Color{30, 22, 30, 230});

        // Fill colour ramp: red (0%) -> orange (50%) -> yellow (90%) -> bright yellow.
        // Linearly interpolate channels so there's no banding.
        const float k = fill_norm;
        Color fill_c;
        if (k < 0.5f) {
            const float u = k / 0.5f;
            fill_c = Color{
                static_cast<unsigned char>(160 + u * 95.0f),
                static_cast<unsigned char>( 30 + u * 110.0f),
                static_cast<unsigned char>( 30 - u * 20.0f),
                240};
        } else {
            const float u = (k - 0.5f) / 0.5f;
            fill_c = Color{255,
                           static_cast<unsigned char>(140 + u * 95.0f),
                           static_cast<unsigned char>( 10 + u * 60.0f),
                           240};
        }
        if (below_min_mass) {
            // De-saturate so the bar reads as "blocked, not yet useable" rather than
            // "cooldown ticking".
            fill_c.r = static_cast<unsigned char>(fill_c.r * 0.4f);
            fill_c.g = static_cast<unsigned char>(fill_c.g * 0.4f);
            fill_c.b = static_cast<unsigned char>(fill_c.b * 0.5f);
        }
        const int fill_px = static_cast<int>(bar_w * fill_norm);
        DrawRectangle(bar_x, bar_y, fill_px, bar_h, fill_c);

        // Ready-state glow: subtle border pulse when the ability is castable. Skipped
        // when min-mass is the blocker -- the bar already looks dimmed in that case.
        if (ready && !below_min_mass) {
            const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 4.0f);
            const unsigned char glow_a = static_cast<unsigned char>(140 + pulse * 100.0f);
            DrawRectangleLinesEx(Rectangle{static_cast<float>(bar_x - 2),
                                           static_cast<float>(bar_y - 2),
                                           static_cast<float>(bar_w + 4),
                                           static_cast<float>(bar_h + 4)},
                                 2.0f, Color{255, 240, 150, glow_a});
        } else {
            DrawRectangleLinesEx(Rectangle{static_cast<float>(bar_x),
                                           static_cast<float>(bar_y),
                                           static_cast<float>(bar_w),
                                           static_cast<float>(bar_h)},
                                 1.0f, Color{200, 180, 200, 160});
        }

        // "Q" label on the left, dimmed when blocked by min mass.
        const Color label_c = below_min_mass
                                ? Color{160, 160, 160, 220}
                                : Color{255, 230, 130, 235};
        DrawText("Q", label_x, bar_y, bar_h, label_c);

        // Status text inside the bar. Centred horizontally so it doesn't get cut off
        // by the fill front edge.
        const char* status = below_min_mass ? "NEED MASS"
                                            : (ready ? "BLAST" : "");
        if (status[0] != 0) {
            const int fs   = 12;
            const int tw   = MeasureText(status, fs);
            const int tx   = bar_x + (bar_w - tw) / 2;
            const int ty   = bar_y + (bar_h - fs) / 2;
            DrawText(status, tx + 1, ty + 1, fs, Color{0, 0, 0, 180});
            DrawText(status, tx,     ty,     fs, Color{255, 255, 255, 230});
        }
    }

    // ---- Leaderboard (top-left corner) ----
    // Lists the top 15 players (by total mass summed across all of their cells)
    // and -- if the watched player isn't in the top 15 -- one extra row showing
    // their own rank. Built from the latest snapshot so it works uniformly in
    // SinglePlayer / LocalHost / LocalClient. Skipped during the Summary panel
    // since the death stats take the focus there.
    if ((phase_ == GamePhase::Playing || phase_ == GamePhase::DeathCam)
        && interp_.hasCurr()) {
        struct LeaderRow {
            PlayerId player     = INVALID_PLAYER;
            float    total_mass = 0.0f;
            uint8_t  tag        = 0;
        };
        // Accumulate per-player totals from the snapshot. Linear scan with a
        // tiny insertion-sorted vector (most matches have <60 players + bots,
        // so unordered_map overhead isn't worth it).
        std::vector<LeaderRow> rows;
        rows.reserve(64);
        for (const auto& c : interp_.curr().cells) {
            if (c.mass <= 0.0f) continue;
            LeaderRow* hit = nullptr;
            for (auto& r : rows) {
                if (r.player == c.owner) { hit = &r; break; }
            }
            if (hit) {
                hit->total_mass += c.mass;
                if (hit->tag == 0) hit->tag = c.personality_tag;
            } else {
                LeaderRow nr;
                nr.player     = c.owner;
                nr.total_mass = c.mass;
                nr.tag        = c.personality_tag;
                rows.push_back(nr);
            }
        }
        std::sort(rows.begin(), rows.end(),
                  [](const LeaderRow& a, const LeaderRow& b) {
                      return a.total_mass > b.total_mass;
                  });

        // Find watched-player rank for the "your rank" extra row (1-indexed).
        int watched_rank = -1;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].player == watched_player_) {
                watched_rank = static_cast<int>(i) + 1;
                break;
            }
        }

        constexpr int kTopN     = 15;
        const int     n_top     = std::min(static_cast<int>(rows.size()), kTopN);
        // Add one row if the watched player exists but is outside the top N.
        const bool    show_self = (watched_rank > kTopN);

        // Panel layout, top-left corner.
        constexpr int x0      = 12;
        constexpr int y0      = 12;
        constexpr int width   = 218;
        constexpr int row_h   = 16;
        constexpr int header  = 22;
        const int     visible_rows = n_top + (show_self ? 1 : 0);
        const int     panel_h = header + visible_rows * row_h + 6;

        // Backdrop + thin border for legibility against busy worlds.
        DrawRectangle(x0 - 2, y0 - 2, width + 4, panel_h + 4, Color{0, 0, 0, 190});
        DrawRectangle(x0, y0, width, panel_h, Color{18, 22, 32, 220});
        DrawRectangleLines(x0, y0, width, panel_h, Color{120, 130, 160, 130});

        // Header.
        DrawText("LEADERBOARD", x0 + 8, y0 + 5, 14, Color{220, 220, 240, 230});

        auto drawRow = [&](int row_y, int rank, const LeaderRow& r,
                            bool is_self_extra) {
            const Color pcol = colorForPlayer(r.player);
            // Highlight the watched player's row with a faint backdrop +
            // brighter text. Also highlight the "your rank" extra row.
            const bool highlight = (r.player == watched_player_) || is_self_extra;
            if (highlight) {
                DrawRectangle(x0 + 2, row_y - 1, width - 4, row_h,
                              Color{255, 220, 100, 35});
            }
            // Rank number, right-aligned in a 22-px column.
            char rank_buf[8];
            std::snprintf(rank_buf, sizeof(rank_buf), "%d.", rank);
            int rank_w = MeasureText(rank_buf, 13);
            DrawText(rank_buf, x0 + 26 - rank_w, row_y + 1, 13,
                     Color{200, 200, 220, 230});
            // Player label: display name when known, falling back to
            // <letter><id>. Truncate at 14 chars so a long custom name still
            // leaves room for the mass column on the right.
            std::string label = playerLabel(r.player, r.tag, /*max_len=*/14);
            // Cell-coloured dot before the name so the leaderboard maps
            // visually to the cells on screen.
            DrawCircle(x0 + 36, row_y + row_h / 2, 4.0f, pcol);
            DrawText(label.c_str(), x0 + 46, row_y + 1, 13,
                     highlight ? Color{255, 240, 180, 255}
                                : Color{220, 225, 245, 230});
            // Mass, right-aligned to the panel.
            char mass_buf[16];
            std::snprintf(mass_buf, sizeof(mass_buf), "%d",
                          static_cast<int>(r.total_mass));
            int mass_w = MeasureText(mass_buf, 13);
            DrawText(mass_buf, x0 + width - mass_w - 10, row_y + 1, 13,
                     highlight ? Color{255, 240, 180, 255}
                                : Color{200, 210, 230, 220});
        };

        for (int i = 0; i < n_top; ++i) {
            drawRow(y0 + header + i * row_h, i + 1, rows[i], false);
        }
        if (show_self) {
            // Separator strip above the "your rank" row so it reads as its
            // own group.
            int sep_y = y0 + header + n_top * row_h - 2;
            DrawRectangle(x0 + 4, sep_y, width - 8, 1, Color{120, 130, 160, 120});
            drawRow(y0 + header + n_top * row_h, watched_rank,
                    rows[static_cast<size_t>(watched_rank - 1)], true);
        }
    }

    // Crashing-comet HUD banner. Big alarm text centered horizontally, just below the
    // top of the screen. Fades in over the first 0.3s of the warning, holds, then
    // fades out as comet_banner_remaining_ approaches zero.
    if (comet_banner_remaining_ > 0.0f) {
        const float age   = kCometBannerSec - comet_banner_remaining_;
        const float fadeI = std::min(1.0f, age / 0.30f);                       // fade in
        const float fadeO = std::min(1.0f, comet_banner_remaining_ / 0.60f);   // fade out
        const float a01   = std::min(fadeI, fadeO);
        const unsigned char a_text = static_cast<unsigned char>(a01 * 250.0f);
        const unsigned char a_bg   = static_cast<unsigned char>(a01 * 130.0f);
        // Pulsing color: orange -> yellow ramp.
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 8.0f);
        const Color tint{255,
                         static_cast<unsigned char>(140 + pulse * 80.0f),
                         static_cast<unsigned char>( 40 + pulse * 30.0f),
                         a_text};
        const char* msg  = "!! COMET INCOMING !!";
        const int   fs   = 44;
        const int   tw   = MeasureText(msg, fs);
        const int   x    = screen_w / 2 - tw / 2;
        const int   y    = 110;
        // Dark backdrop strip behind the text for readability against bright worlds.
        DrawRectangle(0, y - 12, screen_w, fs + 24, Color{40, 10, 0, a_bg});
        // Drop shadow + main text.
        DrawText(msg, x + 2, y + 2, fs, Color{0, 0, 0, a_text});
        DrawText(msg, x,     y,     fs, tint);
    }

    // ---- Match timer (top-center, when a match duration is set) ----
    // Reads match_time_left_sec from the latest snapshot so it works uniformly
    // in SP / LocalHost / LocalClient. We render in mm:ss; when the timer is
    // in its last 10 seconds it switches to a red pulse to telegraph the end.
    if (interp_.hasCurr() && interp_.curr().match_time_left_sec > 0.0f
        && (phase_ == GamePhase::Playing
            || phase_ == GamePhase::DeathCam
            || phase_ == GamePhase::Summary
            || phase_ == GamePhase::Respawning)) {
        const float left = interp_.curr().match_time_left_sec;
        const int   secs = static_cast<int>(left + 0.5f);
        const int   mm   = secs / 60;
        const int   ss   = secs % 60;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d:%02d", mm, ss);
        const bool warn   = (left <= 10.5f);
        const float pulse = warn
            ? 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 6.0f)
            : 0.0f;
        const int fs = 32;
        const int tw = MeasureText(buf, fs);
        const int x  = (screen_w - tw) / 2;
        const int y  = 14;
        DrawRectangle(x - 14, y - 6, tw + 28, fs + 12, Color{0, 0, 0, 170});
        const Color tint = warn
            ? Color{255, static_cast<unsigned char>(80  + pulse * 80),
                         static_cast<unsigned char>(80  + pulse * 40), 255}
            : Color{235, 235, 220, 235};
        DrawText(buf, x + 2, y + 2, fs, Color{0, 0, 0, 200});
        DrawText(buf, x,     y,     fs, tint);
    }

    // ---- Match-end overlay (winner panel + auto-return countdown) ----
    if (phase_ == GamePhase::MatchEnd && match_end_active_) {
        // Dimmed background so the world recedes.
        DrawRectangle(0, 0, screen_w, screen_h, Color{0, 0, 0, 180});

        // Centred card. Larger than the pause overlay because this is the
        // big "match is over" moment.
        const int box_w = 480;
        const int box_h = 240;
        const int box_x = (screen_w - box_w) / 2;
        const int box_y = (screen_h - box_h) / 2;
        DrawRectangleRounded(
            Rectangle{(float)box_x, (float)box_y, (float)box_w, (float)box_h},
            0.10f, 8, Color{22, 28, 48, 245});
        DrawRectangleRoundedLines(
            Rectangle{(float)box_x, (float)box_y, (float)box_w, (float)box_h},
            0.10f, 8, Color{255, 220, 130, 220});

        // Header.
        const char* title = "MATCH OVER";
        const int   t_fs  = 32;
        const int   t_w   = MeasureText(title, t_fs);
        DrawText(title, box_x + (box_w - t_w) / 2 + 2, box_y + 22 + 2, t_fs,
                 Color{0, 0, 0, 200});
        DrawText(title, box_x + (box_w - t_w) / 2,     box_y + 22,     t_fs,
                 Color{255, 220, 130, 255});

        // Winner line.
        const bool   self_won = (match_end_winner_ == watched_player_);
        char         buf[64];
        std::snprintf(buf, sizeof(buf), "WINNER: %s",
                      match_end_winner_name_.c_str());
        const int    w_fs = 28;
        const int    w_w  = MeasureText(buf, w_fs);
        const Color  w_col = self_won
                                ? Color{120, 250, 160, 255}
                                : Color{255, 255, 255, 250};
        DrawText(buf, box_x + (box_w - w_w) / 2 + 2, box_y + 80 + 2, w_fs,
                 Color{0, 0, 0, 200});
        DrawText(buf, box_x + (box_w - w_w) / 2,     box_y + 80,     w_fs,
                 w_col);

        // Mass line.
        char mass_buf[48];
        std::snprintf(mass_buf, sizeof(mass_buf), "with %.0f mass",
                      match_end_winner_mass_);
        const int m_fs = 18;
        const int m_w  = MeasureText(mass_buf, m_fs);
        DrawText(mass_buf, box_x + (box_w - m_w) / 2, box_y + 122, m_fs,
                 Color{200, 210, 230, 230});

        // "Returning in X..." countdown.
        int back_in = static_cast<int>(match_end_remaining_sec_ + 0.999f);
        if (back_in < 0) back_in = 0;
        char back_buf[48];
        std::snprintf(back_buf, sizeof(back_buf), "returning in %d...", back_in);
        const int b_fs = 16;
        const int b_w  = MeasureText(back_buf, b_fs);
        DrawText(back_buf, box_x + (box_w - b_w) / 2, box_y + box_h - 40, b_fs,
                 Color{170, 180, 210, 220});
    }

    // LocalClient runs without a populated sim world; the latest snapshot is the
    // only place to find our watched cell's mass / hiding state / pos. When the
    // world lookup fails we synthesize a minimal Cell from the matching CellSnap
    // so the HUD doesn't need to know which mode we're in.
    const Cell* watched = world.findCell(watched_cell_);
    Cell        synth_watched_storage;
    if (!watched && interp_.hasCurr()) {
        for (const auto& cs : interp_.curr().cells) {
            if (cs.id != watched_cell_) continue;
            synth_watched_storage.id    = cs.id;
            synth_watched_storage.owner = cs.owner;
            synth_watched_storage.pos   = cs.pos;
            synth_watched_storage.vel   = cs.vel;
            synth_watched_storage.mass  = cs.mass;
            // The HUD only reads these state fields from a Cell*; mirror them out
            // of the snapshot. Anything not represented in CellSnap defaults to
            // the Cell ctor's value, which is benign for the HUD.
            synth_watched_storage.hiding_in =
                cs.hiding ? cs.hiding_in_id : INVALID_ENTITY;
            synth_watched_storage.blackhole_stamina = cs.blackhole_stamina_norm;
            watched = &synth_watched_storage;
            break;
        }
    }
    const MatchSummary* summary_ptr =
        (phase_ == GamePhase::Summary) ? &summary_ : nullptr;
    SummaryAction sa = hud_.render(screen_w, screen_h, watched, tick, GetFPS(),
                                   camera_.zoom(), dt_mult_, paused_, isUsingTouch(),
                                   phase_, summary_ptr);

    // Translate Summary / Pause panel clicks into state transitions. PLAY AGAIN
    // collapses the Summary countdown to zero (so updateFrame transitions to
    // Respawning next tick). MAIN MENU sets a flag the outer Menu<->Match loop polls.
    // RESUME unpauses (the Pause overlay's primary button).
    if (sa == SummaryAction::PlayAgainNow) {
        summary_remaining_ = 0.0f;
    } else if (sa == SummaryAction::ReturnToMenu) {
        return_to_menu_pending_ = true;
    } else if (sa == SummaryAction::ResumeFromPause) {
        paused_ = false;
    } else if (sa == SummaryAction::Disconnect) {
        // Same outer-loop hand-off as ReturnToMenu. The outer loop's mode
        // check decides whether we land back in MainMenu (SP) or the lobby
        // (LocalHost / LocalClient). Closing the pause overlay too so the
        // last visible frame before the match teardown isn't a stuck menu.
        return_to_menu_pending_ = true;
        paused_                 = false;
    }

    console_.render(screen_w, screen_h);
}

} // namespace cr
