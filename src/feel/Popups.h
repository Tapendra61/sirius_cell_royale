#pragma once

#include "core/Types.h"
#include "raylib.h"

#include <string>
#include <vector>

namespace cr {

// Floating "+247"-style world-space text. Drift upward, fade, and disappear.
class PopupSystem {
public:
    void spawn(Vec2 pos, std::string text, Color color, float size = 22.0f);
    void update(float frame_dt);
    void draw() const; // call inside BeginMode2D

private:
    struct Popup {
        Vec2        pos;
        Vec2        vel;
        std::string text;
        Color       color;
        float       age      = 0.0f;
        float       lifetime = 0.8f;
        float       size     = 22.0f;
    };
    std::vector<Popup> popups_;
};

} // namespace cr
