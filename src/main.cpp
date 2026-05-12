#include "raylib.h"

#include "core/Tuning.h"

#include <cstdio>

int main() {
    cr::Tuning tuning;
    const char* tuning_path = "tuning.ini";
    if (cr::LoadTuningFromFile(tuning, tuning_path)) {
        std::printf("[cell_royale] Loaded %s\n", tuning_path);
    } else {
        std::printf("[cell_royale] Failed to load %s -- using defaults\n", tuning_path);
    }
    cr::PrintTuning(tuning);

    InitWindow(1280, 720, "Cell Royale v0.0.1");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(Color{20, 24, 32, 255});
        DrawText("Cell Royale v0.0.1", 20, 20, 24, RAYWHITE);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
