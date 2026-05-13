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
    rules::stepCells(world_, tuning_, dt);
    rules::stepFood(world_, tuning_, dt);
    rules::applySoftBounds(world_, tuning_);

    // 3. Interactions (collision-driven).
    rules::processEating(world_, tuning_, events_);
    rules::processVirusPushes(world_, tuning_);
    rules::processRecombine(world_, tuning_);

    // 4. World upkeep.
    rules::respawnFood(world_, tuning_);

    // 5. Spatial grid for whoever queries it next.
    world_.rebuildGrid();
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
    return s;
}

} // namespace cr
