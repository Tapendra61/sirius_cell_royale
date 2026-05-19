#include "Simulation.h"

#include "Rules.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace cr {

Simulation::Simulation(uint64_t seed, Tuning tuning)
    : world_(seed, tuning.world_width, tuning.world_height),
      tuning_(std::move(tuning)),
      director_(seed) {
    // Schedule the first crashing-comet event. Subsequent reschedules happen inside
    // processComets after each spawn. Using a concrete tick (not a sentinel) keeps
    // the cadence deterministic across save/load.
    next_comet_spawn_tick_  = secondsToTicks(tuning_.comet_first_after_sec);
    // Comet shower runs on its own cadence (independent of single-comet
    // schedule). First shower lands later than the first single comet so the
    // player doesn't see a screen-spanning swarm 45 s in with no warm-up.
    next_shower_spawn_tick_ = secondsToTicks(tuning_.comet_shower_first_after_sec);
    // Food-rush schedule: same pattern as the other world events. First rush
    // lands well into the match (default 90 s) so the player's first growth
    // phase happens at baseline rates -- the rush is a CHANGE from the
    // norm and needs a "norm" to land against.
    next_food_rush_spawn_tick_ = secondsToTicks(tuning_.food_rush_first_after_sec);
    // Initial food: tiered roll so the world spawns with a mix of common/uncommon/rare/epic.
    for (int i = 0; i < tuning_.food_target; ++i) {
        Vec2 pos{
            world_.rng().rangeFloat(0.0f, static_cast<float>(world_.width())),
            world_.rng().rangeFloat(0.0f, static_cast<float>(world_.height())),
        };
        world_.spawnFood(pos, rules::rollFoodMass(world_.rng()),
                         Vec2{0.0f, 0.0f}, INVALID_PLAYER);
    }
    // Initial viruses, kept clear of the world edges.
    constexpr float kVirusMargin = 200.0f;
    for (int i = 0; i < tuning_.virus_count; ++i) {
        Vec2 pos{
            world_.rng().rangeFloat(kVirusMargin,
                                    static_cast<float>(world_.width())  - kVirusMargin),
            world_.rng().rangeFloat(kVirusMargin,
                                    static_cast<float>(world_.height()) - kVirusMargin),
        };
        world_.spawnVirus(pos, 200.0f);
    }
    // Initial power-up pickups so the map isn't empty for the first half second.
    constexpr float kPickupMargin = 200.0f;
    for (int i = 0; i < tuning_.pickup_target; ++i) {
        Vec2 pos{
            world_.rng().rangeFloat(kPickupMargin,
                                    static_cast<float>(world_.width())  - kPickupMargin),
            world_.rng().rangeFloat(kPickupMargin,
                                    static_cast<float>(world_.height()) - kPickupMargin),
        };
        int r = world_.rng().rangeInt(1, kPickupKindCount);
        world_.spawnPickup(pos, static_cast<PickupKind>(r));
    }
    // Black holes -- placed once with a rejection sampler to enforce min separation.
    // 5 holes in a 16k world with 4000-unit spacing fits comfortably; if a placement
    // can't satisfy the constraint after many attempts we accept the last candidate
    // anyway so we never deadlock the constructor on pathological tuning.
    const float bh_margin = std::max(tuning_.blackhole_pull_radius + 200.0f, 400.0f);
    for (int i = 0; i < tuning_.blackhole_count; ++i) {
        Vec2 pos{0.0f, 0.0f};
        for (int attempt = 0; attempt < 64; ++attempt) {
            pos = Vec2{
                world_.rng().rangeFloat(bh_margin,
                                        static_cast<float>(world_.width())  - bh_margin),
                world_.rng().rangeFloat(bh_margin,
                                        static_cast<float>(world_.height()) - bh_margin),
            };
            bool ok = true;
            for (const auto& b : world_.blackholes()) {
                if (distance(b.pos, pos) < tuning_.blackhole_min_separation) {
                    ok = false; break;
                }
            }
            if (ok) break;
        }
        world_.spawnBlackHole(pos, tuning_.blackhole_radius,
                              tuning_.blackhole_pull_radius);
    }

    // ---- Tidal current bands ----
    // Horizontal strips spanning the full world width. With the default
    // count=2 we get one band at 25% world height running left->right and
    // one at 75% running right->left -- like opposing ocean currents. The
    // count is generalised so 4 bands gives 20/40/60/80%, etc.; direction
    // alternates per band starting L->R.
    {
        const int n = std::max(0, tuning_.tidal_band_count);
        const float world_w = static_cast<float>(world_.width());
        const float world_h = static_cast<float>(world_.height());
        for (int i = 0; i < n; ++i) {
            // Evenly distribute band centres across the world height.
            // i=0 lands at 1/(n+1), i=n-1 lands at n/(n+1). For n=2 that's
            // 1/3 and 2/3 -- close to the requested "upper / lower" feel.
            // For n=1 it lands at 0.5 (equator).
            const float frac = static_cast<float>(i + 1) /
                               static_cast<float>(n + 1);
            const float band_y = world_h * frac;
            // Alternate direction starting with +x (left -> right).
            const float dx = (i % 2 == 0) ? +1.0f : -1.0f;
            const Vec2  dir{dx, 0.0f};
            const Vec2  pos{world_w * 0.5f, band_y};
            world_.spawnTidalCurrent(pos, dir,
                                     tuning_.tidal_band_height,
                                     tuning_.tidal_band_strength);
        }
    }

    // ---- Wormhole pairs ----
    // Place each pair as two endpoints separated by at least
    // wormhole_pair_min_distance so a teleport actually moves you meaningfully.
    // Also keep wormhole_min_separation between ANY two endpoints (across all
    // pairs) so the visuals don't overlap.
    {
        const float margin = std::max(tuning_.wormhole_radius + 100.0f, 400.0f);
        auto pickEndpoint = [&](Vec2& out_pos) {
            for (int attempt = 0; attempt < 64; ++attempt) {
                Vec2 cand{
                    world_.rng().rangeFloat(margin,
                                            static_cast<float>(world_.width())  - margin),
                    world_.rng().rangeFloat(margin,
                                            static_cast<float>(world_.height()) - margin),
                };
                bool ok = true;
                for (const auto& w : world_.wormholes()) {
                    if (distance(w.pos, cand) < tuning_.wormhole_min_separation) {
                        ok = false; break;
                    }
                }
                if (ok) { out_pos = cand; return; }
                out_pos = cand; // fallback if separation can't be satisfied
            }
        };
        for (int i = 0; i < tuning_.wormhole_pair_count; ++i) {
            Vec2 a, b;
            pickEndpoint(a);
            // Pick b far enough from a.
            bool placed = false;
            for (int attempt = 0; attempt < 64 && !placed; ++attempt) {
                pickEndpoint(b);
                if (distance(a, b) >= tuning_.wormhole_pair_min_distance) {
                    placed = true;
                }
            }
            // pickEndpoint always writes; even if separation failed we accept
            // the last candidate to avoid a constructor stall.
            world_.spawnWormholePair(a, b, tuning_.wormhole_radius);
        }
    }

    // ---- Geysers ----
    // Stationary food-eruption points. Each starts with a deterministic
    // first_event_tick spaced out so they don't all erupt simultaneously --
    // the first one fires at `geyser_first_after_sec`, subsequent ones are
    // offset by half the interval each.
    {
        const float margin = std::max(tuning_.geyser_radius + 200.0f, 400.0f);
        const Tick  first_tick = secondsToTicks(tuning_.geyser_first_after_sec);
        const Tick  stagger    = secondsToTicks(tuning_.geyser_interval_sec * 0.5f);
        for (int i = 0; i < tuning_.geyser_count; ++i) {
            Vec2 pos{0.0f, 0.0f};
            for (int attempt = 0; attempt < 64; ++attempt) {
                pos = Vec2{
                    world_.rng().rangeFloat(margin,
                                            static_cast<float>(world_.width())  - margin),
                    world_.rng().rangeFloat(margin,
                                            static_cast<float>(world_.height()) - margin),
                };
                bool ok = true;
                for (const auto& g : world_.geysers()) {
                    if (distance(g.pos, pos) < tuning_.geyser_min_separation) {
                        ok = false; break;
                    }
                }
                if (ok) break;
            }
            Tick first_event = first_tick + static_cast<Tick>(stagger * i);
            world_.spawnGeyser(pos, tuning_.geyser_radius, first_event);
        }
    }
}

void Simulation::queueCommand(Command cmd) {
    pending_.push_back(std::move(cmd));
}

std::vector<GameEvent> Simulation::takeEvents() {
    std::vector<GameEvent> out;
    out.swap(events_);
    return out;
}

void Simulation::tick(float dt) {
    events_.clear();

    const Tick now = world_.currentTick();

    // Match-timer anchor. We can't initialise this in the constructor because
    // the world spawns its initial cells AFTER construction (see runMatch),
    // and match_started_tick_ should mark the first frame that gameplay
    // actually advances. Set it once on the first tick.
    if (!match_started_) {
        match_started_tick_ = now;
        match_started_      = true;
    }

    // 0. Bots read last tick's state and queue this tick's commands.
    std::vector<Command> bot_commands;
    director_.tick(world_, tuning_, bot_commands);
    for (auto& c : bot_commands) {
        pending_.push_back(std::move(c));
    }

    // 1. Apply commands queued for this tick (and any earlier ticks not yet applied).
    std::vector<Command> still_pending;
    still_pending.reserve(pending_.size());
    for (auto& cmd : pending_) {
        if (cmd.tick <= now) {
            applyCommand(cmd);
        } else {
            still_pending.push_back(std::move(cmd));
        }
    }
    pending_ = std::move(still_pending);

    // 2. Per-tick physics.
    //    Magnet pulls write velocity onto nearby food before stepFood integrates it.
    //    Black-hole pulls write velocity onto cells before stepCells integrates them;
    //    the same function also handles entry, draining stamina, and exit.
    //    Comets move + kill on contact + reschedule themselves; runs after BH so a cell
    //    that just got pinned into a black hole is safe (hiding cells are immune).
    rules::applyMagnetPulls(world_, tuning_);
    rules::processBlackHoles(world_, tuning_, dt);
    // Shower spawn runs BEFORE processComets so newly-spawned shower comets
    // get included in the same tick's motion + kill loop. The two functions
    // share the world.comets() list; processComets handles motion / kill /
    // despawn for ALL comets regardless of where they were spawned.
    rules::processCometShowers(world_, tuning_, next_shower_spawn_tick_, events_);
    rules::processComets(world_, tuning_, dt, next_comet_spawn_tick_, events_);
    // Food rush runs BEFORE processEating so a rush triggered on this tick
    // applies its multiplier to any food eaten this tick (and not, e.g.,
    // missed by one frame). The function also emits Start/End events for
    // the client banner + chime.
    rules::processFoodRush(world_, tuning_, next_food_rush_spawn_tick_,
                           food_rush_active_, events_);
    // Tidal currents must run BEFORE stepCells so the velocity bias they
    // apply gets integrated into position on this same tick. Order vs.
    // BlackHoles / Comets is intentional: BH suction wins over a drift
    // current (more dramatic moment-to-moment), and a comet kill applies
    // before motion so a cell that just got hit doesn't pretend to keep moving.
    rules::applyTidalCurrents(world_, tuning_, dt);
    rules::stepCells(world_, tuning_, dt);
    rules::stepFood(world_, tuning_, dt);
    rules::applySoftBounds(world_, tuning_);
    // Wormholes after stepCells so we teleport based on each cell's final
    // post-motion position (matches the rendered position at end of tick).
    // Cells warped here will appear at their new spot on the very next
    // snapshot, so prev->curr interpolation jumps -- which is what we want
    // (a teleport should read as instantaneous, not a smooth slide).
    rules::processWormholes(world_, tuning_);
    // Geysers can spawn food via world.spawnFood(), which invalidates the
    // foods_grid. We call this AFTER stepFood but BEFORE rebuildGrids so
    // the next-tick eating step sees the newly-erupted pellets.
    rules::processGeysers(world_, tuning_);

    // 3. Rebuild spatial grids once positions have settled, so the eating step can use
    //    them. Grids store vector indices; valid until the first push_back / compactDead
    //    in this tick (i.e. through processEating's queries but stale after).
    // Resolve same-owner overlaps BEFORE rebuilding spatial grids so the
    // eating pass sees the post-push positions. Pushes are tiny (sub-pixel
    // in steady state, larger only right after a split) and don't change
    // the cell array's order or count, so grid rebuilding still works.
    rules::resolveOwnerOverlaps(world_);

    world_.rebuildGrids();

    // 4. Interactions (collision-driven).
    rules::processEating(world_, tuning_, events_);
    rules::processVirusPushes(world_, tuning_);
    rules::processRecombine(world_, tuning_, events_);
    rules::processPickupCollection(world_, tuning_, events_);

    // 5. World upkeep.
    rules::respawnFood(world_, tuning_);
    rules::respawnViruses(world_, tuning_);
    rules::respawnPickups(world_, tuning_);

    // 6. Rebuild grids one more time so the NEXT tick's bot AI (which runs before motion
    // and therefore before the intermediate rebuild) has fresh indices reflecting all
    // post-compaction + respawn changes. Cheap (~0.1ms) and removes a class of bugs
    // where bots would dereference stale indices into the food vector.
    world_.rebuildGrids();

    // 7. Match-end check. Skipped when duration is 0 (sandbox / SP) or after
    // the event has already fired this match. The winner is determined by
    // total mass summed across each owner's cells; ties go to whichever owner
    // we scan first (deterministic via the cells_ vector ordering).
    if (!match_ended_ && tuning_.match_duration_sec > 0) {
        const Tick duration_ticks = secondsToTicks(
            static_cast<float>(tuning_.match_duration_sec));
        if (now - match_started_tick_ + 1 >= duration_ticks) {
            match_ended_ = true;
            // Aggregate mass by owner. Linear scan; cells_ is bounded by
            // max_cells_per_player * (1 + bots) so this is cheap.
            PlayerId winner   = INVALID_PLAYER;
            float    best_mass = -1.0f;
            // Small array-of-pairs to keep the dependency tree clean (no
            // unordered_map include just for one place).
            constexpr size_t kMaxOwners = 256;
            struct Tally { PlayerId pid; float mass; };
            Tally tally[kMaxOwners];
            size_t n_tally = 0;
            for (const auto& c : world_.cells()) {
                if (c.mass <= 0.0f) continue;
                // Find or insert.
                bool found = false;
                for (size_t i = 0; i < n_tally; ++i) {
                    if (tally[i].pid == c.owner) {
                        tally[i].mass += c.mass;
                        found = true;
                        break;
                    }
                }
                if (!found && n_tally < kMaxOwners) {
                    tally[n_tally++] = Tally{c.owner, c.mass};
                }
            }
            for (size_t i = 0; i < n_tally; ++i) {
                if (tally[i].mass > best_mass) {
                    best_mass = tally[i].mass;
                    winner    = tally[i].pid;
                }
            }
            MatchEndEvent me;
            me.winner_player = winner;
            me.winner_mass   = (best_mass < 0.0f) ? 0.0f : best_mass;
            me.reason        = MatchEndEvent::Reason::TimeLimit;
            events_.push_back(me);
        }
    }

    world_.advanceTick();
}

void Simulation::applyCommand(const Command& cmd) {
    if (auto* m = std::get_if<MoveCmd>(&cmd.payload)) {
        // Clamp target into the playfield with a small inset so cells don't pile against
        // the wall when the player aims outside the world (high-zoom mouse, bot flee target
        // past the edge, etc.). Cheap, deterministic, applies uniformly to bot + player.
        constexpr float kMargin = 32.0f;
        Vec2 target{
            std::clamp(m->target.x, kMargin,
                       static_cast<float>(world_.width())  - kMargin),
            std::clamp(m->target.y, kMargin,
                       static_cast<float>(world_.height()) - kMargin),
        };
        for (auto& c : world_.cellsMut()) {
            if (c.owner == cmd.player) c.target = target;
        }
        return;
    }
    if (std::holds_alternative<SplitCmd>(cmd.payload)) {
        rules::doSplit(world_, cmd.player, tuning_, events_);
        return;
    }
    if (std::holds_alternative<EjectCmd>(cmd.payload)) {
        rules::doEject(world_, cmd.player, tuning_);
        return;
    }
    if (std::holds_alternative<DashCmd>(cmd.payload)) {
        rules::doDash(world_, cmd.player, tuning_);
        return;
    }
    if (std::holds_alternative<BlastCmd>(cmd.payload)) {
        rules::doBlast(world_, cmd.player, tuning_, events_);
        return;
    }
    if (std::holds_alternative<RespawnCmd>(cmd.payload)) {
        rules::doRespawn(world_, cmd.player, tuning_);
        return;
    }
}

Snapshot Simulation::buildSnapshot() const {
    Snapshot s;
    s.tick      = world_.currentTick();
    s.rng_state = world_.rng().state;

    const Tick  now             = world_.currentTick();
    const float cooldown_ticks  = std::max(1.0f, tuning_.cooldown_sec * kSimHz);

    s.cells.reserve(world_.cells().size());
    const auto& bots = director_.bots();
    for (const auto& c : world_.cells()) {
        CellSnap cs;
        cs.id      = c.id;
        cs.owner   = c.owner;
        cs.pos     = c.pos;
        cs.vel     = c.vel;
        cs.mass    = c.mass;
        cs.invuln  = (c.invuln_until > now);
        cs.dashing = (c.dash_until > now);
        cs.god     = c.god;
        // Power-up effect snapshots. Norm fields use a fixed visual reference duration
        // so the aura fade rate is consistent regardless of how long the effect runs.
        constexpr float kEffectNormTicks = 5.0f * kSimHz; // 5s reference
        auto effectNorm = [&](Tick until) {
            if (until <= now) return 0.0f;
            float remaining = static_cast<float>(until - now);
            return std::clamp(remaining / kEffectNormTicks, 0.0f, 1.0f);
        };
        cs.shield_active  = (c.shield_until  > now);
        cs.magnet_active  = (c.magnet_until  > now);
        cs.stealth_active = (c.stealth_until > now);
        cs.shield_norm    = effectNorm(c.shield_until);
        cs.magnet_norm    = effectNorm(c.magnet_until);
        cs.stealth_norm   = effectNorm(c.stealth_until);
        // "Fully hiding" only counts when there's no active entry animation (the
        // entry anim is when the cell is mid-shrink and still visually present).
        cs.hiding = (c.hiding_in != INVALID_ENTITY && c.entry_anim_until <= now);
        cs.hiding_in_id           = c.hiding_in;
        cs.blackhole_stamina_norm = std::clamp(c.blackhole_stamina, 0.0f, 1.0f);

        // Visual scale: drives cell-radius shrink/grow during the transition. Maps
        // entry-anim progress 0..1 to scale 1..0, fully-hidden to 0, exit-anim
        // progress 0..1 to scale 0..1, and the open world to 1.
        const float anim_ticks = std::max(1.0f, tuning_.blackhole_transition_sec * kSimHz);
        if (c.entry_anim_until > now) {
            float remaining = static_cast<float>(c.entry_anim_until - now);
            float progress  = std::clamp(1.0f - remaining / anim_ticks, 0.0f, 1.0f);
            cs.blackhole_visual_scale = 1.0f - (progress * progress);
        } else if (c.hiding_in != INVALID_ENTITY) {
            cs.blackhole_visual_scale = 0.0f;
        } else if (c.exit_anim_until > now) {
            float remaining = static_cast<float>(c.exit_anim_until - now);
            float progress  = std::clamp(1.0f - remaining / anim_ticks, 0.0f, 1.0f);
            // Ease-out: 1 - (1 - p)^2 -- matches the position ease in Rules.cpp.
            float ease = 1.0f - (1.0f - progress) * (1.0f - progress);
            cs.blackhole_visual_scale = ease;
        } else {
            cs.blackhole_visual_scale = 1.0f;
        }
        if (c.dash_cooldown_until > now) {
            float remaining = static_cast<float>(c.dash_cooldown_until - now);
            cs.dash_cooldown_norm = std::clamp(1.0f - remaining / cooldown_ticks, 0.0f, 1.0f);
        } else {
            cs.dash_cooldown_norm = 1.0f;
        }
        // Blast cooldown is normalised the same way but against the blast tuning's
        // own cooldown_sec -- so the HUD bar fills consistently regardless of
        // whether dash and blast share a cooldown duration.
        const float blast_cd_ticks =
            std::max(1.0f, tuning_.blast_cooldown_sec * kSimHz);
        if (c.blast_cooldown_until > now) {
            float remaining = static_cast<float>(c.blast_cooldown_until - now);
            cs.blast_cooldown_norm =
                std::clamp(1.0f - remaining / blast_cd_ticks, 0.0f, 1.0f);
        } else {
            cs.blast_cooldown_norm = 1.0f;
        }
        // personality_tag + is_elite are stored on the Cell at bot spawn; the only
        // bot-side value that still needs a scan is the Hunter dash-windup telegraph
        // (it changes every tick so writing it back to Cell would cost the same as
        // reading it here). Cache that scan to skip the inner loop for player cells,
        // which is most of them in the early game.
        cs.personality_tag     = c.personality_tag;
        cs.is_elite            = c.is_elite;
        cs.dash_telegraph_norm = 0.0f;
        if (c.personality_tag != 0) {
            for (const auto& bot : bots) {
                if (bot.player == c.owner) {
                    if (bot.dash_windup_until > now
                        && bot.dash_windup_until > bot.dash_windup_started) {
                        float dur = static_cast<float>(bot.dash_windup_until - bot.dash_windup_started);
                        float el  = static_cast<float>(now - bot.dash_windup_started);
                        cs.dash_telegraph_norm = std::clamp(el / dur, 0.0f, 1.0f);
                    }
                    break;
                }
            }
        }
        s.cells.push_back(cs);
    }

    s.food.reserve(world_.food().size());
    for (const auto& f : world_.food()) {
        s.food.push_back(FoodSnap{f.id, f.pos, f.vel, f.mass});
    }

    s.viruses.reserve(world_.viruses().size());
    for (const auto& v : world_.viruses()) {
        s.viruses.push_back(VirusSnap{v.id, v.pos, v.mass});
    }

    s.pickups.reserve(world_.pickups().size());
    for (const auto& p : world_.pickups()) {
        s.pickups.push_back(PickupSnap{p.id, p.pos, p.kind});
    }

    s.blackholes.reserve(world_.blackholes().size());
    for (const auto& b : world_.blackholes()) {
        // Occupancy: how many cells are currently hiding inside. Used by the renderer
        // for a subtle "occupied" tell on the vortex.
        uint8_t occ = 0;
        for (const auto& c : world_.cells()) {
            if (c.hiding_in == b.id) { ++occ; if (occ == 255) break; }
        }
        s.blackholes.push_back(BlackHoleSnap{b.id, b.pos, b.radius, b.pull_radius, occ});
    }

    // Comets: 0..1 in single-comet windows, 5..10 during a comet-shower event.
    // telegraph_norm is 0 at spawn, 1 at start_at (and stays 1).
    s.comets.reserve(world_.comets().size());
    for (const auto& c : world_.comets()) {
        CometSnap cs;
        cs.id              = c.id;
        cs.pos             = c.pos;
        cs.vel             = c.vel;
        cs.radius          = c.radius;
        cs.telegraph_start = c.telegraph_start;
        cs.telegraph_end   = c.telegraph_end;
        cs.variant         = c.variant;
        if (c.start_at > now) {
            const float total = std::max(1.0f,
                static_cast<float>(c.start_at - c.spawned_at));
            const float el    = static_cast<float>(now - c.spawned_at);
            cs.telegraph_norm = std::clamp(el / total, 0.0f, 1.0f);
        } else {
            cs.telegraph_norm = 1.0f;
        }
        s.comets.push_back(cs);
    }

    // Currents: horizontal bands. Static positions; renderer uses them to
    // seed the streaming particle visual.
    s.currents.reserve(world_.currents().size());
    for (const auto& c : world_.currents()) {
        CurrentSnap cs;
        cs.id          = c.id;
        cs.pos         = c.pos;
        cs.dir         = c.dir;
        cs.half_height = c.half_height;
        cs.strength    = c.strength;
        s.currents.push_back(cs);
    }

    // Wormholes: per-pair entries. spin_phase is computed from the current
    // tick so all clients render the same swirl angle (no client-side time
    // drift). Slow rotation -- the visual reads as gentle vortex, not strobe.
    s.wormholes.reserve(world_.wormholes().size());
    for (const auto& w : world_.wormholes()) {
        WormholeSnap ws;
        ws.id         = w.id;
        ws.pair_id    = w.pair_id;
        ws.pos        = w.pos;
        ws.radius     = w.radius;
        // 0.7 rad/sec ~= one full rotation per ~9 seconds.
        ws.spin_phase = std::fmod(static_cast<float>(now) * (0.7f / kSimHz),
                                  6.28318530718f);
        s.wormholes.push_back(ws);
    }

    // Geysers: state + phase_norm tell the renderer which visual to draw.
    s.geysers.reserve(world_.geysers().size());
    for (const auto& g : world_.geysers()) {
        GeyserSnap gs;
        gs.id     = g.id;
        gs.pos    = g.pos;
        gs.radius = g.radius;
        gs.state  = static_cast<uint8_t>(g.state);
        // phase_norm: position within the current state. For Idle / Telegraph
        // we walk from prev-transition-tick toward next_event_tick. For
        // Erupt it's always 1 (single tick).
        switch (g.state) {
            case GeyserState::Idle: {
                // The Idle phase starts at the *previous* erupt's end. Use
                // last_erupted_at + 1 (right after erupt) as the lower bound;
                // if we haven't erupted yet, use 0 -- the renderer treats
                // phase_norm in Idle as a low-priority "charging" cue.
                const Tick start = (g.last_erupted_at == 0) ? Tick{0}
                                                            : g.last_erupted_at + 1;
                const Tick total = (g.next_event_tick > start)
                                       ? (g.next_event_tick - start)
                                       : Tick{1};
                const Tick el    = (now >= start) ? (now - start) : Tick{0};
                gs.phase_norm = std::clamp(
                    static_cast<float>(el) / static_cast<float>(total),
                    0.0f, 1.0f);
                break;
            }
            case GeyserState::Telegraph: {
                // Telegraph window: total = telegraph_ticks, elapsed = now -
                // (next_event_tick - telegraph_ticks).
                const float ticks_total = std::max(1.0f,
                    tuning_.geyser_telegraph_sec * kSimHz);
                const Tick  start = (g.next_event_tick > static_cast<Tick>(ticks_total))
                                        ? (g.next_event_tick - static_cast<Tick>(ticks_total))
                                        : Tick{0};
                const Tick  el    = (now >= start) ? (now - start) : Tick{0};
                gs.phase_norm = std::clamp(
                    static_cast<float>(el) / ticks_total,
                    0.0f, 1.0f);
                break;
            }
            case GeyserState::Erupt: {
                gs.phase_norm = 1.0f;
                break;
            }
        }
        s.geysers.push_back(gs);
    }

    // Match timer: seconds remaining at the moment this snapshot was built.
    // 0 = unlimited or match already ended. Clamped >= 0 so the HUD doesn't
    // need its own clamp.
    if (tuning_.match_duration_sec > 0 && match_started_ && !match_ended_) {
        const Tick duration_ticks = secondsToTicks(
            static_cast<float>(tuning_.match_duration_sec));
        const Tick elapsed = (now > match_started_tick_)
                                ? (now - match_started_tick_) : 0u;
        const Tick remaining = (elapsed >= duration_ticks)
                                   ? 0u : (duration_ticks - elapsed);
        s.match_time_left_sec = static_cast<float>(remaining) / kSimHz;
    }

    // Food-rush timer. World owns the deadline tick so this is just a
    // tick-delta -> seconds conversion. 0 when no rush is active.
    {
        const Tick rush_until = world_.foodRushUntil();
        if (rush_until > now) {
            s.food_rush_time_left_sec = static_cast<float>(rush_until - now) / kSimHz;
        }
    }

    return s;
}

} // namespace cr
