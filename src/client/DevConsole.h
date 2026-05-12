#pragma once

#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace cr {

// Toggled with `~` (KEY_GRAVE). When open, captures keyboard text input until Enter or Escape.
// All commands run through a single handler set by the owner so the console doesn't have to
// know about Simulation/World/Replay/Tuning. Gated by CR_DEV_TOOLS at compile time -- with
// the flag off the toggle and input are no-ops.
class DevConsole {
public:
    using Handler = std::function<void(const std::vector<std::string>&)>;

    void setHandler(Handler h) { handler_ = std::move(h); }
    bool isOpen() const { return open_; }
    void toggle() { open_ = !open_; }

    void log(std::string line);
    void clearOutput() { output_.clear(); }

    void poll();
    void render(int screen_w, int screen_h) const;

private:
    void runCurrentInput();

    bool                    open_ = false;
    std::string             input_;
    std::deque<std::string> output_;
    Handler                 handler_;
};

} // namespace cr
