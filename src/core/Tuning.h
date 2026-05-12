#pragma once
#include <string>

namespace cr {

struct Tuning {
    // [cell]
    float base_speed = 280.0f;
    float speed_falloff = 0.45f;
    float start_mass = 100.0f;
    int   max_cells_per_player = 16;

    // [absorb]
    float overlap_required = 0.4f;
    float mass_ratio_required = 1.25f;
    float combo_window_sec = 2.5f;

    // [split]
    float min_mass_to_split = 200.0f;
    float launch_velocity = 700.0f;
    float launch_decay = 4.0f;
    float recombine_delay_sec = 12.0f;

    // [eject]
    float min_mass_to_eject = 150.0f;
    float eject_mass = 18.0f;
    float eject_velocity = 600.0f;

    // [dash]
    float dash_duration_sec = 0.4f;
    float invuln_frames_sec = 0.15f;
    float speed_multiplier = 3.0f;
    float cost_percent = 0.08f;
    float cooldown_sec = 4.0f;

    // [crit]
    float chance_per_absorb = 0.05f;
    float bonus_multiplier = 1.5f;
    float near_miss_threshold = 0.92f;

    // [world]
    int world_width = 8000;
    int world_height = 8000;
    int food_target = 1200;
    int virus_count = 30;

    // [bots]
    int   bot_target_count = 22;
    float bot_spawn_interval_sec = 1.5f;
    float difficulty_scaling = 0.85f;

    // [feel]
    float screen_shake_max = 16.0f;
    float hitstop_threshold_mass = 250.0f;
    float hitstop_duration_sec = 0.06f;
    int   particle_pool_size_desktop = 8192;
    int   particle_pool_size_mobile = 2048;
};

bool LoadTuningFromFile(Tuning& out, const std::string& path);
void PrintTuning(const Tuning& t);

} // namespace cr
