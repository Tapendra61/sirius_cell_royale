#pragma once

#include "Camera.h"
#include "DevConsole.h"
#include "Interpolator.h"
#include "Renderer.h"
#include "core/Command.h"
#include "core/Snapshot.h"
#include "platform/Input.h"

#include <vector>

namespace cr {

// Owns the on-screen pieces (camera, renderer, interpolator, dev console) and
// the per-frame input -> command translation. Stays independent of the
// simulation pointer so it can be reused across reset/restart later.
class Client {
public:
    Client(EntityId watched_cell, PlayerId watched_player);

    // Input poll (mouse / keyboard / touch) -> queue of Commands. Drained each
    // frame by main with takeCommands(). When the dev console is open, gameplay
    // input is suppressed and ESC closes the console rather than pausing.
    void pollFrame(int screen_w, int screen_h, Tick current_tick);

    std::vector<Command> takeCommands();

    void onSnapshot(Snapshot s) { interp_.push(std::move(s)); }

    void render(int screen_w, int screen_h, float alpha, const Tuning& tuning) const;

    // Time scaling. dt_mult_ is the slowmo setting (dev console); paused short-circuits to 0.
    float dtMultiplier() const { return dt_mult_; }
    void  setDtMultiplier(float m) { dt_mult_ = m; }

    bool  isPaused() const { return paused_; }
    void  togglePause() { paused_ = !paused_; }
    float effectiveDtMultiplier() const { return paused_ ? 0.0f : dt_mult_; }

    InputConfig&       inputConfig() { return input_config_; }
    const InputConfig& inputConfig() const { return input_config_; }

    EntityId watchedCell() const { return watched_cell_; }
    void     setWatchedCell(EntityId id) { watched_cell_ = id; }

    PlayerId watchedPlayer() const { return watched_player_; }

    CameraController&       camera() { return camera_; }
    const CameraController& camera() const { return camera_; }

    DevConsole&         console() { return console_; }
    const Interpolator& interp() const { return interp_; }

private:
    EntityId             watched_cell_;
    PlayerId             watched_player_;
    Interpolator         interp_;
    CameraController     camera_;
    Renderer             renderer_;
    DevConsole           console_;
    InputConfig          input_config_;
    std::vector<Command> queued_;
    float                dt_mult_ = 1.0f;
    bool                 paused_  = false;
};

} // namespace cr
