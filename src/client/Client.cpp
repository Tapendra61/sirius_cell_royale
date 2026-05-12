#include "Client.h"

#include "raylib.h"

#include <utility>

namespace cr {

Client::Client(EntityId watched_cell, PlayerId watched_player)
    : watched_cell_(watched_cell), watched_player_(watched_player) {}

void Client::pollFrame(int screen_w, int screen_h, Tick current_tick) {
    // Console gets first crack at keyboard input. If ESC closed it this frame, swallow
    // the same edge so we don't immediately pause as well.
    const bool was_open = console_.isOpen();
    console_.poll();
    if (console_.isOpen()) return;
    if (was_open) return;

    Camera2D   cam = camera_.toCamera2D(screen_w, screen_h);
    InputState s   = pollInput(cam, screen_w, screen_h, input_config_);

    if (s.pausePressed) {
        togglePause();
        return; // don't process other input on the same frame as the pause edge
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

void Client::render(int screen_w, int screen_h, float alpha, const Tuning& tuning) const {
    renderer_.drawWorld(interp_, camera_, tuning, screen_w, screen_h, alpha, watched_cell_);
    renderTouchOverlay(screen_w, screen_h, input_config_);
    console_.render(screen_w, screen_h);
}

} // namespace cr
