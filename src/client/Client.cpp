#include "Client.h"

#include "raylib.h"

#include <algorithm>
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
        bool skip = s.splitPressed || s.dashPressed || s.ejectPressed || s.pausePressed
                 || IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        if (skip) summary_remaining_ = 0.0f; // cuts to Respawning next updateFrame
        return;
    }
    if (phase_ == GamePhase::Respawning) {
        return; // waiting for main to spawn the cell
    }

    // ---- Playing phase ----
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
}

std::vector<Command> Client::takeCommands() {
    std::vector<Command> out;
    out.swap(queued_);
    return out;
}

float Client::effectiveDtMultiplier() const {
    if (paused_ || auto_paused_) return 0.0f;
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
    // FPS cap is applied by main.cpp at startup / in the Settings live-preview path.
}

SaveData Client::snapshotForSave() const {
    SaveData s;
    s.total_xp      = static_cast<uint32_t>(std::max(0, total_xp_));
    s.level         = static_cast<uint32_t>(std::max(1, level_));
    s.games_played  = static_cast<uint32_t>(std::max(0, games_played_));
    s.best_mass     = best_mass_ever_;
    s.best_combo    = static_cast<uint32_t>(std::max(0, best_combo_ever_));
    s.master_volume = audio_.masterVolume();
    s.sfx_volume    = audio_.sfxVolume();
    s.music_volume  = audio_.musicVolume();
    s.music_enabled = audio_.isMusicEnabled();
    s.hold_to_move  = input_config_.hold_to_move;
    s.invert_thumbs = input_config_.invert_thumbs;
    s.last_mission_reset_day = last_mission_reset_day_;
    for (int i = 0; i < kMissionCount; ++i) {
        s.daily_missions[i]          = missions_[i];
        s.daily_missions[i].progress = 0; // progress is per-match -- never persisted
    }
    return s;
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

    // ReachMass mission: tracks the player's largest total mass during this match.
    // Computed once per onEvents (i.e., once per sim tick) rather than per event.
    float player_total_mass = 0.0f;
    for (const auto& c : world.cells()) {
        if (c.owner == watched_player_) player_total_mass += c.mass;
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
                Cell* pred = world.findCell(e.predator);
                Color color = pred ? colorForPlayer(pred->owner) : RAYWHITE;
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
                const bool is_player = pred && pred->owner == watched_player_;
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
                // e.player is the OWNER of the cell that just died; e.by is the predator's cell id.
                if (e.player == watched_player_) {
                    // Snapshot the dead cell's pos before main retargets watched_cell_.
                    Vec2  pos{0.0f, 0.0f};
                    float mass = 100.0f;
                    if (auto* dead = world.findCell(watched_cell_)) {
                        pos  = dead->pos;
                        mass = dead->mass;
                    }
                    particles_.spawnDeathBurst(pos, mass, colorForPlayer(watched_player_));
                    shake_.addTrauma(0.9f);
                    hud_.onPlayerDeath();
                    audio_.playPlayerDeath();
                    death_cam_.active    = true;
                    death_cam_.target    = e.by;
                    death_cam_.remaining = DeathCam::kDuration;

                    // Capture run stats at moment of death (XP computed at summary entry).
                    phase_                   = GamePhase::DeathCam;
                    summary_.final_mass      = static_cast<int>(mass);
                    summary_.best_combo      = hud_.bestCombo();
                    summary_.time_alive_sec  = static_cast<float>(now_sec - run_start_sec_);
                }
            } else if constexpr (std::is_same_v<T, SplitEvent>) {
                Cell* from = world.findCell(e.from);
                if (from) {
                    particles_.spawnSplitPuff(from->pos, colorForPlayer(e.player));
                }
                // SplitEvents with INVALID_ENTITY child indicate a virus explosion.
                // Player virus pops always audible; bot virus pops only if visible --
                // otherwise 60 viruses + 50 bots produce a constant pop fog from off-screen.
                if (e.into == INVALID_ENTITY) {
                    const bool is_player = e.player == watched_player_;
                    if (is_player || (from && within_earshot(from->pos))) {
                        audio_.playVirusPop();
                    }
                }
            } else if constexpr (std::is_same_v<T, NearMissEvent>) {
                // hunter is the bigger cell that couldn't quite eat.
                Cell* hunter = world.findCell(e.hunter);
                Cell* prey   = world.findCell(e.prey);
                bool  player_involved = false;
                if (hunter && hunter->owner == watched_player_) {
                    hud_.onPlayerNearMissPrey();
                    particles_.spawnNearMissSparks(e.at, Color{255, 220, 80, 255});
                    player_involved = true;
                }
                if (prey && prey->owner == watched_player_) {
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
                Cell* pred = world.findCell(e.predator);
                const bool is_player = pred && pred->owner == watched_player_;
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
        summary_remaining_     = std::max(0.0f, summary_remaining_ - frame_dt);
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
        ClearBackground(Color{18, 22, 30, 255}); // bg gets baked into the RT
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
        BeginMode2D(cam);
        renderer_.drawWorld(interp_, tuning, alpha, view_min, view_max,
                            watched_cell_, watched_player_, level_);
        particles_.draw();
        popups_.draw();
        EndMode2D();
    }

    // Screen-space overlays.
    renderTouchOverlay(screen_w, screen_h, input_config_);

    const Cell* watched = world.findCell(watched_cell_);
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
    }

    console_.render(screen_w, screen_h);
}

} // namespace cr
