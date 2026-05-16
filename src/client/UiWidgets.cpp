#include "UiWidgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cr {

namespace {

// Global HUD text scale. Hud.cpp reads this through currentHudTextScale().
float g_hud_text_scale = 1.0f;

// "Swallow the next click" gate. Set by phase transitions (see swallowNextClick());
// drawButton-family widgets read it and suppress clicks until the mouse has had a
// chance to come fully up with no pending release event.
bool g_swallow_click = false;

// Forwarded by drawButton / drawSlider / drawButtonWithSub. Returns whether the
// click should be honored. Clears the gate once there's no in-flight release.
bool consumeClick(bool raw_click) {
    if (!g_swallow_click) return raw_click;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)
        && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        g_swallow_click = false;
    }
    return false; // swallowed
}

bool pointInRect(Vector2 p, Rectangle r) {
    return p.x >= r.x && p.x <= r.x + r.width
        && p.y >= r.y && p.y <= r.y + r.height;
}

Color lighten(Color c, int amount) {
    return Color{
        static_cast<unsigned char>(std::min(255, c.r + amount)),
        static_cast<unsigned char>(std::min(255, c.g + amount)),
        static_cast<unsigned char>(std::min(255, c.b + amount)),
        c.a
    };
}

Color darken(Color c, float factor) {
    return Color{
        static_cast<unsigned char>(c.r * factor),
        static_cast<unsigned char>(c.g * factor),
        static_cast<unsigned char>(c.b * factor),
        c.a
    };
}

Color desaturate(Color c) {
    unsigned char g = static_cast<unsigned char>((c.r + c.g + c.b) / 3);
    return Color{g, g, g, static_cast<unsigned char>(c.a * 0.6f)};
}

void drawButtonBase(Rectangle r, Color fill, bool hover, bool down, bool enabled) {
    Color effective = fill;
    if (!enabled) {
        effective = desaturate(fill);
    } else if (down) {
        effective = darken(fill, 0.78f);
    } else if (hover) {
        effective = lighten(fill, 24);
    }

    // Hover gives the rect a 4px outward grow so it reads like the button is rising
    // toward the cursor. Press snaps back to base so it reads like a depress.
    Rectangle draw_r = r;
    if (enabled && hover && !down) {
        draw_r.x      -= 2.0f;
        draw_r.y      -= 2.0f;
        draw_r.width  += 4.0f;
        draw_r.height += 4.0f;
    }

    DrawRectangleRounded(draw_r, 0.28f, 8, effective);

    // Edge highlight (lighter than fill) gives the button definition without a hard
    // outline. Dim when disabled.
    Color edge = enabled
        ? Color{255, 255, 255, hover ? (unsigned char)110 : (unsigned char)70}
        : Color{255, 255, 255, 30};
    DrawRectangleRoundedLines(draw_r, 0.28f, 8, edge);
}

} // namespace

bool drawButton(Rectangle r, const char* label, int fs,
                Color fill, Color text_color, bool enabled) {
    Vector2 mp    = GetMousePosition();
    bool    hover = enabled && pointInRect(mp, r);
    bool    down  = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    bool    click = consumeClick(hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT));

    drawButtonBase(r, fill, hover, down, enabled);

    int tw = MeasureText(label, fs);
    Color tc = enabled
        ? text_color
        : Color{text_color.r, text_color.g, text_color.b, 120};
    int tx = static_cast<int>(r.x + (r.width - tw) * 0.5f);
    int ty = static_cast<int>(r.y + (r.height - fs) * 0.5f);
    // Subtle drop shadow for legibility on busy backgrounds.
    DrawText(label, tx + 1, ty + 2, fs, Color{0, 0, 0, 140});
    DrawText(label, tx, ty, fs, tc);

    return click;
}

bool drawSlider(Rectangle r, const char* label, const char* value_text,
                float* value, float lo, float hi) {
    if (!value || hi <= lo) return false;

    // Label above the track, value text at the right end.
    DrawText(label, static_cast<int>(r.x), static_cast<int>(r.y - 22), 16,
             Color{220, 225, 240, 220});
    int vw = MeasureText(value_text, 14);
    DrawText(value_text,
             static_cast<int>(r.x + r.width - vw),
             static_cast<int>(r.y - 20), 14,
             Color{180, 195, 220, 200});

    // Track
    DrawRectangleRounded(r, 0.5f, 4, Color{30, 38, 58, 255});
    DrawRectangleRoundedLines(r, 0.5f, 4, Color{255, 255, 255, 35});

    // Fill (current value)
    float clamped = std::clamp(*value, lo, hi);
    float frac    = (clamped - lo) / (hi - lo);
    Rectangle fill_r = r;
    fill_r.width = r.width * frac;
    if (fill_r.width > 4.0f) {
        DrawRectangleRounded(fill_r, 0.5f, 4, Color{120, 200, 140, 220});
    }

    // Handle
    float handle_cx = r.x + r.width * frac;
    float handle_cy = r.y + r.height * 0.5f;
    float handle_r  = r.height * 0.95f;
    DrawCircleV(Vector2{handle_cx, handle_cy}, handle_r, Color{220, 240, 230, 255});
    DrawCircleLinesV(Vector2{handle_cx, handle_cy}, handle_r, Color{40, 60, 50, 220});

    // Drag handling. Track-press anywhere on the track jumps the handle.
    Vector2 mp = GetMousePosition();
    bool over_track = pointInRect(mp, Rectangle{r.x - 8, r.y - 4,
                                                r.width + 16, r.height + 8});
    if (over_track && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float new_frac = std::clamp((mp.x - r.x) / r.width, 0.0f, 1.0f);
        float new_val  = lo + (hi - lo) * new_frac;
        if (std::fabs(new_val - *value) > 1e-4f) {
            *value = new_val;
            return true;
        }
    }
    return false;
}

bool drawChoice(Rectangle r, const char* label,
                const char* const* options, int option_count, int* index) {
    if (!options || !index || option_count <= 0) return false;

    DrawText(label, static_cast<int>(r.x), static_cast<int>(r.y - 22), 16,
             Color{220, 225, 240, 220});

    // Layout: [<]  current option (centered)  [>]
    const float arrow_w  = r.height;
    Rectangle   left_r   = {r.x, r.y, arrow_w, r.height};
    Rectangle   right_r  = {r.x + r.width - arrow_w, r.y, arrow_w, r.height};
    Rectangle   center_r = {r.x + arrow_w + 4, r.y,
                            r.width - arrow_w * 2 - 8, r.height};

    bool changed = false;
    if (drawButton(left_r,  "<", 24,
                   Color{52, 62, 92, 255}, Color{220, 225, 245, 255})) {
        *index = (*index + option_count - 1) % option_count;
        changed = true;
    }
    if (drawButton(right_r, ">", 24,
                   Color{52, 62, 92, 255}, Color{220, 225, 245, 255})) {
        *index = (*index + 1) % option_count;
        changed = true;
    }

    DrawRectangleRounded(center_r, 0.25f, 6, Color{30, 38, 58, 255});
    DrawRectangleRoundedLines(center_r, 0.25f, 6, Color{255, 255, 255, 35});
    const char* opt = options[std::clamp(*index, 0, option_count - 1)];
    int ow = MeasureText(opt, 20);
    DrawText(opt,
             static_cast<int>(center_r.x + (center_r.width - ow) * 0.5f),
             static_cast<int>(center_r.y + (center_r.height - 20) * 0.5f),
             20, Color{255, 220, 130, 240});
    return changed;
}

bool drawPresetRow(Rectangle r, const char* label,
                   const char* const* options, int option_count, int* index) {
    if (!options || !index || option_count <= 0) return false;

    DrawText(label, static_cast<int>(r.x), static_cast<int>(r.y - 22), 16,
             Color{220, 225, 240, 220});

    bool changed = false;
    const float gap   = 6.0f;
    const float total = r.width - gap * (option_count - 1);
    const float btn_w = total / option_count;
    for (int i = 0; i < option_count; ++i) {
        Rectangle br{r.x + i * (btn_w + gap), r.y, btn_w, r.height};
        const bool selected = (i == *index);
        Color fill = selected
            ? Color{70, 150, 110, 255}
            : Color{40, 50, 76, 255};
        Color text_c = selected
            ? Color{255, 250, 220, 255}
            : Color{210, 220, 240, 220};
        if (drawButton(br, options[i], 16, fill, text_c)) {
            if (!selected) {
                *index = i;
                changed = true;
            }
        }
        if (selected) {
            // Bright accent ring around the active chip.
            DrawRectangleRoundedLines(br, 0.25f, 6, Color{255, 220, 130, 220});
        }
    }
    return changed;
}

bool drawToggle(Rectangle r, const char* label, bool* value) {
    if (!value) return false;
    DrawText(label, static_cast<int>(r.x), static_cast<int>(r.y - 22), 16,
             Color{220, 225, 240, 220});

    Color fill = *value
        ? Color{55, 145, 95, 255}
        : Color{60, 70, 100, 255};
    const char* text = *value ? "ON" : "OFF";
    if (drawButton(r, text, 20, fill, Color{255, 255, 255, 255})) {
        *value = !*value;
        return true;
    }
    return false;
}

bool drawButtonWithSub(Rectangle r, const char* label, int fs,
                       const char* sub_label, int sub_fs,
                       Color fill, Color text_color, bool enabled) {
    Vector2 mp    = GetMousePosition();
    bool    hover = enabled && pointInRect(mp, r);
    bool    down  = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    bool    click = consumeClick(hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT));

    drawButtonBase(r, fill, hover, down, enabled);

    int tw = MeasureText(label, fs);
    int sw = MeasureText(sub_label, sub_fs);
    int gap = 4;
    int block_h = fs + gap + sub_fs;
    int tx = static_cast<int>(r.x + (r.width - tw) * 0.5f);
    int ty = static_cast<int>(r.y + (r.height - block_h) * 0.5f);
    int sx = static_cast<int>(r.x + (r.width - sw) * 0.5f);
    int sy = ty + fs + gap;

    Color tc = enabled
        ? text_color
        : Color{text_color.r, text_color.g, text_color.b, 120};
    DrawText(label, tx + 1, ty + 2, fs, Color{0, 0, 0, 140});
    DrawText(label, tx, ty, fs, tc);
    DrawText(sub_label, sx, sy, sub_fs,
             Color{text_color.r, text_color.g, text_color.b, 150});

    return click;
}

void  setHudTextScale(float s) { g_hud_text_scale = std::clamp(s, 0.85f, 1.30f); }
float currentHudTextScale()    { return g_hud_text_scale; }
void  swallowNextClick()       { g_swallow_click = true; }

} // namespace cr
