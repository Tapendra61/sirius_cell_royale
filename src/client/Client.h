#pragma once

#include "Camera.h"
#include "DevConsole.h"
#include "Hud.h"
#include "Interpolator.h"
#include "Renderer.h"
#include "core/Command.h"
#include "core/Events.h"
#include "core/Snapshot.h"
#include "feel/Audio.h"
#include "feel/ChromaShift.h"
#include "feel/Hitstop.h"
#include "feel/Particles.h"
#include "feel/Popups.h"
#include "feel/ScreenShake.h"
#include "meta/SaveFile.h"
#include "platform/Input.h"
#include "sim/World.h"

#include <vector>

namespace cr {

// Owns the on-screen pieces (camera, renderer, interpolator, dev console, feel layer,
// HUD) and the per-frame input -> command translation.
class Client {
public:
    Client(EntityId watched_cell, PlayerId watched_player);
    ~Client();
    Client(const Client&)            = delete;
    Client& operator=(const Client&) = delete;

    // Input poll -> queue of Commands. Drained each frame by main with takeCommands().
    void pollFrame(int screen_w, int screen_h, Tick current_tick);

    std::vector<Command> takeCommands();

    void onSnapshot(Snapshot s) { interp_.push(std::move(s)); }
    // Exposed so the outer loop can read the latest received snapshot for paths
    // that bypass the local sim (LocalClient camera follow / status lookups).
    const Interpolator& interpolator() const { return interp_; }

    // Convert sim events into juice + HUD updates. Called once per sim tick by main.
    void onEvents(const std::vector<GameEvent>& events, World& world,
                  const Tuning& tuning);

    // Per-frame update of all client-side time-driven state (shake, particles, popups,
    // hitstop, HUD, death cam). Called before render.
    void updateFrame(float frame_dt, double now_sec, const Tuning& tuning);

    // Non-const because Summary screen buttons read mouse state and feed it back into
    // the phase state machine (consumed via consumeRespawnRequest / consumeReturnToMenuRequest).
    void render(int screen_w, int screen_h, float alpha, const Tuning& tuning,
                const World& world, Tick tick);

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

    // Phase 8 restart loop. main consumes the respawn request once Summary expires
    // (or the player clicks PLAY AGAIN on the summary panel). ReturnToMenu is consumed
    // by main's outer Menu<->Match loop -- the player clicked MAIN MENU on summary.
    GamePhase phase() const { return phase_; }
    bool      consumeRespawnRequest();
    bool      consumeReturnToMenuRequest();
    void      onPlayerRespawned(double now_sec);
    int       totalXp() const { return total_xp_; }
    int       level() const { return level_; }

    // Phase 9 persistence. main loads at startup, applies once; saves at shutdown using
    // a fresh snapshot. Settings changes mid-session (volume, input config) are reflected
    // directly on the live objects so snapshotForSave reads current state.
    void     applyLoadedSave(const SaveData& s);
    SaveData snapshotForSave() const;

    // Phase 9 lifecycle: auto-pause while the window is unfocused so we don't burn CPU
    // (and don't try to play SFX into a backgrounded audio context).
    void  setAutoPaused(bool v) { auto_paused_ = v; }
    bool  isAutoPaused() const { return auto_paused_; }

    InputConfig&       inputConfig() { return input_config_; }
    const InputConfig& inputConfig() const { return input_config_; }

    EntityId watchedCell() const { return watched_cell_; }
    void     setWatchedCell(EntityId id) { watched_cell_ = id; }
    PlayerId watchedPlayer() const { return watched_player_; }
    // Set the player slot the client cares about. Used by the multiplayer path when
    // the host's welcome packet tells the client which slot they own. Don't call
    // mid-match for singleplayer (it'd reset combo / kill-feed coloring tied to the
    // local slot).
    void     setWatchedPlayer(PlayerId p) { watched_player_ = p; }

    CameraController&       camera() { return camera_; }
    const CameraController& camera() const { return camera_; }

    DevConsole&  console() { return console_; }
    Hud&         hud() { return hud_; }
    AudioSystem& audio() { return audio_; }

private:
    // Bumps the matching mission's progress to at least new_progress, marks it complete
    // and awards XP if the target is reached. Caller passes the value the mission cares
    // about (food eaten count, peak mass, crit count, seconds alive).
    void bumpMissionProgress(MissionKind kind, int new_progress);

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
    AudioSystem          audio_;
    ChromaShift          chroma_;
    DeathCam             death_cam_;

    // Off-screen target used only when chroma_ is active, lazily allocated on first
    // render (raylib needs an open window for LoadRenderTexture).
    mutable RenderTexture2D world_rt_{};
    mutable int             world_rt_w_ = 0;
    mutable int             world_rt_h_ = 0;

    // Phase 8 -- match state
    GamePhase            phase_              = GamePhase::Playing;
    MatchSummary         summary_{};
    float                summary_remaining_  = 0.0f;
    bool                 respawn_pending_    = false;
    bool                 return_to_menu_pending_ = false;
    double               run_start_sec_      = 0.0;
    int                  total_xp_           = 0;
    int                  level_              = 1;

    // Phase 9 -- persistent lifetime stats
    int                  games_played_       = 0;
    float                best_mass_ever_     = 0.0f;
    int                  best_combo_ever_    = 0;

    // Phase 8 -- daily missions (copied from SaveData; progress reset per match)
    Mission              missions_[kMissionCount]{};
    uint32_t             last_mission_reset_day_ = 0;

    // Phase 9 -- settings that pass through Client unchanged. Client doesn't act on
    // these directly but rounds them through applyLoadedSave -> snapshotForSave so
    // they survive the match.
    uint16_t             fps_cap_                = 60;
    bool                 first_run_complete_     = true; // assume true once a match starts
    int                  match_food_eaten_       = 0;
    int                  match_crits_landed_     = 0;
    float                match_peak_mass_        = 0.0f;
    int                  match_mission_xp_       = 0; // XP earned via mission completion
                                                      // this run; folded into xp_earned at
                                                      // Summary entry so the player sees it

    float                dt_mult_     = 1.0f;
    bool                 paused_      = false;
    bool                 auto_paused_ = false; // window unfocused

    // Crashing-comet world event: pure presentation state. Set to kCometBannerSec when
    // a Telegraph event fires; counts down each frame (regardless of pause state) so
    // the HUD overlay can fade out. Doesn't affect the sim.
    float                comet_banner_remaining_ = 0.0f;
    static constexpr float kCometBannerSec      = 3.5f;
};

} // namespace cr
