#include "Tuning.h"

#include "mini/ini.h"

#include <cstdio>
#include <string>

namespace cr {

namespace {

bool readFloat(mINI::INIStructure& ini, const char* section, const char* key, float& out) {
    if (!ini.has(section) || !ini[section].has(key)) return false;
    try {
        out = std::stof(ini[section][key]);
        return true;
    } catch (...) {
        return false;
    }
}

bool readInt(mINI::INIStructure& ini, const char* section, const char* key, int& out) {
    if (!ini.has(section) || !ini[section].has(key)) return false;
    try {
        out = std::stoi(ini[section][key]);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

bool LoadTuningFromFile(Tuning& out, const std::string& path) {
    mINI::INIFile file(path);
    mINI::INIStructure ini;
    if (!file.read(ini)) return false;

    readFloat(ini, "cell", "base_speed",              out.base_speed);
    readFloat(ini, "cell", "speed_falloff",           out.speed_falloff);
    readFloat(ini, "cell", "start_mass",              out.start_mass);
    readInt  (ini, "cell", "max_cells_per_player",    out.max_cells_per_player);

    readFloat(ini, "absorb", "overlap_required",      out.overlap_required);
    readFloat(ini, "absorb", "mass_ratio_required",   out.mass_ratio_required);
    readFloat(ini, "absorb", "combo_window_sec",      out.combo_window_sec);

    readFloat(ini, "split", "min_mass_to_split",      out.min_mass_to_split);
    readFloat(ini, "split", "launch_velocity",        out.launch_velocity);
    readFloat(ini, "split", "launch_decay",           out.launch_decay);
    readFloat(ini, "split", "recombine_delay_sec",    out.recombine_delay_sec);

    readFloat(ini, "eject", "min_mass_to_eject",      out.min_mass_to_eject);
    readFloat(ini, "eject", "eject_mass",             out.eject_mass);
    readFloat(ini, "eject", "eject_velocity",         out.eject_velocity);

    readFloat(ini, "dash", "duration_sec",            out.dash_duration_sec);
    readFloat(ini, "dash", "invuln_frames_sec",       out.invuln_frames_sec);
    readFloat(ini, "dash", "speed_multiplier",        out.speed_multiplier);
    readFloat(ini, "dash", "cost_percent",            out.cost_percent);
    readFloat(ini, "dash", "cooldown_sec",            out.cooldown_sec);

    readFloat(ini, "blast", "min_mass",               out.blast_min_mass);
    readFloat(ini, "blast", "cost_percent",           out.blast_cost_percent);
    readFloat(ini, "blast", "radius",                 out.blast_radius);
    readFloat(ini, "blast", "push_speed",             out.blast_push_speed);
    readFloat(ini, "blast", "food_push_scale",        out.blast_food_push_scale);
    readFloat(ini, "blast", "cooldown_sec",           out.blast_cooldown_sec);

    readFloat(ini, "crit", "chance_per_absorb",       out.chance_per_absorb);
    readFloat(ini, "crit", "bonus_multiplier",        out.bonus_multiplier);
    readFloat(ini, "crit", "near_miss_threshold",     out.near_miss_threshold);

    readInt(ini, "world", "width",                    out.world_width);
    readInt(ini, "world", "height",                   out.world_height);
    readInt(ini, "world", "food_target",              out.food_target);
    readInt(ini, "world", "virus_count",              out.virus_count);

    readInt  (ini, "pickups", "target",                   out.pickup_target);
    readFloat(ini, "pickups", "shield_duration_sec",      out.pickup_shield_duration_sec);
    readFloat(ini, "pickups", "magnet_duration_sec",      out.pickup_magnet_duration_sec);
    readFloat(ini, "pickups", "stealth_duration_sec",     out.pickup_stealth_duration_sec);
    readFloat(ini, "pickups", "magnet_radius",            out.pickup_magnet_radius);
    readFloat(ini, "pickups", "magnet_speed",             out.pickup_magnet_speed);

    readInt  (ini, "blackholes", "count",                 out.blackhole_count);
    readFloat(ini, "blackholes", "radius",                out.blackhole_radius);
    readFloat(ini, "blackholes", "pull_radius",           out.blackhole_pull_radius);
    readFloat(ini, "blackholes", "pull_speed",            out.blackhole_pull_speed);
    readFloat(ini, "blackholes", "pull_min_speed",        out.blackhole_pull_min_speed);
    readFloat(ini, "blackholes", "min_separation",        out.blackhole_min_separation);
    readFloat(ini, "blackholes", "stamina_drain_sec",     out.blackhole_stamina_drain_sec);
    readFloat(ini, "blackholes", "stamina_refill_sec",    out.blackhole_stamina_refill_sec);
    readFloat(ini, "blackholes", "transition_sec",        out.blackhole_transition_sec);

    readFloat(ini, "comet", "event_interval_sec", out.comet_event_interval_sec);
    readFloat(ini, "comet", "interval_jitter",    out.comet_interval_jitter);
    readFloat(ini, "comet", "telegraph_sec",      out.comet_telegraph_sec);
    readFloat(ini, "comet", "radius",             out.comet_radius);
    readFloat(ini, "comet", "speed",              out.comet_speed);
    readFloat(ini, "comet", "first_after_sec",    out.comet_first_after_sec);

    readFloat(ini, "comet_shower", "event_interval_sec",  out.comet_shower_event_interval_sec);
    readFloat(ini, "comet_shower", "first_after_sec",     out.comet_shower_first_after_sec);
    readFloat(ini, "comet_shower", "main_radius",         out.comet_shower_main_radius);
    readInt  (ini, "comet_shower", "satellite_min",       out.comet_shower_satellite_min);
    readInt  (ini, "comet_shower", "satellite_max",       out.comet_shower_satellite_max);
    readFloat(ini, "comet_shower", "satellite_min_radius", out.comet_shower_satellite_min_radius);
    readFloat(ini, "comet_shower", "satellite_max_radius", out.comet_shower_satellite_max_radius);
    readFloat(ini, "food_rush", "event_interval_sec",  out.food_rush_event_interval_sec);
    readFloat(ini, "food_rush", "first_after_sec",     out.food_rush_first_after_sec);
    readFloat(ini, "food_rush", "duration_sec",        out.food_rush_duration_sec);
    readFloat(ini, "food_rush", "mass_multiplier",     out.food_rush_mass_multiplier);

    readFloat(ini, "comet_shower", "spread_perp",         out.comet_shower_spread_perp);
    readFloat(ini, "comet_shower", "spread_along",        out.comet_shower_spread_along);
    readFloat(ini, "comet_shower", "min_separation",      out.comet_shower_min_separation);

    readInt  (ini, "currents", "band_count",    out.tidal_band_count);
    readFloat(ini, "currents", "band_height",   out.tidal_band_height);
    readFloat(ini, "currents", "band_strength", out.tidal_band_strength);

    readInt  (ini, "wormholes", "pair_count",       out.wormhole_pair_count);
    readFloat(ini, "wormholes", "radius",           out.wormhole_radius);
    readFloat(ini, "wormholes", "cooldown_sec",     out.wormhole_cooldown_sec);
    readFloat(ini, "wormholes", "pair_min_distance", out.wormhole_pair_min_distance);
    readFloat(ini, "wormholes", "min_separation",   out.wormhole_min_separation);

    readInt  (ini, "geysers", "count",            out.geyser_count);
    readFloat(ini, "geysers", "radius",           out.geyser_radius);
    readFloat(ini, "geysers", "interval_sec",     out.geyser_interval_sec);
    readFloat(ini, "geysers", "interval_jitter",  out.geyser_interval_jitter);
    readFloat(ini, "geysers", "telegraph_sec",    out.geyser_telegraph_sec);
    readFloat(ini, "geysers", "first_after_sec",  out.geyser_first_after_sec);
    readInt  (ini, "geysers", "food_count_min",   out.geyser_food_count_min);
    readInt  (ini, "geysers", "food_count_max",   out.geyser_food_count_max);
    readFloat(ini, "geysers", "food_eject_speed", out.geyser_food_eject_speed);
    readFloat(ini, "geysers", "food_spread",      out.geyser_food_spread);
    readFloat(ini, "geysers", "min_separation",   out.geyser_min_separation);

    readInt  (ini, "bots", "target_count",            out.bot_target_count);
    readFloat(ini, "bots", "spawn_interval_sec",      out.bot_spawn_interval_sec);
    readFloat(ini, "bots", "difficulty_scaling",      out.difficulty_scaling);

    readInt  (ini, "match", "duration_sec",           out.match_duration_sec);

    readFloat(ini, "feel", "screen_shake_max",        out.screen_shake_max);
    readFloat(ini, "feel", "hitstop_threshold_mass",  out.hitstop_threshold_mass);
    readFloat(ini, "feel", "hitstop_duration_sec",    out.hitstop_duration_sec);
    readInt  (ini, "feel", "particle_pool_size_desktop", out.particle_pool_size_desktop);
    readInt  (ini, "feel", "particle_pool_size_mobile",  out.particle_pool_size_mobile);

    return true;
}

void PrintTuning(const Tuning& t) {
    std::printf("Tuning:\n");
    std::printf("  [cell]    base_speed=%.2f  speed_falloff=%.2f  start_mass=%.1f  max_cells=%d\n",
                t.base_speed, t.speed_falloff, t.start_mass, t.max_cells_per_player);
    std::printf("  [absorb]  overlap=%.2f  mass_ratio=%.2f  combo_window=%.2fs\n",
                t.overlap_required, t.mass_ratio_required, t.combo_window_sec);
    std::printf("  [split]   min_mass=%.1f  launch_vel=%.1f  decay=%.1f  recombine=%.1fs\n",
                t.min_mass_to_split, t.launch_velocity, t.launch_decay, t.recombine_delay_sec);
    std::printf("  [eject]   min_mass=%.1f  eject_mass=%.1f  vel=%.1f\n",
                t.min_mass_to_eject, t.eject_mass, t.eject_velocity);
    std::printf("  [dash]    duration=%.2fs  invuln=%.2fs  speed_mult=%.1f  cost=%.2f  cooldown=%.1fs\n",
                t.dash_duration_sec, t.invuln_frames_sec, t.speed_multiplier,
                t.cost_percent, t.cooldown_sec);
    std::printf("  [crit]    chance=%.2f  bonus=%.2f  near_miss=%.2f\n",
                t.chance_per_absorb, t.bonus_multiplier, t.near_miss_threshold);
    std::printf("  [world]   %dx%d  food_target=%d  viruses=%d  pickups=%d  blackholes=%d\n",
                t.world_width, t.world_height, t.food_target, t.virus_count,
                t.pickup_target, t.blackhole_count);
    std::printf("  [bots]    target=%d  spawn_interval=%.2fs  difficulty=%.2f\n",
                t.bot_target_count, t.bot_spawn_interval_sec, t.difficulty_scaling);
    std::printf("  [feel]    shake_max=%.1f  hitstop>%.0f@%.2fs  particles=%d/%d\n",
                t.screen_shake_max, t.hitstop_threshold_mass, t.hitstop_duration_sec,
                t.particle_pool_size_desktop, t.particle_pool_size_mobile);
    std::printf("  [comet]   every %.0fs (+/-%.0f%%, first @ %.0fs)  warn=%.1fs  r=%.0f  speed=%.0f\n",
                t.comet_event_interval_sec, t.comet_interval_jitter * 100.0f,
                t.comet_first_after_sec, t.comet_telegraph_sec,
                t.comet_radius, t.comet_speed);
    std::printf("  [currents] bands=%d  height=%.0f  strength=%.0f\n",
                t.tidal_band_count, t.tidal_band_height, t.tidal_band_strength);
    std::printf("  [wormholes] pairs=%d  r=%.0f  cooldown=%.1fs  pair_min_dist=%.0f\n",
                t.wormhole_pair_count, t.wormhole_radius,
                t.wormhole_cooldown_sec, t.wormhole_pair_min_distance);
    std::printf("  [geysers] count=%d  every %.0fs (+/-%.0f%%, first @ %.0fs)  warn=%.1fs  food=%d-%d\n",
                t.geyser_count, t.geyser_interval_sec, t.geyser_interval_jitter * 100.0f,
                t.geyser_first_after_sec, t.geyser_telegraph_sec,
                t.geyser_food_count_min, t.geyser_food_count_max);
}

} // namespace cr
