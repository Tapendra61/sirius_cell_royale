#pragma once

#include "meta/SaveFile.h"
#include "raylib.h"

#include <vector>

namespace cr {

// What the menu wants the outer loop to do after this frame. Returned by render().
enum class MenuAction {
    None,
    StartVsAI,
    ShowRoyaleMenu,          // navigate to the Royale sub-menu (Local / Global picker)
    ShowSettings,
    ReplayIntro,             // replay the first-run intro for users who want a refresher
    Quit,
};

// Title-screen scene. Renders a layered "microscopic" backdrop (gradient + slow
// drifting blobs + a particle dust field + a soft halo behind the title) plus
// the title, buttons, and lifetime stats panel. The old version used 26 large
// floating circles for the bg; we replaced that with a tiered particle system
// because the circles read as "screensaver" instead of "alive". The title no
// longer pulses its font size each frame (which jittered between integer
// pixel sizes) -- instead a soft glow halo behind it breathes via alpha.
class MainMenu {
public:
    MainMenu();

    void       update(float frame_dt, int screen_w, int screen_h);
    MenuAction render(int screen_w, int screen_h, const SaveData& save);

private:
    // Unified background sprite. We tier on radius:
    //   r in [1, 3]     -- "dust" (lots of these; brightest layer)
    //   r in [4, 10]    -- "motes" (mid layer; soft glow rim)
    //   r in [150, 320] -- "blobs" (a few; almost invisible large fills that
    //                      give the screen a sense of depth + organic feel)
    struct Particle {
        float x, y;
        float vx, vy;
        float r;
        Color tint;       // tinting color (alpha varies per-tier)
        float phase;      // for per-particle alpha shimmer
    };

    void ensureBgInit(int screen_w, int screen_h);

    std::vector<Particle> particles_;
    bool   bg_inited_           = false;
    int    bg_init_w_           = 0;
    int    bg_init_h_           = 0;
    float  anim_time_           = 0.0f;
};

} // namespace cr
