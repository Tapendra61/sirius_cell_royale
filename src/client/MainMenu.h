#pragma once

#include "meta/SaveFile.h"
#include "raylib.h"

#include <vector>

namespace cr {

// What the menu wants the outer loop to do after this frame. Returned by render().
enum class MenuAction {
    None,
    StartVsAI,
    StartRoyalePlaceholder,  // shows a "coming soon" toast; outer loop does nothing
    ShowSettings,
    Quit,
};

// Title-screen scene. Owns its own animated background (drifting cells) and renders
// the title + two buttons + a footer. Reads lifetime stats from the loaded SaveData
// so the menu shows the player's level and best run.
class MainMenu {
public:
    MainMenu();

    void       update(float frame_dt, int screen_w, int screen_h);
    MenuAction render(int screen_w, int screen_h, const SaveData& save);

private:
    struct BgCell {
        float x, y, vx, vy, r;
        Color color;
    };

    void ensureBgInit(int screen_w, int screen_h);

    std::vector<BgCell> bg_cells_;
    bool   bg_inited_           = false;
    int    bg_init_w_           = 0;
    int    bg_init_h_           = 0;
    float  anim_time_           = 0.0f;
    float  coming_soon_remaining_ = 0.0f;
};

} // namespace cr
