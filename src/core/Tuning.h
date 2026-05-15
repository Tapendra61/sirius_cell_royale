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

    // [blast] -- 4th ability. The largest player cell spends a fraction of its mass
    // to emit a radial shockwave that pushes enemy cells (and food, weaker) outward.
    float blast_min_mass        = 300.0f;  // must be at least this big to cast
    float blast_cost_percent    = 0.20f;   // fraction of source cell's mass consumed
    float blast_radius          = 600.0f;  // push reach
    float blast_push_speed      = 1200.0f; // peak outward velocity at the blast centre
    float blast_food_push_scale = 0.5f;    // food gets a weaker push than cells
    float blast_cooldown_sec    = 4.0f;

    // [crit]
    float chance_per_absorb = 0.05f;
    float bonus_multiplier = 1.5f;
    float near_miss_threshold = 0.92f;

    // [world]
    int world_width = 8000;
    int world_height = 8000;
    int food_target = 1200;
    int virus_count = 30;

    // [pickups] -- power-up drops. Target count is the steady-state population the
    // world tries to maintain (similar to food_target). Each pickup respawns at a
    // random position after collection.
    int   pickup_target = 8;
    float pickup_shield_duration_sec  = 4.0f;
    float pickup_magnet_duration_sec  = 5.0f;
    float pickup_stealth_duration_sec = 4.0f;
    float pickup_magnet_radius        = 800.0f; // attraction reach
    float pickup_magnet_speed         = 500.0f; // world units/sec velocity applied to food

    // [blackholes] -- map-scattered safe-haven / hazards. Spawned ONCE at world
    // construction; no respawn. Cells within pull_radius of one are accelerated
    // toward its centre, and snap to "hiding" upon crossing the inner radius.
    int   blackhole_count             = 5;
    float blackhole_radius            = 70.0f;   // visual + entry threshold
    float blackhole_pull_radius       = 220.0f;  // outer reach of the pull
    float blackhole_pull_speed        = 600.0f;  // peak pull acceleration near the inner edge
    float blackhole_pull_min_speed    = 150.0f;  // pull at the outer-ring edge (gentle suck)
    float blackhole_min_separation    = 4000.0f; // minimum centre-to-centre distance
    float blackhole_stamina_drain_sec = 9.0f;    // 1.0 -> 0 inside the hole
    float blackhole_stamina_refill_sec = 15.0f;  // 0 -> 1 while outside
    float blackhole_transition_sec     = 0.35f;  // entry / exit animation duration

    // [comet] -- periodic "world event" flying across the map. Telegraphed for a few
    // seconds with a glowing path line so the player can dodge, then sweeps across in
    // a straight line, killing every cell it touches. Despawns when it exits the world.
    float comet_event_interval_sec = 75.0f;  // mean time between comet spawns
    float comet_interval_jitter    = 0.30f;  // +/- this fraction of the interval (RNG)
    float comet_telegraph_sec      = 3.0f;   // warning window before the strike
    float comet_radius             = 440.0f; // kill radius / visual half-size
    float comet_speed              = 900.0f; // world units per second
    float comet_first_after_sec    = 45.0f;  // delay before the first comet of a match

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
