#pragma once

#include "Camera.h"
#include "DevConsole.h"
#include "Hud.h"
#include "Interpolator.h"
#include "Renderer.h"
#include "core/Command.h"
#include "core/Events.h"
#include "core/Snapshot.h"
#include "feel/Hitstop.h"
#include "feel/Particles.h"
#include "feel/Popups.h"
#include "feel/ScreenShake.h"
#include "platform/Input.h"
#include "sim/World.h"

#include <vector>

namespace cr {

// Owns the on-screen pieces (camera, renderer, interpolator, dev console, feel layer,
// HUD) and the per-frame input -> command translation.
class Client {
public:
    Client(EntityId watched_cell, PlayerId watched_player);

    // Input poll -> queue of Commands. Drained each frame by main with takeCommands().
    void pollFrame(int screen_w, int screen_h, Tick current_tick);

    std::vector<Command> takeCommands();

    void onSnapshot(Snapshot s) { interp_.push(std::move(s)); }

    // Convert sim events into juice + HUD updates. Called once per sim tick by main.
    void onEvents(const std::vector<GameEvent>& events, World& world,
                  const Tuning& tuning);

    // Per-frame update of all client-side time-driven state (shake, particles, popups,
    // hitstop, HUD, death cam). Called before render.
    void updateFrame(float frame_dt, double now_sec, const Tuning& tuning);

    void render(int screen_w, int screen_h, float alpha, const Tuning& tuning,
                const World& world, Tick tick) const;

    // Time scaling.
    float dtMultiplier() const { return dt_mult_; }
    void  setDtMultiplier(float m) { dt_mult_ = m; }
    bool  isPaused() const { return paused_; }
    void  togglePause() { paused_ = !paused_; }
    // Includes pause + death-cam slow-mo.
    float effectiveDtMultiplier() const;

    bool isHitstopActive() const { return hitstop_.isFrozen(); }

    // Death cam: when active, main loop should point the camera at deathCamTarget().
    bool     deathCamActive() const { return death_cam_.active; }
    EntityId deathCamTarget() const { return death_cam_.target; }

    InputConfig&       inputConfig() { return input_config_; }
    const InputConfig& inputConfig() const { return input_config_; }

    EntityId watchedCell() const { return watched_cell_; }
    void     setWatchedCell(EntityId id) { watched_cell_ = id; }
    PlayerId watchedPlayer() const { return watched_player_; }

    CameraController&       camera() { return camera_; }
    const CameraController& camera() const { return camera_; }

    DevConsole& console() { return console_; }
    Hud&        hud() { return hud_; }

private:
    struct DeathCam {
        bool     active   = false;
        EntityId target   = INVALID_ENTITY;
        float    remaining = 0.0f;
        static constexpr float kDuration = 1.5f;
    };

    EntityId             watched_cell_;
    PlayerId             watched_player_;
    Interpolator         interp_;
    CameraController     camera_;
    Renderer             renderer_;
    DevConsole           console_;
    InputConfig          input_config_;
    std::vector<Command> queued_;

    // Feel layer
    ParticleSystem       particles_;
    ScreenShake          shake_;
    Hitstop              hitstop_;
    PopupSystem          popups_;
    Hud                  hud_;
    DeathCam             death_cam_;

    float                dt_mult_ = 1.0f;
    bool                 paused_  = false;
};

} // namespace cr
