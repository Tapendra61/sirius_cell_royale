#include "LocalLobby.h"

#include "UiWidgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cr {

namespace {

// Convert a LocalDiscovery entry into the lobby's display struct. The lobby
// existed before discovery did, so the two have slightly different layouts;
// `player_count` / `max_players` aren't part of the wire format yet (the
// announce packet is tiny). Default both to 0 / 8 so the UI line still reads.
DiscoveredHost toDiscoveredHost(const DiscoveredHostEntry& e) {
    DiscoveredHost h;
    h.name         = (e.name[0] != 0) ? std::string(e.name) : std::string("Cell Royale");
    char addr_buf[80];
    std::snprintf(addr_buf, sizeof(addr_buf), "%s:%u",
                  e.address.c_str(), static_cast<unsigned>(e.game_port));
    h.address      = addr_buf;
    h.player_count = 0;
    h.max_players  = 8;
    return h;
}

// Vertical gradient + subtle radial vignette + drifting accent stripe. Shared
// by all lobby sub-states so the picker / host / join screens feel like one
// cohesive surface instead of four different flat backdrops.
void drawLobbyBackdrop(int sw, int sh, float anim_time, Color accent) {
    // Base vertical gradient (top: near-black, bottom: cool blue-grey).
    DrawRectangleGradientV(0, 0, sw, sh,
                           Color{8, 12, 20, 255}, Color{18, 26, 42, 255});

    // Soft side-accent stripe -- a vertical band on the left side that
    // hints at the sub-state's mood color (greens for HOST, blues for
    // JOIN). Two-pass alpha so it fades into the gradient.
    DrawRectangleGradientH(0, 0, 220, sh,
                           Color{accent.r, accent.g, accent.b, 35},
                           Color{accent.r, accent.g, accent.b, 0});

    // Top accent line for visual grounding.
    DrawRectangleGradientH(0, 0, sw, 3,
                           Color{accent.r, accent.g, accent.b, 0},
                           Color{accent.r, accent.g, accent.b, 90});

    // Drifting subtle orbs -- one slow-pulsing accent dot in the corners.
    float p = (std::sin(anim_time * 0.7f) + 1.0f) * 0.5f; // 0..1
    DrawCircle(sw - 60, 60, 4.0f + p * 2.0f,
               Color{accent.r, accent.g, accent.b,
                     static_cast<unsigned char>(60 + p * 90)});
    DrawCircle(40, sh - 60, 4.0f + (1.0f - p) * 2.0f,
               Color{accent.r, accent.g, accent.b,
                     static_cast<unsigned char>(60 + (1.0f - p) * 90)});

    // Bottom edge fade-to-black so HUD-like elements (hint footer) sit
    // cleanly on a darker surface.
    DrawRectangleGradientV(0, sh - 60, sw, 60,
                           Color{0, 0, 0, 0}, Color{0, 0, 0, 90});
}

// Color tints for each sub-state's accent (drives the side-stripe + corner
// dots so users instantly read "host vs join" peripheral-vision-wise).
Color subStateAccent(LobbySubState s) {
    switch (s) {
        case LobbySubState::Picker:       return Color{120, 170, 200, 255};
        case LobbySubState::HostWaiting:  return Color{ 90, 200, 150, 255};
        case LobbySubState::JoinBrowsing: return Color{120, 165, 230, 255};
        case LobbySubState::JoinWaiting:  return Color{160, 195, 240, 255};
    }
    return Color{120, 170, 200, 255};
}

// Smooth 0..1 triangle wave for breathing animations -- duplicated here from
// MainMenu/RoyaleMenu so the lobby doesn't need to depend on a shared header.
// Cheap; the call rate is once per frame per title.
float lobbyEaseOutCubic(float t) {
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}
float lobbyBreathe(float t, float period) {
    float p = std::fmod(t, period) / period;
    float tri = p < 0.5f ? (p * 2.0f) : (2.0f - p * 2.0f);
    return lobbyEaseOutCubic(tri);
}

// Reusable title block with breathing alpha halo (no per-frame size pulse)
// and a fading-edge underline. The halo is tinted with `accent` so the four
// sub-states each get their own mood (green for HOST, blue for JOIN, etc).
// Returns the y-coordinate just below the title's underline so callers can
// chain a tagline / panels below without re-computing layout.
int drawLobbyTitle(int sw, int sh, const char* title, int t_size,
                   int ty, Color accent, float anim_time) {
    const float ui = uiScale(sw, sh);
    const int   tw = MeasureText(title, t_size);
    const float cx = sw * 0.5f;
    const float cy = static_cast<float>(ty) + t_size * 0.5f;

    // Breathing halo, scaled by uiScale so it grows with the window.
    const float p      = lobbyBreathe(anim_time, 5.5f);
    const float halo_a = 30.0f + 22.0f * p;
    const Color halo   = Color{accent.r, accent.g, accent.b,
                               static_cast<unsigned char>(halo_a)};
    DrawCircle((int)cx, (int)cy, 380.0f * ui,
               Color{halo.r, halo.g, halo.b,
                     (unsigned char)(halo.a * 0.32f)});
    DrawCircle((int)cx, (int)cy, 270.0f * ui,
               Color{halo.r, halo.g, halo.b,
                     (unsigned char)(halo.a * 0.55f)});
    DrawCircle((int)cx, (int)cy, 180.0f * ui,
               Color{halo.r, halo.g, halo.b,
                     (unsigned char)(halo.a * 0.82f)});
    DrawCircle((int)cx, (int)cy, 110.0f * ui, halo);

    Color body{
        static_cast<unsigned char>(std::min(255, (int)accent.r + 60)),
        static_cast<unsigned char>(std::min(255, (int)accent.g + 50)),
        static_cast<unsigned char>(std::min(255, (int)accent.b + 30)),
        255
    };
    const int sh_a = std::max(1, uiPx(sw, sh, 7));
    const int sh_b = std::max(1, uiPx(sw, sh, 9));
    const int sh_c = std::max(1, uiPx(sw, sh, 3));
    const int sh_d = std::max(1, uiPx(sw, sh, 4));
    DrawText(title, (sw - tw) / 2 + sh_a, ty + sh_b, t_size, Color{0, 0, 0, 110});
    DrawText(title, (sw - tw) / 2 + sh_c, ty + sh_d, t_size, Color{0, 0, 0, 200});
    DrawText(title, (sw - tw) / 2,        ty,        t_size, body);

    // Breathing underline.
    {
        unsigned char a = static_cast<unsigned char>(110 + p * 80);
        const int inset = uiPx(sw, sh, 20);
        const int uly   = ty + t_size + uiPx(sw, sh, 4);
        const int ulh   = std::max(2, uiPx(sw, sh, 2));
        DrawRectangleGradientH((sw - tw) / 2 + inset, uly,
                               tw - 2 * inset, ulh,
                               Color{accent.r, accent.g, accent.b, 0},
                               Color{accent.r, accent.g, accent.b, a});
        DrawRectangleGradientH((sw - tw) / 2 + inset, uly,
                               tw - 2 * inset, ulh,
                               Color{accent.r, accent.g, accent.b, a},
                               Color{accent.r, accent.g, accent.b, 0});
    }
    return ty + t_size + uiPx(sw, sh, 12);
}

void drawLobbyEyebrow(int sw, int ey, const char* text, Color accent) {
    // Use sh = ey * (720/72) as a proxy since the helper doesn't get sh -- ey
    // is a sh-derived value (typically 0.10*sh, so sh = ey*10). Good enough
    // for the uiScale lookup since the eyebrow size hardly varies.
    int sh_guess = ey > 0 ? std::max(720, ey * 10) : 720;
    int e_size = uiPx(sw, sh_guess, 12);
    int ew = MeasureText(text, e_size);
    int line_w = uiPx(sw, sh_guess, 60);
    int gap_l  = uiPx(sw, sh_guess, 70);
    int gap_r  = uiPx(sw, sh_guess, 10);
    DrawRectangle((sw - ew) / 2 - gap_l, ey + e_size / 2, line_w, 1,
                  Color{accent.r, accent.g, accent.b, 180});
    DrawRectangle((sw + ew) / 2 + gap_r, ey + e_size / 2, line_w, 1,
                  Color{accent.r, accent.g, accent.b, 180});
    DrawText(text, (sw - ew) / 2, ey, e_size,
             Color{accent.r, accent.g, accent.b, 220});
}

} // namespace

void LocalLobby::update(float frame_dt, int /*sw*/, int /*sh*/) {
    anim_time_ += frame_dt;
    if (refresh_remaining_ > 0.0f) {
        refresh_remaining_ -= frame_dt;
    }

    // Drive LAN discovery while the JOIN browser is up. We lazily start the
    // listener the first frame we land in JoinBrowsing and stop it the moment
    // we leave (Picker / HostWaiting / JoinWaiting all kill it). Idle in the
    // other states.
    if (sub_state_ == LobbySubState::JoinBrowsing) {
        if (discovery_.mode() != LocalDiscovery::Mode::Client) {
            discovery_retry_timer_ -= frame_dt;
            if (discovery_retry_timer_ <= 0.0f) {
                discovery_.startClient();
                // Whether bind succeeded or not, back off for a second so we
                // don't spam errno logs each frame on persistent failure.
                discovery_retry_timer_ = 1.0f;
            }
        }
        // Drain incoming announces using the SAME clock we'll use for the
        // staleness check in getKnownHosts (raylib's GetTime). Mixing
        // sources here caused the JOIN list to flicker / stay empty even
        // with announces live on the wire.
        const double now_sec = GetTime();
        discovery_.pollIncoming(now_sec);
        // Build the visible list from live results. The lobby's `discovered_`
        // is rebuilt every frame -- the underlying deque is tiny (<10) so
        // there's no point caching.
        std::vector<DiscoveredHostEntry> live;
        discovery_.getKnownHosts(live, now_sec);
        discovered_.clear();
        discovered_.reserve(live.size());
        for (const auto& e : live) discovered_.push_back(toDiscoveredHost(e));
    } else {
        if (discovery_.mode() != LocalDiscovery::Mode::Idle) {
            discovery_.stop();
        }
    }
}

void LocalLobby::reset() {
    sub_state_              = LobbySubState::Picker;
    refresh_remaining_      = 0.0f;
    discovery_retry_timer_  = 0.0f;
    discovery_.stop();
    players_.clear();
    host_status_.clear();
    join_status_.clear();
    remote_host_name_.clear();
}

LocalLobbyAction LocalLobby::render(int sw, int sh) {
    // Shared backdrop -- vertical gradient + side accent stripe + corner orbs
    // tinted by the active sub-state. Replaces the old flat fill so the lobby
    // feels like one cohesive surface.
    drawLobbyBackdrop(sw, sh, anim_time_, subStateAccent(sub_state_));

    LocalLobbyAction action = LocalLobbyAction::None;

    switch (sub_state_) {
        case LobbySubState::Picker:       renderPicker(sw, sh, action);       break;
        case LobbySubState::HostWaiting:  renderHostWaiting(sw, sh, action);  break;
        case LobbySubState::JoinBrowsing: renderJoinBrowsing(sw, sh, action); break;
        case LobbySubState::JoinWaiting:  renderJoinWaiting(sw, sh, action);  break;
    }

    return action;
}

void LocalLobby::renderPicker(int sw, int sh, LocalLobbyAction& action) {
    const float ui = uiScale(sw, sh);

    // ---- Eyebrow tag ----
    drawLobbyEyebrow(sw, static_cast<int>(sh * 0.10f),
                     "LOCAL  MULTIPLAYER  --  PICK  YOUR  ROLE",
                     Color{160, 195, 230, 255});

    // ---- Title with breathing halo ----
    const int title_fs = uiPx(sw, sh, 64);
    drawLobbyTitle(sw, sh, "LOCAL GAME", title_fs,
                   static_cast<int>(sh * 0.15f),
                   subStateAccent(LobbySubState::Picker),
                   anim_time_);

    // ---- Tagline ----
    {
        const char* tagline = "host a match or join one on your LAN";
        int g_size = uiPx(sw, sh, 16);
        int gw = MeasureText(tagline, g_size);
        int gy = static_cast<int>(sh * 0.15f) + title_fs + uiPx(sw, sh, 30);
        DrawText(tagline, (sw - gw) / 2 + 1, gy + 1, g_size,
                 Color{0, 0, 0, 150});
        DrawText(tagline, (sw - gw) / 2,     gy,     g_size,
                 Color{185, 205, 225, 220});
    }

    // ---- Two-card body (HOST | JOIN) ----
    const int card_w   = uiPx(sw, sh, 310);
    const int card_h   = uiPx(sw, sh, 215);
    const int card_gap = uiPx(sw, sh,  28);
    const int card_y   = static_cast<int>(sh * 0.42f);
    const int total_w  = card_w * 2 + card_gap;
    const int left_x   = (sw - total_w) / 2;
    const int right_x  = left_x + card_w + card_gap;

    // Shared scaled paddings.
    const int pad_l       = uiPx(sw, sh, 16);
    const int pip_cx      = uiPx(sw, sh, 30);
    const int pip_cy      = uiPx(sw, sh, 28);
    const int role_x      = uiPx(sw, sh, 50);
    const int role_y      = uiPx(sw, sh, 14);
    const int sub_role_y  = uiPx(sw, sh, 44);
    const int header_rule = uiPx(sw, sh, 50);
    const int row1_y      = uiPx(sw, sh, 78);
    const int row2_y      = uiPx(sw, sh, 102);
    const int row3_y      = uiPx(sw, sh, 122);
    const int role_fs     = uiPx(sw, sh, 30);
    const int sub_role_fs = uiPx(sw, sh, 12);
    const int desc_fs_a   = uiPx(sw, sh, 15);
    const int desc_fs_b   = uiPx(sw, sh, 13);
    const int pip_r_big   = std::max(4.0f, 7.0f * ui);
    const int pip_r_inner = std::max(2.0f, 3.5f * ui);
    const int btn_inset   = uiPx(sw, sh, 24);
    const int btn_h       = uiPx(sw, sh, 40);
    const int btn_bot     = uiPx(sw, sh, 56);
    const int btn_fs      = uiPx(sw, sh, 18);

    // ---- HOST card (green) ----
    {
        Rectangle r{(float)left_x, (float)card_y, (float)card_w, (float)card_h};
        DrawRectangleGradientV(left_x, card_y, card_w, card_h,
                               Color{28, 58, 50, 240},
                               Color{16, 34, 30, 240});
        DrawRectangleRoundedLines(r, 0.06f, 8, Color{120, 200, 170, 130});

        DrawRectangle(left_x + pad_l, card_y + header_rule, card_w - 2 * pad_l, 1,
                      Color{120, 200, 170, 90});

        DrawCircle(left_x + pip_cx, card_y + pip_cy, pip_r_big,    Color{120, 200, 150, 230});
        DrawCircle(left_x + pip_cx, card_y + pip_cy, pip_r_inner,  Color{210, 250, 220, 240});
        DrawText("HOST", left_x + role_x, card_y + role_y, role_fs,
                 Color{220, 245, 230, 245});
        DrawText("server", left_x + role_x, card_y + sub_role_y, sub_role_fs,
                 Color{160, 210, 180, 220});

        DrawText("Open a game on this machine.",
                 left_x + btn_inset, card_y + row1_y, desc_fs_a, Color{215, 230, 220, 230});
        DrawText("Pick the match length, bot count,",
                 left_x + btn_inset, card_y + row2_y, desc_fs_b, Color{180, 200, 190, 210});
        DrawText("and player cap before launching.",
                 left_x + btn_inset, card_y + row3_y, desc_fs_b, Color{180, 200, 190, 210});

        Rectangle btn_r{(float)(left_x + btn_inset),
                        (float)(card_y + card_h - btn_bot),
                        (float)(card_w - 2 * btn_inset), (float)btn_h};
        if (drawButton(btn_r, "START  LOBBY  >", btn_fs,
                       Color{55, 145, 105, 255},
                       Color{240, 250, 245, 255})) {
            sub_state_ = LobbySubState::HostWaiting;
            action     = LocalLobbyAction::BeginHosting;
            swallowNextClick();
        }
    }

    // ---- JOIN card (blue) ----
    {
        Rectangle r{(float)right_x, (float)card_y, (float)card_w, (float)card_h};
        DrawRectangleGradientV(right_x, card_y, card_w, card_h,
                               Color{28, 44, 80, 240},
                               Color{16, 26, 52, 240});
        DrawRectangleRoundedLines(r, 0.06f, 8, Color{130, 175, 230, 130});

        DrawRectangle(right_x + pad_l, card_y + header_rule, card_w - 2 * pad_l, 1,
                      Color{130, 175, 230, 90});

        DrawCircleLines(right_x + pip_cx, card_y + pip_cy, pip_r_big,
                        Color{130, 175, 230, 230});
        DrawCircle(right_x + pip_cx, card_y + pip_cy,
                   std::max(2.0f, 3.0f * ui),
                   Color{200, 225, 250, 240});
        DrawText("JOIN", right_x + role_x, card_y + role_y, role_fs,
                 Color{225, 235, 250, 245});
        DrawText("client", right_x + role_x, card_y + sub_role_y, sub_role_fs,
                 Color{170, 195, 225, 220});

        DrawText("Connect to a host on the LAN.",
                 right_x + btn_inset, card_y + row1_y, desc_fs_a, Color{220, 230, 245, 230});
        DrawText("Auto-discovers nearby games or",
                 right_x + btn_inset, card_y + row2_y, desc_fs_b, Color{180, 195, 220, 210});
        DrawText("enter an IP address directly.",
                 right_x + btn_inset, card_y + row3_y, desc_fs_b, Color{180, 195, 220, 210});

        Rectangle btn_r{(float)(right_x + btn_inset),
                        (float)(card_y + card_h - btn_bot),
                        (float)(card_w - 2 * btn_inset), (float)btn_h};
        if (drawButton(btn_r, "FIND  GAMES  >", btn_fs,
                       Color{60, 110, 170, 255},
                       Color{235, 245, 255, 255})) {
            sub_state_  = LobbySubState::JoinBrowsing;
            discovered_.clear();
            swallowNextClick();
        }
    }

    // ---- BACK button (centered below cards) ----
    {
        const int back_w  = uiPx(sw, sh, 200);
        const int back_h  = uiPx(sw, sh, 50);
        const int back_fs = uiPx(sw, sh, 22);
        Rectangle bb{(float)(sw / 2 - back_w / 2),
                     (float)(card_y + card_h + uiPx(sw, sh, 30)),
                     (float)back_w, (float)back_h};
        if (drawButton(bb, "BACK", back_fs,
                       Color{42, 50, 72, 255}, Color{220, 225, 245, 230})) {
            action = LocalLobbyAction::BackToRoyaleMenu;
        }
    }

    // ---- Footer ----
    {
        const char* foot = "press  ESC  to  go  back";
        int fs = uiPx(sw, sh, 13);
        int fw = MeasureText(foot, fs);
        DrawText(foot, (sw - fw) / 2, sh - uiPx(sw, sh, 28), fs,
                 Color{140, 155, 180, 180});
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        action = LocalLobbyAction::BackToRoyaleMenu;
    }
}

void LocalLobby::renderPlayerListPanel(int panel_x, int panel_y, int panel_w, int panel_h) {
    // Two-tone gradient backdrop for the panel (depth cue).
    DrawRectangleGradientV(panel_x, panel_y, panel_w, panel_h,
                           Color{24, 32, 52, 240}, Color{14, 20, 34, 240});
    DrawRectangleRoundedLines(
        Rectangle{(float)panel_x, (float)panel_y, (float)panel_w, (float)panel_h},
        0.06f, 8, Color{120, 140, 180, 100});

    char title[40];
    std::snprintf(title, sizeof(title), "PLAYERS  (%d)", (int)players_.size());
    DrawText(title, panel_x + 18, panel_y + 14, 16, Color{200, 215, 240, 230});

    // Subtle horizontal rule under the title.
    DrawRectangle(panel_x + 18, panel_y + 36, panel_w - 36, 1,
                  Color{120, 140, 180, 60});

    const int row_y0 = panel_y + 48;
    const int row_h  = 30;
    const int max_rows = (panel_h - 72) / row_h;
    int rendered = 0;
    for (size_t i = 0; i < players_.size() && rendered < max_rows; ++i, ++rendered) {
        const auto& row = players_[i];
        int ry = row_y0 + rendered * row_h;

        // Status dot: green if we have a name, orange if it's still
        // "Joining..." / "Player" placeholder (the host hasn't received
        // ClientHello yet).
        bool has_real_name = !row.name.empty()
            && row.name != "Joining..."
            && row.name != "Player";
        Color dot_color = has_real_name
            ? Color{120, 220, 150, 230}
            : Color{230, 180, 90, 220};
        DrawCircle(panel_x + 28, ry + 12, 4.5f, dot_color);

        // Per-row text + role tags.
        char tag[16] = {0};
        if (row.is_self && row.is_host)      std::snprintf(tag, sizeof(tag), "you, host");
        else if (row.is_self)                std::snprintf(tag, sizeof(tag), "you");
        else if (row.is_host)                std::snprintf(tag, sizeof(tag), "host");

        Color name_c = row.is_self ? Color{255, 235, 170, 240}
                                   : Color{220, 230, 245, 230};
        if (row.is_host && !row.is_self) name_c = Color{180, 235, 210, 235};
        DrawText(row.name.c_str(), panel_x + 42, ry + 4, 18, name_c);

        if (tag[0] != 0) {
            char tag_buf[24];
            std::snprintf(tag_buf, sizeof(tag_buf), "(%s)", tag);
            int tw = MeasureText(tag_buf, 13);
            DrawText(tag_buf, panel_x + panel_w - tw - 18, ry + 8, 13,
                     Color{170, 185, 210, 200});
        }

        // Subtle row separator between consecutive rows.
        if (rendered > 0) {
            DrawRectangle(panel_x + 24, ry, panel_w - 48, 1,
                          Color{80, 95, 120, 50});
        }
    }
    if (players_.empty()) {
        const char* empty = "waiting for players...";
        int fw = MeasureText(empty, 16);
        // Pulsing alpha so the message clearly reads as "alive / listening".
        float p = (std::sin(anim_time_ * 2.0f) + 1.0f) * 0.5f;
        unsigned char a = static_cast<unsigned char>(140 + p * 80);
        DrawText(empty, panel_x + (panel_w - fw) / 2,
                 panel_y + panel_h / 2 - 8, 16,
                 Color{160, 175, 200, a});
    }
}

// Preset arrays for the host settings panel. Indices map 1:1 between the
// `*_labels` and `*_values` arrays so a click on label[i] writes value[i] into
// match_settings_. Keep these in sync if you reorder.
namespace {
constexpr int kDurationOptionCount = 5;
const char* const kDurationLabels[kDurationOptionCount] = {
    "1m", "3m", "5m", "10m", "ENDLESS"
};
constexpr int kDurationValues[kDurationOptionCount] = {60, 180, 300, 600, 0};

constexpr int kPlayerCountOptionCount = 4;
const char* const kPlayerCountLabels[kPlayerCountOptionCount] = {
    "2", "4", "8", "16"
};
constexpr int kPlayerCountValues[kPlayerCountOptionCount] = {2, 4, 8, 16};

constexpr int kBotCountOptionCount = 4;
const char* const kBotCountLabels[kBotCountOptionCount] = {
    "OFF", "5", "10", "25"
};
constexpr int kBotCountValues[kBotCountOptionCount] = {0, 5, 10, 25};

// Helper: pick the preset index closest to `current`. Falls back to 0 if
// `current` is below the smallest preset, or option_count-1 if above the
// largest. Used so the host can tune values via tuning.ini and the lobby
// reflects the active preset.
int closestIndex(int current, const int* values, int count) {
    int best_i   = 0;
    int best_err = std::abs(current - values[0]);
    for (int i = 1; i < count; ++i) {
        int e = std::abs(current - values[i]);
        if (e < best_err) { best_err = e; best_i = i; }
    }
    return best_i;
}
} // namespace

void LocalLobby::renderHostWaiting(int sw, int sh, LocalLobbyAction& action) {
    // ---- Eyebrow ----
    drawLobbyEyebrow(sw, static_cast<int>(sh * 0.03f),
                     "LIVE  --  WAITING  FOR  PLAYERS",
                     Color{140, 220, 175, 255});

    // ---- Title with breathing halo ----
    const int ty = static_cast<int>(sh * 0.07f);
    drawLobbyTitle(sw, sh, "HOSTING", uiPx(sw, sh, 52), ty,
                   subStateAccent(LobbySubState::HostWaiting),
                   anim_time_);

    // ---- Status line + animated dot trail ----
    const int t_size = uiPx(sw, sh, 52);
    const char* status_text = !host_status_.empty()
        ? host_status_.c_str()
        : "binding socket...";
    const int s_fs = uiPx(sw, sh, 16);
    int sw_w = MeasureText(status_text, s_fs);
    DrawText(status_text, (sw - sw_w) / 2, ty + t_size + uiPx(sw, sh, 18), s_fs,
             Color{200, 215, 230, 220});
    {
        float k = std::fmod(anim_time_ * 2.0f, 3.0f);
        int dots = static_cast<int>(k) + 1;
        char trail[8] = {0};
        for (int i = 0; i < dots && i < 7; ++i) trail[i] = '.';
        int t_fs = uiPx(sw, sh, 18);
        int t_w  = MeasureText(trail, t_fs);
        DrawText(trail, sw / 2 - t_w / 2,
                 ty + t_size + uiPx(sw, sh, 18) + s_fs + uiPx(sw, sh, 6),
                 t_fs, Color{180, 200, 230, 200});
    }

    // ---- Two-column body: settings (left) | player list (right) ----
    const int body_top    = static_cast<int>(sh * 0.28f);
    const int body_height = uiPx(sw, sh, 360);
    const int col_gap     = uiPx(sw, sh,  20);
    const int col_w       = uiPx(sw, sh, 460);
    const int total_w     = col_w * 2 + col_gap;
    const int left_x      = (sw - total_w) / 2;
    const int right_x     = left_x + col_w + col_gap;

    // -------- LEFT: Match settings panel --------
    {
        DrawRectangleGradientV(left_x, body_top, col_w, body_height,
                               Color{22, 30, 48, 235}, Color{14, 20, 34, 235});
        DrawRectangleRoundedLines(
            Rectangle{(float)left_x, (float)body_top, (float)col_w, (float)body_height},
            0.05f, 8, Color{120, 140, 180, 90});
        DrawText("MATCH SETTINGS",
                 left_x + uiPx(sw, sh, 18), body_top + uiPx(sw, sh, 14),
                 uiPx(sw, sh, 16),
                 Color{200, 215, 240, 230});

        const int row_h     = uiPx(sw, sh, 44);
        const int row_label = uiPx(sw, sh, 22);
        const int row_step  = row_h + row_label + uiPx(sw, sh, 18);
        const int row_inset = uiPx(sw, sh, 22);
        int row_y           = body_top + uiPx(sw, sh, 60);

        {
            int idx = closestIndex(match_settings_.match_duration_sec,
                                    kDurationValues, kDurationOptionCount);
            Rectangle row{(float)(left_x + row_inset), (float)row_y,
                          (float)(col_w - 2 * row_inset), (float)row_h};
            if (drawPresetRow(row, "Match duration",
                              kDurationLabels, kDurationOptionCount, &idx)) {
                match_settings_.match_duration_sec = kDurationValues[idx];
            }
            row_y += row_step;
        }
        {
            int idx = closestIndex(match_settings_.max_players,
                                    kPlayerCountValues, kPlayerCountOptionCount);
            Rectangle row{(float)(left_x + row_inset), (float)row_y,
                          (float)(col_w - 2 * row_inset), (float)row_h};
            if (drawPresetRow(row, "Max players",
                              kPlayerCountLabels, kPlayerCountOptionCount, &idx)) {
                match_settings_.max_players = kPlayerCountValues[idx];
            }
            row_y += row_step;
        }
        {
            int idx = closestIndex(match_settings_.bot_count,
                                    kBotCountValues, kBotCountOptionCount);
            Rectangle row{(float)(left_x + row_inset), (float)row_y,
                          (float)(col_w - 2 * row_inset), (float)row_h};
            if (drawPresetRow(row, "Bots",
                              kBotCountLabels, kBotCountOptionCount, &idx)) {
                match_settings_.bot_count = kBotCountValues[idx];
            }
            row_y += row_step;
        }

        char summary[160];
        if (match_settings_.match_duration_sec <= 0) {
            std::snprintf(summary, sizeof(summary),
                          "endless FFA  -  %d players max  -  %d bots",
                          match_settings_.max_players,
                          match_settings_.bot_count);
        } else {
            std::snprintf(summary, sizeof(summary),
                          "%d min match  -  %d players max  -  %d bots",
                          match_settings_.match_duration_sec / 60,
                          match_settings_.max_players,
                          match_settings_.bot_count);
        }
        const int sum_fs = uiPx(sw, sh, 13);
        int fw = MeasureText(summary, sum_fs);
        DrawText(summary, left_x + (col_w - fw) / 2,
                 body_top + body_height - uiPx(sw, sh, 26), sum_fs,
                 Color{180, 195, 220, 210});
    }

    // -------- RIGHT: Player list panel --------
    renderPlayerListPanel(right_x, body_top, col_w, body_height);

    // ---- Action buttons ----
    const int btn_w       = uiPx(sw, sh, 220);
    const int btn_h       = uiPx(sw, sh,  58);
    const int btn_gap     = uiPx(sw, sh,  18);
    const int btn_total_w = btn_w * 2 + btn_gap;
    const int btn_y       = body_top + body_height + uiPx(sw, sh, 24);
    const int btn_fs      = uiPx(sw, sh, 24);
    const bool can_start = (players_.size() >= 1);

    if (drawButton(
            Rectangle{(float)(sw / 2 - btn_total_w / 2), (float)btn_y,
                      (float)btn_w, (float)btn_h},
            "START GAME", btn_fs,
            can_start ? Color{60, 140, 90, 255} : Color{55, 80, 70, 255},
            Color{240, 250, 245, 255},
            can_start)) {
        action = LocalLobbyAction::StartLocalHost;
    }
    if (drawButton(
            Rectangle{(float)(sw / 2 - btn_total_w / 2 + btn_w + btn_gap), (float)btn_y,
                      (float)btn_w, (float)btn_h},
            "CANCEL", btn_fs,
            Color{82, 52, 60, 255}, Color{240, 220, 220, 230})) {
        sub_state_ = LobbySubState::Picker;
        action     = LocalLobbyAction::LeaveHostingLobby;
        swallowNextClick();
    }

    // ---- Hint footer ----
    {
        const char* foot = "peers can join while you wait; START GAME launches everyone at once.";
        int fs = uiPx(sw, sh, 13);
        int fw = MeasureText(foot, fs);
        DrawText(foot, (sw - fw) / 2, sh - uiPx(sw, sh, 26), fs,
                 Color{140, 150, 170, 190});
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        sub_state_ = LobbySubState::Picker;
        action     = LocalLobbyAction::LeaveHostingLobby;
    }
}

void LocalLobby::renderJoinBrowsing(int sw, int sh, LocalLobbyAction& action) {
    // ---- Eyebrow + title with halo ----
    drawLobbyEyebrow(sw, static_cast<int>(sh * 0.05f),
                     "SCANNING  THE  LAN  --  PICK  A  GAME",
                     Color{150, 190, 240, 255});
    drawLobbyTitle(sw, sh, "JOIN A GAME", uiPx(sw, sh, 48),
                   static_cast<int>(sh * 0.10f),
                   subStateAccent(LobbySubState::JoinBrowsing),
                   anim_time_);

    // ---- Discovered-host list panel ----
    const int panel_w = 520;
    const int panel_h = 240;
    const int panel_x = (sw - panel_w) / 2;
    const int panel_y = static_cast<int>(sh * 0.28f);
    // Two-tone gradient + accent border to match the player-list panel
    // we render in HostWaiting / JoinWaiting.
    DrawRectangleGradientV(panel_x, panel_y, panel_w, panel_h,
                           Color{24, 32, 52, 240}, Color{14, 20, 34, 240});
    DrawRectangleRoundedLines(
        Rectangle{(float)panel_x, (float)panel_y, (float)panel_w, (float)panel_h},
        0.06f, 8, Color{130, 175, 230, 110});
    // Header strip.
    char header[40];
    std::snprintf(header, sizeof(header), "DISCOVERED HOSTS  (%d)",
                  (int)discovered_.size());
    DrawText(header, panel_x + 18, panel_y + 14, 16,
             Color{200, 215, 240, 230});
    DrawRectangle(panel_x + 18, panel_y + 36, panel_w - 36, 1,
                  Color{130, 175, 230, 70});

    // REFRESH button (top-right of the panel).
    {
        const int r_w = 110, r_h = 32;
        Rectangle rb{(float)(panel_x + panel_w - r_w - 12),
                     (float)(panel_y + 10), (float)r_w, (float)r_h};
        const bool busy = refresh_remaining_ > 0.0f;
        if (drawButton(rb, busy ? "..." : "REFRESH", 15,
                       Color{50, 70, 110, 255}, Color{220, 230, 250, 230}, !busy)) {
            // Visual flash only -- the listener is always running while this
            // screen is up, so the list refreshes automatically as host
            // announces arrive (every ~1s on the host side). Click is mostly
            // there for "I clicked it, give me feedback".
            refresh_remaining_ = 0.6f;
            discovered_.clear(); // force a re-fill from the next poll cycle
        }
    }

    if (discovered_.empty()) {
        // Pulsing alpha so the "looking..." message reads as live, not stuck.
        float p = (std::sin(anim_time_ * 1.8f) + 1.0f) * 0.5f;
        unsigned char a = static_cast<unsigned char>(150 + p * 80);
        const char* empty_msg = "scanning the network...";
        int e_fs = 16;
        int e_w  = MeasureText(empty_msg, e_fs);
        DrawText(empty_msg, panel_x + (panel_w - e_w) / 2,
                 panel_y + panel_h / 2 - e_fs / 2 - 8, e_fs,
                 Color{170, 195, 230, a});

        // Sub-hint with the manual-entry suggestion.
        const char* sub = "no games found yet -- try entering an address below";
        int s_fs = 12;
        int sw_w = MeasureText(sub, s_fs);
        DrawText(sub, panel_x + (panel_w - sw_w) / 2,
                 panel_y + panel_h / 2 + e_fs / 2 + 4, s_fs,
                 Color{140, 155, 180, 200});
    } else {
        // Render each discovered host as a card-style row. Each row has:
        //   - a small status pip (green = live host visible)
        //   - host name (primary text)
        //   - meta line under name (address + player count)
        //   - JOIN button on the right
        // Plus a subtle bottom-border separator between rows.
        const int row_h    = 44;
        const int rows_top = panel_y + 48;
        const int max_rows = std::min<int>(discovered_.size(),
                                            (panel_h - 56) / row_h);
        for (int i = 0; i < max_rows; ++i) {
            const auto& h = discovered_[i];
            int ry = rows_top + i * row_h;

            // Status pip -- pulsing alpha so it reads as "live signal".
            float p = 0.5f + 0.5f * std::sin(anim_time_ * 2.4f + i * 0.5f);
            unsigned char pip_a = static_cast<unsigned char>(180 + p * 75);
            DrawCircle(panel_x + 28, ry + 14, 5.0f,
                       Color{120, 220, 150, pip_a});
            DrawCircle(panel_x + 28, ry + 14, 2.5f,
                       Color{210, 250, 220, 240});

            // Host name + meta stacked.
            DrawText(h.name.c_str(), panel_x + 46, ry + 2, 16,
                     Color{225, 235, 250, 240});
            char meta[80];
            std::snprintf(meta, sizeof(meta), "%s   --   %d/%d players",
                          h.address.c_str(),
                          h.player_count, h.max_players);
            DrawText(meta, panel_x + 46, ry + 22, 12,
                     Color{170, 185, 210, 210});

            // Per-row JOIN button.
            Rectangle jb{(float)(panel_x + panel_w - 100), (float)(ry + 6),
                         84.0f, 30.0f};
            if (drawButton(jb, "JOIN  >", 14,
                           Color{60, 110, 170, 255},
                           Color{235, 245, 255, 240})) {
                join_input_ = h.address;
                sub_state_  = LobbySubState::JoinWaiting;
                action      = LocalLobbyAction::BeginJoining;
                swallowNextClick();
            }

            // Subtle separator below each row except the last.
            if (i + 1 < max_rows) {
                DrawRectangle(panel_x + 24, ry + row_h - 2, panel_w - 48, 1,
                              Color{90, 110, 140, 55});
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
            sub_state_ = LobbySubState::JoinWaiting;
            action     = LocalLobbyAction::BeginJoining;
            swallowNextClick();
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

void LocalLobby::renderJoinWaiting(int sw, int sh, LocalLobbyAction& action) {
    // ---- Eyebrow + title with halo ----
    drawLobbyEyebrow(sw, static_cast<int>(sh * 0.08f),
                     "CONNECTED  --  WAITING  FOR  START",
                     Color{170, 200, 240, 255});
    const int ty     = static_cast<int>(sh * 0.12f);
    const int t_size = uiPx(sw, sh, 44);
    drawLobbyTitle(sw, sh, "WAITING FOR HOST", t_size, ty,
                   subStateAccent(LobbySubState::JoinWaiting),
                   anim_time_);

    // ---- Status line ----
    // Filled in by the outer loop -- "connecting to host:port" / "connected; waiting
    // for START" / "lost host" etc.
    const char* status_text = !join_status_.empty()
        ? join_status_.c_str()
        : "connecting...";
    int s_fs = 16;
    int sw_w = MeasureText(status_text, s_fs);
    DrawText(status_text, (sw - sw_w) / 2, ty + t_size + 18, s_fs,
             Color{200, 215, 230, 220});

    // "in <host_name>'s lobby" line, once we have the host's name.
    if (!remote_host_name_.empty()) {
        char host_line[80];
        std::snprintf(host_line, sizeof(host_line), "in %s's lobby",
                      remote_host_name_.c_str());
        int hw = MeasureText(host_line, 14);
        DrawText(host_line, (sw - hw) / 2, ty + t_size + 18 + s_fs + 8, 14,
                 Color{170, 200, 220, 220});
    }

    // Animated dot-trail.
    {
        float k = std::fmod(anim_time_ * 2.0f, 3.0f);
        int dots = static_cast<int>(k) + 1;
        char trail[8] = {0};
        for (int i = 0; i < dots && i < 7; ++i) trail[i] = '.';
        int t_fs = 22;
        int t_w  = MeasureText(trail, t_fs);
        DrawText(trail, sw / 2 - t_w / 2,
                 ty + t_size + 70, t_fs, Color{180, 200, 230, 220});
    }

    // ---- Player list panel ----
    const int panel_w = 460;
    const int panel_h = 220;
    const int panel_x = (sw - panel_w) / 2;
    const int panel_y = static_cast<int>(sh * 0.38f);
    renderPlayerListPanel(panel_x, panel_y, panel_w, panel_h);

    // ---- Disconnect button ----
    {
        const int back_w = 220;
        const int back_h = 56;
        Rectangle bb{(float)(sw / 2 - back_w / 2),
                     (float)(panel_y + panel_h + 28),
                     (float)back_w, (float)back_h};
        if (drawButton(bb, "LEAVE LOBBY", 20,
                       Color{82, 52, 60, 255}, Color{240, 220, 220, 230})) {
            sub_state_ = LobbySubState::Picker;
            action     = LocalLobbyAction::LeaveJoiningLobby;
            swallowNextClick();
        }
    }

    // ---- Footer hint ----
    {
        const char* foot = "the host will press START GAME when everyone is in.";
        int fs = 13;
        int fw = MeasureText(foot, fs);
        DrawText(foot, (sw - fw) / 2, sh - 30, fs,
                 Color{140, 150, 170, 190});
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        sub_state_ = LobbySubState::Picker;
        action     = LocalLobbyAction::LeaveJoiningLobby;
    }
}

} // namespace cr
