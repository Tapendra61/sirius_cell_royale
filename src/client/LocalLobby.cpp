#include "LocalLobby.h"

#include "UiWidgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cr {

namespace {

// Skeleton: hardcoded discovery results so the JOIN screen has something visible.
// Replace with live UDP-broadcast results once net/LocalDiscovery is implemented.
std::vector<DiscoveredHost> placeholderDiscovery() {
    return {
        // Empty by default -- a real REFRESH would populate this. We leave it empty so
        // the empty-state copy is the default UX rather than fake hosts users might
        // try to click.
    };
}

} // namespace

void LocalLobby::update(float frame_dt, int /*sw*/, int /*sh*/) {
    anim_time_ += frame_dt;
    if (refresh_remaining_ > 0.0f) {
        refresh_remaining_ -= frame_dt;
    }
}

void LocalLobby::reset() {
    sub_state_         = LobbySubState::Picker;
    refresh_remaining_ = 0.0f;
}

LocalLobbyAction LocalLobby::render(int sw, int sh) {
    DrawRectangle(0, 0, sw, sh, Color{12, 16, 24, 235});

    LocalLobbyAction action = LocalLobbyAction::None;

    switch (sub_state_) {
        case LobbySubState::Picker:       renderPicker(sw, sh, action);       break;
        case LobbySubState::HostWaiting:  renderHostWaiting(sw, sh, action);  break;
        case LobbySubState::JoinBrowsing: renderJoinBrowsing(sw, sh, action); break;
    }

    return action;
}

void LocalLobby::renderPicker(int sw, int sh, LocalLobbyAction& action) {
    // ---- Title ----
    const char* title = "LOCAL GAME";
    int   t_size = 64;
    int   tw     = MeasureText(title, t_size);
    int   ty     = static_cast<int>(sh * 0.18f);
    DrawText(title, (sw - tw) / 2 + 4, ty + 4, t_size, Color{0, 0, 0, 200});
    DrawText(title, (sw - tw) / 2,     ty,     t_size, Color{220, 240, 230, 255});

    const char* tagline = "play on your local network";
    int g_size = 18;
    int gw = MeasureText(tagline, g_size);
    DrawText(tagline, (sw - gw) / 2, ty + t_size + 12, g_size,
             Color{180, 195, 210, 200});

    // ---- Buttons ----
    const int btn_w = 340;
    const int btn_h = 80;
    const int btn_x = (sw - btn_w) / 2;
    int       btn_y = static_cast<int>(sh * 0.44f);

    if (drawButtonWithSub(
            Rectangle{(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h},
            "HOST", 32,
            "start a game others can join", 13,
            Color{55, 130, 110, 255},
            Color{240, 250, 245, 255})) {
        sub_state_  = LobbySubState::HostWaiting;
        swallowNextClick();
    }
    btn_y += btn_h + 22;

    if (drawButtonWithSub(
            Rectangle{(float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h},
            "JOIN", 32,
            "find a game on the network", 13,
            Color{60, 100, 160, 255},
            Color{225, 235, 250, 255})) {
        sub_state_  = LobbySubState::JoinBrowsing;
        discovered_ = placeholderDiscovery();
        swallowNextClick();
    }
    btn_y += btn_h + 30;

    {
        const int back_w = (btn_w - 12) / 2;
        const int back_x = (sw - back_w) / 2;
        if (drawButton(
                Rectangle{(float)back_x, (float)btn_y, (float)back_w, 52.0f},
                "BACK", 22,
                Color{42, 50, 72, 255}, Color{220, 225, 245, 230})) {
            action = LocalLobbyAction::BackToRoyaleMenu;
        }
    }

    {
        const char* foot = "press ESC to go back";
        int fs = 14;
        int fw = MeasureText(foot, fs);
        DrawText(foot, (sw - fw) / 2, sh - 30, fs,
                 Color{150, 160, 180, 200});
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        action = LocalLobbyAction::BackToRoyaleMenu;
    }
}

void LocalLobby::renderHostWaiting(int sw, int sh, LocalLobbyAction& action) {
    // ---- Title ----
    const char* title = "HOSTING";
    int t_size = 52;
    int tw     = MeasureText(title, t_size);
    int ty     = static_cast<int>(sh * 0.13f);
    DrawText(title, (sw - tw) / 2 + 3, ty + 3, t_size, Color{0, 0, 0, 200});
    DrawText(title, (sw - tw) / 2,     ty,     t_size, Color{240, 230, 180, 255});

    // ---- Status line ----
    // Skeleton: the real implementation will pull the bind address + port from
    // NetworkTransport once host() is implemented. For now we show the placeholder
    // that matches the loopback default the JOIN screen suggests.
    char status_line[160];
    std::snprintf(status_line, sizeof(status_line),
                  "listening on 0.0.0.0:7456  -- waiting for players...");
    int s_fs = 18;
    int sw_w = MeasureText(status_line, s_fs);
    DrawText(status_line, (sw - sw_w) / 2, ty + t_size + 20, s_fs,
             Color{200, 215, 230, 220});

    // Animated dot-trail to show the listen state is "alive".
    {
        float k = std::fmod(anim_time_ * 2.0f, 3.0f);
        int dots = static_cast<int>(k) + 1;
        char trail[8] = {0};
        for (int i = 0; i < dots && i < 7; ++i) trail[i] = '.';
        int t_fs = 22;
        int t_w  = MeasureText(trail, t_fs);
        DrawText(trail, sw / 2 - t_w / 2,
                 ty + t_size + 20 + s_fs + 8, t_fs, Color{180, 200, 230, 220});
    }

    // ---- Player list panel ----
    // Skeleton placeholder: shows the host slot (you) and empty seats for future
    // joiners. Wire to NetworkTransport's connected-peer list when that lands.
    const int panel_w = 460;
    const int panel_h = 200;
    const int panel_x = (sw - panel_w) / 2;
    const int panel_y = static_cast<int>(sh * 0.40f);
    DrawRectangleRounded(
        Rectangle{(float)panel_x, (float)panel_y, (float)panel_w, (float)panel_h},
        0.18f, 6, Color{20, 26, 42, 230});
    DrawRectangleRoundedLines(
        Rectangle{(float)panel_x, (float)panel_y, (float)panel_w, (float)panel_h},
        0.18f, 6, Color{120, 140, 180, 90});

    DrawText("PLAYERS", panel_x + 18, panel_y + 14, 16, Color{200, 215, 240, 200});

    const int row_y = panel_y + 44;
    DrawText("> you (host)", panel_x + 24, row_y, 18,
             Color{255, 220, 130, 235});
    DrawText("empty slot",   panel_x + 24, row_y + 28, 16, Color{120, 130, 150, 200});
    DrawText("empty slot",   panel_x + 24, row_y + 52, 16, Color{120, 130, 150, 200});
    DrawText("empty slot",   panel_x + 24, row_y + 76, 16, Color{120, 130, 150, 200});

    // ---- Action buttons ----
    const int btn_w = 200;
    const int btn_h = 56;
    const int btn_gap = 16;
    const int total_w = btn_w * 2 + btn_gap;
    const int btn_y = panel_y + panel_h + 28;

    if (drawButton(
            Rectangle{(float)(sw / 2 - total_w / 2), (float)btn_y,
                      (float)btn_w, (float)btn_h},
            "START GAME", 22,
            Color{60, 140, 90, 255}, Color{240, 250, 245, 255})) {
        action = LocalLobbyAction::StartLocalHost;
    }
    if (drawButton(
            Rectangle{(float)(sw / 2 - total_w / 2 + btn_w + btn_gap), (float)btn_y,
                      (float)btn_w, (float)btn_h},
            "CANCEL", 22,
            Color{82, 52, 60, 255}, Color{240, 220, 220, 230})) {
        sub_state_ = LobbySubState::Picker;
        swallowNextClick();
    }

    // ---- Hint footer ----
    {
        const char* foot = "skeleton: START runs a local session (network sync wires in next).";
        int fs = 13;
        int fw = MeasureText(foot, fs);
        DrawText(foot, (sw - fw) / 2, sh - 30, fs,
                 Color{140, 150, 170, 190});
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        sub_state_ = LobbySubState::Picker;
    }
}

void LocalLobby::renderJoinBrowsing(int sw, int sh, LocalLobbyAction& action) {
    // ---- Title ----
    const char* title = "JOIN A GAME";
    int t_size = 48;
    int tw     = MeasureText(title, t_size);
    int ty     = static_cast<int>(sh * 0.12f);
    DrawText(title, (sw - tw) / 2 + 3, ty + 3, t_size, Color{0, 0, 0, 200});
    DrawText(title, (sw - tw) / 2,     ty,     t_size, Color{200, 220, 250, 255});

    // ---- Discovered-host list panel ----
    const int panel_w = 520;
    const int panel_h = 230;
    const int panel_x = (sw - panel_w) / 2;
    const int panel_y = static_cast<int>(sh * 0.27f);
    DrawRectangleRounded(
        Rectangle{(float)panel_x, (float)panel_y, (float)panel_w, (float)panel_h},
        0.15f, 6, Color{20, 26, 42, 230});
    DrawRectangleRoundedLines(
        Rectangle{(float)panel_x, (float)panel_y, (float)panel_w, (float)panel_h},
        0.15f, 6, Color{120, 140, 180, 90});
    DrawText("DISCOVERED HOSTS", panel_x + 18, panel_y + 14, 16,
             Color{200, 215, 240, 200});

    // REFRESH button (top-right of the panel).
    {
        const int r_w = 110, r_h = 32;
        Rectangle rb{(float)(panel_x + panel_w - r_w - 12),
                     (float)(panel_y + 10), (float)r_w, (float)r_h};
        const bool busy = refresh_remaining_ > 0.0f;
        if (drawButton(rb, busy ? "..." : "REFRESH", 15,
                       Color{50, 70, 110, 255}, Color{220, 230, 250, 230}, !busy)) {
            // Skeleton: trigger a brief visual flash. Real impl will kick off the
            // background broadcast discovery scan.
            refresh_remaining_ = 0.6f;
            discovered_        = placeholderDiscovery();
        }
    }

    if (discovered_.empty()) {
        const char* empty_msg = "no games found on this network";
        int e_fs = 16;
        int e_w  = MeasureText(empty_msg, e_fs);
        DrawText(empty_msg, panel_x + (panel_w - e_w) / 2,
                 panel_y + panel_h / 2 - e_fs / 2, e_fs,
                 Color{160, 170, 190, 220});
    } else {
        // Render each discovered host as a row with a JOIN button on the right.
        const int row_h    = 36;
        const int rows_top = panel_y + 44;
        const int max_rows = std::min<int>(discovered_.size(), 5);
        for (int i = 0; i < max_rows; ++i) {
            const auto& h = discovered_[i];
            int ry = rows_top + i * row_h;
            DrawText(h.name.c_str(), panel_x + 18, ry, 16, Color{220, 230, 250, 230});
            char meta[64];
            std::snprintf(meta, sizeof(meta), "%s   %d/%d",
                          h.address.c_str(), h.player_count, h.max_players);
            int mw = MeasureText(meta, 13);
            DrawText(meta, panel_x + panel_w - mw - 110, ry + 2, 13,
                     Color{170, 180, 200, 200});
            // Per-row JOIN button (skeleton: clicking populates the manual field).
            Rectangle jb{(float)(panel_x + panel_w - 90), (float)(ry - 2),
                         70.0f, 28.0f};
            if (drawButton(jb, "JOIN", 14,
                           Color{55, 110, 140, 255}, Color{230, 240, 250, 230})) {
                join_input_ = h.address;
                action      = LocalLobbyAction::StartLocalJoin;
            }
        }
    }

    // ---- Manual-IP entry row ----
    {
        const int box_y = panel_y + panel_h + 22;
        const int label_fs = 14;
        DrawText("Connect by address:", panel_x, box_y, label_fs,
                 Color{200, 215, 235, 220});

        const int input_w = panel_w - 130;
        Rectangle input_box{(float)panel_x,
                             (float)(box_y + label_fs + 6),
                             (float)input_w, 36.0f};
        DrawRectangleRec(input_box, Color{30, 38, 58, 230});
        // Click toggles focus -- when focused, keypresses append.
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mp = GetMousePosition();
            join_input_focused_ = CheckCollisionPointRec(mp, input_box);
        }
        DrawRectangleLinesEx(input_box, 2.0f,
            join_input_focused_ ? Color{180, 200, 240, 230}
                                : Color{100, 120, 160, 180});
        DrawText(join_input_.c_str(),
                 (int)input_box.x + 10, (int)input_box.y + 10, 18,
                 Color{230, 240, 250, 240});

        // Naive text edit -- enough for the skeleton. Real impl should use a proper
        // text-input widget with cursor, selection, paste, etc.
        if (join_input_focused_) {
            int ch = GetCharPressed();
            while (ch > 0) {
                if (join_input_.size() < 48
                    && ((ch >= '0' && ch <= '9') || ch == '.' || ch == ':'
                        || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                        || ch == '-')) {
                    join_input_.push_back(static_cast<char>(ch));
                }
                ch = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE) && !join_input_.empty()) {
                join_input_.pop_back();
            }
        }

        // JOIN button on the right.
        Rectangle jb{(float)(panel_x + input_w + 12),
                     (float)(box_y + label_fs + 6),
                     110.0f, 36.0f};
        if (drawButton(jb, "JOIN", 18,
                       Color{55, 110, 140, 255}, Color{230, 240, 250, 240})) {
            action = LocalLobbyAction::StartLocalJoin;
        }
    }

    // ---- Action row (BACK) ----
    {
        const int back_w = 200;
        const int back_h = 52;
        Rectangle bb{(float)(sw / 2 - back_w / 2), (float)(sh - back_h - 60),
                     (float)back_w, (float)back_h};
        if (drawButton(bb, "BACK", 22,
                       Color{42, 50, 72, 255}, Color{220, 225, 245, 230})) {
            sub_state_ = LobbySubState::Picker;
            swallowNextClick();
        }
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        sub_state_ = LobbySubState::Picker;
    }
}

} // namespace cr
