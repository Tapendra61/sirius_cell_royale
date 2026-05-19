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
    float launch_velocity = 500.0f; // dropped from 700 -- the 700 split read as
                                    // "child teleports" (separation in ~3 frames
                                    // at 60 fps). 500 stretches the emerge beat
                                    // to ~5 frames so the player sees the child
                                    // visibly glide out of the parent.
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
    float blast_cooldown_sec    = 6.0f;

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
    float comet_radius             = 1260.0f; // kill radius / visual half-size.
                                               // ~31% of world width as diameter --
                                               // big enough you can't sidestep the
                                               // telegraph and ignore it, small
                                               // enough the comet still feels like
                                               // a directional threat rather than
                                               // a screen-clearing wipe. Iterated
                                               // 440 -> 700 -> 2100 -> 1575 -> 1260
                                               // through playtesting (-20% off the
                                               // 1575 pass; 1575 felt slightly
                                               // overtuned vs. the rest of the
                                               // hazard suite).
    float comet_speed              = 900.0f; // world units per second
    float comet_first_after_sec    = 45.0f;  // delay before the first comet of a match

    // [food_rush] -- rare world event: for ~10 s every food pellet eaten grants
    // the food_rush_mass_multiplier of its normal mass. Announced like the
    // comet banner with a golden chime + a pulsing gold glow on every food
    // pellet for the duration. Long mean interval (240 s default = "twice per
    // five-minute match if the dice are friendly") so the rush stays a
    // genuine event rather than ambient noise.
    float food_rush_event_interval_sec = 240.0f; // mean time between rushes
    float food_rush_first_after_sec    = 90.0f;  // delay before the first rush
    float food_rush_duration_sec       = 30.0f;  // how long the rush lasts
                                                 // (bumped 10 -> 30: 10 s
                                                 // ended before the player
                                                 // could properly capitalise
                                                 // on the multiplier; 30 s
                                                 // is long enough to plan
                                                 // a feeding run but still
                                                 // ends before the natural
                                                 // next event window)
    float food_rush_mass_multiplier    = 3.0f;   // every food eaten grants 3x mass

    // [comet_shower] -- separate world event: 1 main comet + 3..7 smaller satellites
    // in formation, in red / blue color variants. Less frequent than singles so the
    // shower stays a "holy crap" moment rather than ambient noise. Fires on its
    // own cadence; can overlap with regular single comets.
    float comet_shower_event_interval_sec = 180.0f; // mean time between showers
    float comet_shower_first_after_sec    = 90.0f;  // delay before the first shower
    float comet_shower_main_radius        = 367.0f;  // main comet's kill radius.
                                                     // Smaller than single-comet 1260
                                                     // so the formation reads as a
                                                     // swarm, not one giant + dust.
                                                     // Iterated 1100 -> 825 -> 550 ->
                                                     // 367 through playtesting -- the
                                                     // shower overwhelms via numbers,
                                                     // not individual size, so the
                                                     // main keeps shrinking until it
                                                     // reads as the "biggest of the
                                                     // bunch" rather than "a real
                                                     // comet with friends".
    int   comet_shower_satellite_min      = 3;       // minimum satellite count
    int   comet_shower_satellite_max      = 7;       // maximum satellite count
                                                     // (4..8 comets total counting
                                                     // the main one; was 5..10 before
                                                     // playtesting found 10 was too
                                                     // much screen clutter)
    float comet_shower_satellite_min_radius = 117.0f; // smallest satellite radius
                                                     // (350 -> 263 -> 175 -> 117
                                                     // across playtest passes;
                                                     // satellites should feel like
                                                     // dangerous sparks, not
                                                     // mini-comets)
    float comet_shower_satellite_max_radius = 233.0f; // biggest satellite radius
                                                     // (700 -> 525 -> 350 -> 233)
    float comet_shower_spread_perp        = 1800.0f; // perpendicular scatter of
                                                     // satellites around the main
                                                     // path (each side)
    float comet_shower_spread_along       = 1500.0f; // longitudinal scatter (along
                                                     // the velocity axis). Causes
                                                     // satellites to land earlier
                                                     // / later than the main so it
                                                     // feels like a sustained barrage
                                                     // rather than a single wall.
    // Minimum centre-to-centre distance enforced between any two comets in the
    // shower (including main vs each satellite). The spawner rejection-samples
    // each satellite position up to kMaxRetries times; if no slot satisfies the
    // separation we accept the last roll (so cluster count is preserved). With
    // default 600 + max satellite radius 350 + main radius 550 we get partial
    // overlap at worst -- comets never fully coincide on top of each other.
    float comet_shower_min_separation     = 600.0f;

    // [currents] -- horizontal tidal-current bands that stretch across the
    // entire map width. Cells whose y falls inside a band get pushed along
    // the band's direction (±x). With `count=2` (default) we get one band
    // running left->right above the equator and one band running right->left
    // below it, like ocean currents. Force scales inversely with sqrt(mass).
    // Pure ambient terrain -- no damage, no kill, no slowdown for big cells.
    int   tidal_band_count    = 2;       // number of horizontal bands (0 disables)
    float tidal_band_height   = 650.0f;  // half-height: total vertical reach is 2*this.
                                          // Slimmer than the original "vast ocean"
                                          // setting so the bands read as actual
                                          // rivers cutting across the world.
    float tidal_band_strength = 360.0f;  // px/sec drift on a start_mass cell at the
                                          // centreline. base_speed is ~280, so a
                                          // starter cell is pushed slightly faster
                                          // than it can move on its own -- swept
                                          // along but the player can still fight it
                                          // meaningfully. Scales inversely with
                                          // sqrt(mass) so a 1600-mass cell feels
                                          // ~90 px/s and a 10k mega-cell barely 36.
                                          // Lets small cells use bands as a fast
                                          // highway without being overwhelming.

    // [wormholes] -- teleport pairs. Cells entering an endpoint warp to the
    // partner endpoint, with momentum preserved. A per-cell cooldown prevents
    // loop-back through the same wormhole. Both endpoints share a `pair_id`.
    int   wormhole_pair_count   = 2;      // number of PAIRS spawned at world init (so 2 = 4 endpoints)
    float wormhole_radius       = 70.0f;  // capture radius (small + sharp; visuals
                                          // are bright so anything bigger feels overwhelming)
    float wormhole_cooldown_sec = 3.0f;   // per-cell teleport-cooldown after a warp
    // Minimum world-units distance between the two endpoints of a pair so a
    // wormhole isn't trivial (entering and exiting in the same spot). At
    // 6000 in a 16k world the partner is usually somewhere on the other
    // side of the map -- a teleport actually warps you somewhere new.
    float wormhole_pair_min_distance = 6000.0f;
    // Minimum centre-to-centre distance between wormhole endpoints (across
    // ALL pairs) so they don't cluster.
    float wormhole_min_separation = 3500.0f;

    // [geysers] -- periodic food eruption points. Cycles Idle (quiet) ->
    // Telegraph (warning ring grows) -> Erupt (one tick of food spawn). Each
    // eruption flings N food pellets outward in a radial pattern with
    // outward velocity so the burst spreads naturally.
    int   geyser_count            = 5;      // number of geysers spawned at world init
    float geyser_radius           = 180.0f; // base visual radius
    float geyser_interval_sec     = 28.0f;  // mean time between eruptions (per geyser)
    float geyser_interval_jitter  = 0.30f;  // +/- fraction of the interval (RNG)
    float geyser_telegraph_sec    = 3.0f;   // warning window
    float geyser_first_after_sec  = 15.0f;  // first eruption window starts this far in
    int   geyser_food_count_min   = 14;     // food pellets per eruption (min)
    int   geyser_food_count_max   = 22;     // food pellets per eruption (max)
    float geyser_food_eject_speed = 520.0f; // initial outward velocity of erupted food
    float geyser_food_spread      = 90.0f;  // radial spread (px) of spawn positions
    float geyser_min_separation   = 3000.0f;

    // [bots]
    // VS AI / SinglePlayer default. Royale modes (LocalHost / LocalClient) override
    // this to 0 at match start so a multiplayer lobby isn't pre-populated with bots
    // -- hosts opt in via the `bots N` console command. Headless tests use the
    // Tuning struct's default (the value below) since they don't load tuning.ini.
    int   bot_target_count = 50;

    // [match]
    // Total length of a match in seconds. 0 = unlimited (open-ended sandbox, the
    // pre-multiplayer default). Royale modes override this to a non-zero value at
    // match start so peers see a clear "GG" moment instead of an endless match.
    // Headless tests use 0 so determinism runs don't accidentally hit an end.
    int   match_duration_sec = 0;
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
