#pragma once

#include "raylib.h"

#include <string>
#include <vector>

namespace cr {

// What state the lobby UI is in. The outer loop renders the same LocalLobby instance
// across multiple frames, so the sub-state lives here rather than in the host loop.
enum class LobbySubState {
    Picker,        // top-level: pick HOST or JOIN
    HostWaiting,   // hosting a game, watching the connected-players list
    JoinBrowsing,  // joining: list of discovered LAN hosts + direct-IP entry
};

// Action returned by LocalLobby::render() that tells the outer loop what to do next.
enum class LocalLobbyAction {
    None,
    BackToRoyaleMenu,
    StartLocalHost,   // hand off to runMatch in LocalHost mode (with current settings)
    StartLocalJoin,   // hand off to runMatch in LocalClient mode (target host in
                      // `joinTargetAddress()`)
    Quit,
};

// Stub: one entry per discovered LAN host. The skeleton populates this with placeholder
// strings; the real implementation will fill it from a UDP broadcast discovery service.
struct DiscoveredHost {
    std::string name;          // user-visible label, e.g. "Sagar's Game (16k world)"
    std::string address;       // dotted-quad or hostname; "192.168.1.42:7456"
    int         player_count = 0;
    int         max_players  = 8;
};

// Two-screen lobby that fronts local multiplayer. The Picker screen is the entry
// point (HOST / JOIN / BACK). HOST navigates to HostWaiting and reads the connected
// peer list (empty in the skeleton). JOIN navigates to JoinBrowsing and shows the
// discovered-host list + a manual IP field.
class LocalLobby {
public:
    void              update(float frame_dt, int screen_w, int screen_h);
    LocalLobbyAction  render(int screen_w, int screen_h);

    // Used by the outer loop when the user returns here from a finished match. Resets
    // the sub-state to Picker so the user lands cleanly on the entry screen.
    void reset();

    // Currently-typed manual-join address. The outer loop reads this when the user
    // hits "Join by IP" so it can pass the address to NetworkTransport::connect.
    const std::string& joinTargetAddress() const { return join_input_; }

private:
    void renderPicker(int sw, int sh, LocalLobbyAction& action);
    void renderHostWaiting(int sw, int sh, LocalLobbyAction& action);
    void renderJoinBrowsing(int sw, int sh, LocalLobbyAction& action);

    LobbySubState sub_state_ = LobbySubState::Picker;
    float         anim_time_ = 0.0f;

    // Manual-join address field. Skeleton starts at "127.0.0.1:7456" so users testing
    // two instances on one machine have a workable default.
    std::string   join_input_ = "127.0.0.1:7456";
    bool          join_input_focused_ = false;

    // Stub discovery results. Real implementation will refresh this from a LAN
    // broadcast service running on a background thread; for the skeleton we fill it
    // with a hardcoded example so the layout is visible.
    std::vector<DiscoveredHost> discovered_;
    float                       refresh_remaining_ = 0.0f; // visual feedback for the
                                                           // REFRESH button (stub)
};

} // namespace cr
