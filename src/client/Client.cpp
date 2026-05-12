#include "Client.h"

#include "raylib.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <variant>

namespace cr {

Client::Client(EntityId watched_cell, PlayerId watched_player)
    : watched_cell_(watched_cell),
      watched_player_(watched_player),
      particles_(4096) {}

void Client::pollFrame(int screen_w, int screen_h, Tick current_tick) {
    const bool was_open = console_.isOpen();
    console_.poll();
    if (console_.isOpen()) return;
    if (was_open) return;

    Camera2D   cam = camera_.toCamera2D(screen_w, screen_h);
    InputState s   = pollInput(cam, screen_w, screen_h, input_config_);

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
    }
    if (s.ejectPressed) {
        queued_.push_back(Command{watched_player_, current_tick, EjectCmd{}});
    }
    if (s.dashPressed) {
        queued_.push_back(Command{watched_player_, current_tick, DashCmd{}});
    }
}

std::vector<Command> Client::takeCommands() {
    std::vector<Command> out;
    out.swap(queued_);
    return out;
}

float Client::effectiveDtMultiplier() const {
    if (paused_) return 0.0f;
    float m = dt_mult_;
    if (death_cam_.active) m *= 0.3f;
    return m;
}

void Client::onEvents(const std::vector<GameEvent>& events, World& world,
                      const Tuning& tuning) {
    const double now_sec = GetTime();

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

                // Trauma scales with mass; cap to keep small absorbs subtle.
                shake_.addTrauma(std::min(0.5f, e.mass_gained * 0.012f));

                // Player-specific: combo + hitstop.
                if (pred && pred->owner == watched_player_) {
                    hud_.onPlayerAbsorb(e.mass_gained, now_sec, tuning.combo_window_sec);
                    if (e.mass_gained >= tuning.hitstop_threshold_mass) {
                        hitstop_.trigger(tuning.hitstop_duration_sec);
                    }
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
                    death_cam_.active    = true;
                    death_cam_.target    = e.by;
                    death_cam_.remaining = DeathCam::kDuration;
                }
            } else if constexpr (std::is_same_v<T, SplitEvent>) {
                if (auto* from = world.findCell(e.from)) {
                    particles_.spawnSplitPuff(from->pos, colorForPlayer(e.player));
                }
            } else if constexpr (std::is_same_v<T, NearMissEvent>) {
                // hunter is the bigger cell that couldn't quite eat.
                Cell* hunter = world.findCell(e.hunter);
                Cell* prey   = world.findCell(e.prey);
                if (hunter && hunter->owner == watched_player_) {
                    hud_.onPlayerNearMissPrey();
                    particles_.spawnNearMissSparks(e.at, Color{255, 220, 80, 255});
                }
                if (prey && prey->owner == watched_player_) {
                    hud_.onPlayerNearMissAttacker();
                    shake_.addTrauma(0.25f);
                    particles_.spawnNearMissSparks(e.at, Color{255, 80, 80, 255});
                }
            } else if constexpr (std::is_same_v<T, CritEvent>) {
                // Phase 8 emits these; render hook left here for forward compat.
                particles_.spawnAbsorbBurst(e.at, e.mass_gained * 2.0f,
                                            Color{255, 220, 80, 255});
                shake_.addTrauma(0.4f);
            }
        }, ev);
    }
}

void Client::updateFrame(float frame_dt, double now_sec, const Tuning& tuning) {
    shake_.update(frame_dt);
    hitstop_.update(frame_dt);
    particles_.update(frame_dt);
    popups_.update(frame_dt);
    hud_.update(frame_dt, now_sec, tuning.combo_window_sec);

    if (death_cam_.active) {
        death_cam_.remaining -= frame_dt;
        if (death_cam_.remaining <= 0.0f) {
            death_cam_.active = false;
        }
    }
}

void Client::render(int screen_w, int screen_h, float alpha, const Tuning& tuning,
                    const World& world, Tick tick) const {
    // Camera + screen shake (shake is added to target so the whole scene jiggles).
    Camera2D cam = camera_.toCamera2D(screen_w, screen_h);
    Vec2 shake = shake_.sampleOffset(tuning.screen_shake_max);
    cam.target.x += shake.x;
    cam.target.y += shake.y;

    BeginMode2D(cam);
    renderer_.drawWorld(interp_, tuning, alpha, watched_cell_);
    particles_.draw();
    popups_.draw();
    EndMode2D();

    // Screen-space overlays.
    renderTouchOverlay(screen_w, screen_h, input_config_);

    const Cell* watched = world.findCell(watched_cell_);
    hud_.render(screen_w, screen_h, watched, tick, GetFPS(),
                camera_.zoom(), dt_mult_, paused_, isUsingTouch());

    console_.render(screen_w, screen_h);
}

} // namespace cr
