#pragma once

#include "core/Types.h"
#include "raylib.h"
#include "transport/LocalDiscovery.h"

#include <string>
#include <vector>

namespace cr {

// What state the lobby UI is in. The outer loop renders the same LocalLobby instance
// across multiple frames, so the sub-state lives here rather than in the host loop.
enum class LobbySubState {
    Picker,        // top-level: pick HOST or JOIN
    HostWaiting,   // hosting a game, watching the connected-players list
    JoinBrowsing,  // joining: list of discovered LAN hosts + direct-IP entry
    JoinWaiting,   // joining: connected to a host, waiting in their lobby for START
};

// Action returned by LocalLobby::render() that tells the outer loop what to do next.
// Most actions describe what the lobby UI is *requesting*; the outer loop owns the
// NetworkTransport so it decides whether to honour the request.
enum class LocalLobbyAction {
    None,
    BackToRoyaleMenu,
    BeginHosting,     // user pressed HOST -- outer loop should host() the socket and
                      // start the LAN announcer. Lobby has already transitioned to
                      // HostWaiting sub-state.
    BeginJoining,     // user clicked a JOIN row or pressed JOIN by IP -- outer loop
                      // should call connect() on `joinTargetAddress()`. Lobby has
                      // already transitioned to JoinWaiting sub-state.
    LeaveHostingLobby,// user backed out of HostWaiting -- outer loop should drop the
                      // host socket and stop the LAN announcer.
    LeaveJoiningLobby,// user backed out of JoinWaiting -- outer loop should disconnect.
    StartLocalHost,   // host pressed START GAME -- hand off to runMatch in LocalHost
                      // mode reusing the live transport + connected peer set.
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

// One row in the lobby's "players in this match" panel. The outer loop builds the
// list each frame from its peer / name maps and pushes it via setPlayerList().
struct LobbyPlayerRow {
    PlayerId    id      = INVALID_PLAYER;
    std::string name;
    bool        is_self = false;     // bolded + tagged "(you)"
    bool        is_host = false;     // tagged "(host)"
};

// Host-configured match parameters chosen in the lobby's HostWaiting panel.
// Applied to the tuning at runMatch entry (and restored on exit) so they only
// affect the current match. Defaults match the existing Royale behavior so
// hosts who never touch the panel get the same experience as before.
struct MatchSettings {
    // Match length in seconds. 0 = endless (no timer; world keeps spinning
    // until everyone leaves). Otherwise clamped to [30, 3600].
    int  match_duration_sec = 300;   // 5 min default

    // Soft cap on the number of players (host + connected peers). New
    // connections beyond this are accepted by ENet then immediately
    // disconnected with a log line. 0 = unlimited (still clamped to 16).
    int  max_players        = 8;

    // Bots to spawn at match start. Default 0 (pure-human Royale). Higher
    // values fill out small lobbies. The host can also use the `bots N`
    // dev console mid-match to override.
    int  bot_count          = 0;

    static constexpr int kHardPlayerCap = 16;
};

// Two-screen lobby that fronts local multiplayer. The Picker screen is the entry
// point (HOST / JOIN / BACK). HOST navigates to HostWaiting and reads the connected
// peer list (empty in the skeleton). JOIN navigates to JoinBrowsing and shows the
// discovered-host list + a manual IP field. Selecting a host transitions to
// JoinWaiting where the joiner waits for the host to press START GAME.
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

    LobbySubState subState() const { return sub_state_; }

    // Outer loop pushes the current player roster each frame (host + connected
    // peers + self for the joining side). The lobby renders this in the HostWaiting
    // / JoinWaiting panels.
    void setPlayerList(std::vector<LobbyPlayerRow> rows) { players_ = std::move(rows); }

    // Outer loop pushes status text + the host's bind port (HostWaiting) or the
    // target address (JoinWaiting). The lobby renders this in the status line.
    void setHostStatus(const std::string& text) { host_status_ = text; }
    void setJoinStatus(const std::string& text) { join_status_ = text; }

    // Set by main once the client receives the host's lobby welcome. Used to
    // render "connected to <host_name>" in JoinWaiting.
    void setRemoteHostName(const std::string& name) { remote_host_name_ = name; }

    // Host-side match settings. The main loop hands these to runMatch when
    // the host hits START GAME. Persisted across HostWaiting visits within a
    // session (reset only on lobby::reset()).
    const MatchSettings& matchSettings() const { return match_settings_; }
    MatchSettings&       matchSettings() { return match_settings_; }

private:
    void renderPicker(int sw, int sh, LocalLobbyAction& action);
    void renderHostWaiting(int sw, int sh, LocalLobbyAction& action);
    void renderJoinBrowsing(int sw, int sh, LocalLobbyAction& action);
    void renderJoinWaiting(int sw, int sh, LocalLobbyAction& action);
    void renderPlayerListPanel(int panel_x, int panel_y, int panel_w, int panel_h);

    LobbySubState sub_state_ = LobbySubState::Picker;
    float         anim_time_ = 0.0f;

    // Manual-join address field. Skeleton starts at "127.0.0.1:7456" so users testing
    // two instances on one machine have a workable default.
    std::string   join_input_ = "127.0.0.1:7456";
    bool          join_input_focused_ = false;

    // Live LAN discovery results. The LocalDiscovery socket runs the moment the
    // user enters the JoinBrowsing sub-state and stops when they go back or
    // leave the lobby entirely. Each frame we copy out the current host list
    // into `discovered_` for the render code to draw.
    LocalDiscovery              discovery_;
    std::vector<DiscoveredHost> discovered_;
    float                       refresh_remaining_ = 0.0f; // visual feedback for
                                                           // the REFRESH button
    // Rate-limiter for the lazy startClient retry. If the bind keeps failing
    // (port already in use, sandbox denial, etc.) we'd otherwise spam the
    // terminal with errno output every frame. Cooldown to one retry per second.
    float                       discovery_retry_timer_ = 0.0f;

    // Live roster pushed in from the outer loop each frame. The lobby owns
    // nothing here -- it just renders.
    std::vector<LobbyPlayerRow> players_;
    std::string                 host_status_;        // HostWaiting status line
    std::string                 join_status_;        // JoinWaiting status line
    std::string                 remote_host_name_;   // host's display name (joiner)

    // Host-configured match parameters; rendered in the HostWaiting side panel.
    // The selected preset indices below mirror these (kept in sync inside
    // renderHostWaiting so the click handlers can flip both at once).
    MatchSettings               match_settings_;
};

} // namespace cr
