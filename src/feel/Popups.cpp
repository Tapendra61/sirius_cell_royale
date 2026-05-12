#include "Popups.h"

#include <algorithm>
#include <utility>

namespace cr {

void PopupSystem::spawn(Vec2 pos, std::string text, Color color, float size) {
    Popup p;
    p.pos      = pos;
    p.vel      = {0.0f, -70.0f}; // drift up
    p.text     = std::move(text);
    p.color    = color;
    p.lifetime = 0.8f;
    p.size     = size;
    popups_.push_back(std::move(p));
}

void PopupSystem::update(float frame_dt) {
    for (auto& p : popups_) {
        p.age += frame_dt;
        p.pos.x += p.vel.x * frame_dt;
        p.pos.y += p.vel.y * frame_dt;
        p.vel.x *= 0.96f;
        p.vel.y *= 0.96f;
    }
    popups_.erase(std::remove_if(popups_.begin(), popups_.end(),
                                 [](const Popup& p) { return p.age >= p.lifetime; }),
                  popups_.end());
}

void PopupSystem::draw() const {
    for (const auto& p : popups_) {
        float t     = p.age / p.lifetime;
        float fade  = 1.0f - t;
        Color c     = p.color;
        c.a         = static_cast<unsigned char>(fade * 255.0f);
        int   font  = static_cast<int>(p.size);
        int   tw    = MeasureText(p.text.c_str(), font);
        DrawText(p.text.c_str(),
                 static_cast<int>(p.pos.x) - tw / 2,
                 static_cast<int>(p.pos.y) - font / 2,
                 font, c);
    }
}

} // namespace cr
