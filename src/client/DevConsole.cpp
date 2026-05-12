#include "DevConsole.h"

#include "raylib.h"

#include <sstream>
#include <utility>

namespace cr {

namespace {
constexpr size_t kMaxOutputLines = 12;
constexpr int    kFontSize       = 16;
} // namespace

void DevConsole::log(std::string line) {
    output_.push_back(std::move(line));
    while (output_.size() > kMaxOutputLines) output_.pop_front();
}

void DevConsole::poll() {
#ifdef CR_DEV_TOOLS
    if (IsKeyPressed(KEY_GRAVE)) {
        toggle();
        return;
    }
    if (!open_) return;

    if (IsKeyPressed(KEY_ESCAPE)) {
        open_ = false;
        return;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        runCurrentInput();
        return;
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !input_.empty()) {
        input_.pop_back();
    }
    int key = GetCharPressed();
    while (key != 0) {
        if (key >= 32 && key < 127 && key != '`' && key != '~') {
            input_.push_back(static_cast<char>(key));
        }
        key = GetCharPressed();
    }
#endif
}

void DevConsole::runCurrentInput() {
    if (input_.empty()) return;
    log("> " + input_);
    std::vector<std::string> tokens;
    std::istringstream       iss(input_);
    std::string              tok;
    while (iss >> tok) tokens.push_back(tok);
    input_.clear();
    if (tokens.empty()) return;
    if (handler_) handler_(tokens);
}

void DevConsole::render(int screen_w, int /*screen_h*/) const {
    if (!open_) return;

    const int line_h     = kFontSize + 2;
    const int panel_rows = static_cast<int>(kMaxOutputLines) + 2;
    const int panel_h    = panel_rows * line_h + 16;

    DrawRectangle(0, 0, screen_w, panel_h, Color{0, 0, 0, 200});
    DrawLine(0, panel_h, screen_w, panel_h, Color{120, 120, 120, 255});

    int y = 8;
    for (const auto& line : output_) {
        DrawText(line.c_str(), 12, y, kFontSize, RAYWHITE);
        y += line_h;
    }

    int input_y = panel_h - kFontSize - 8;
    DrawText("> ", 12, input_y, kFontSize, YELLOW);
    int prompt_w = MeasureText("> ", kFontSize);
    DrawText(input_.c_str(), 12 + prompt_w, input_y, kFontSize, WHITE);
    if (static_cast<int>(GetTime() * 2.0) % 2 == 0) {
        int cursor_x = 12 + prompt_w + MeasureText(input_.c_str(), kFontSize);
        DrawRectangle(cursor_x, input_y, 2, kFontSize, WHITE);
    }
}

} // namespace cr
