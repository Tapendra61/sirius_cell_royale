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
    rules::applyMagnetPulls(world_, tuning_);
    rules::processBlackHoles(world_, tuning_, dt);
    rules::stepCells(world_, tuning_, dt);
    rules::stepFood(world_, tuning_, dt);
    rules::applySoftBounds(world_, tuning_);

    // 3. Rebuild spatial grids once positions have settled, so the eating step can use
    //    them. Grids store vector indices; valid until the first push_back / compactDead
    //    in this tick (i.e. through processEating's queries but stale after).
    world_.rebuildGrids();

    // 4. Interactions (collision-driven).
    rules::processEating(world_, tuning_, events_);
    rules::processVirusPushes(world_, tuning_);
    rules::processRecombine(world_, tuning_);
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
        // Tag with personality if this cell is bot-owned (small linear scan; bot count <= ~30).
        // Also surface Hunter dash-windup progress + elite flag so the renderer can draw
        // both tells.
        cs.personality_tag     = 0;
        cs.dash_telegraph_norm = 0.0f;
        cs.is_elite            = false;
        for (const auto& bot : bots) {
            if (bot.player == c.owner) {
                cs.personality_tag = static_cast<uint8_t>(bot.personality) + 1;
                cs.is_elite        = bot.is_elite;
                if (bot.dash_windup_until > now
                    && bot.dash_windup_until > bot.dash_windup_started) {
                    float dur = static_cast<float>(bot.dash_windup_until - bot.dash_windup_started);
                    float el  = static_cast<float>(now - bot.dash_windup_started);
                    cs.dash_telegraph_norm = std::clamp(el / dur, 0.0f, 1.0f);
                }
                break;
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
    return s;
}

} // namespace cr
