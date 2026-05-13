#pragma once

#include "Camera.h"
#include "Interpolator.h"
#include "Renderer.h"
#include "core/Command.h"
#include "core/Tuning.h"
#include "sim/Simulation.h"

#include <vector>

namespace cr {

// Phase 9 first-run intro: a 15-second auto-played demo that runs the first time the
// player launches the game (detected via SaveData::first_run_complete). The player
// watches a cell move, eat food, split, and dash -- no text, no prompts, "show don't
// tell" as the roadmap recommends. Any key press skips it.
//
// Implementation: a self-contained Simulation with the live BotDirector and a small
// list of scripted Commands queued at specific ticks. The player cell starts with
// enough mass to make the scripted Split fire reliably (not relying on RNG eating
// for the demo to read).
class IntroScreen {
public:
    explicit IntroScreen(const Tuning& tuning);

    // Update once per frame. Returns true when the intro is finished (15s elapsed,
    // skipped via key/mouse, or window-close requested upstream).
    bool update(float frame_dt);

    void render(int screen_w, int screen_h);

private:
    enum class CmdKind { Move, Split, Dash };

    struct ScriptCmd {
        Tick    queue_tick;   // sim tick at which to enqueue this command
        CmdKind kind;         // which command to issue
        Vec2    move_target;  // used only when kind == CmdKind::Move
    };

    void buildScript();
    void queuePendingCommands();

    Tuning            tuning_;
    Simulation        sim_;
    CameraController  camera_;
    Renderer          renderer_;
    Interpolator      interp_;

    PlayerId          player_id_     = 1;
    EntityId          player_cell_   = INVALID_ENTITY;

    float             elapsed_sec_   = 0.0f;
    double            accumulator_   = 0.0;
    float             render_alpha_  = 0.0f; // 0..1 interpolation across sim ticks
    bool              done_          = false;

    std::vector<ScriptCmd> script_;
    int                next_cmd_idx_ = 0;

    static constexpr float kDurationSec       = 15.0f;
    static constexpr float kFadeInDurationSec = 0.6f;
    static constexpr float kFadeOutDurationSec = 0.6f;
};

} // namespace cr
