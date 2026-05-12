#include "Simulation.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace cr {

Simulation::Simulation(uint64_t seed, Tuning tuning)
    : world_(seed, tuning.world_width, tuning.world_height),
      tuning_(std::move(tuning)) {
    // Spawn initial food deterministically using the world RNG.
    int target = tuning_.food_target;
    for (int i = 0; i < target; ++i) {
        Vec2 pos{
            world_.rng().rangeFloat(0.0f, static_cast<float>(world_.width())),
            world_.rng().rangeFloat(0.0f, static_cast<float>(world_.height())),
        };
        world_.spawnFood(pos);
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

    // Apply commands whose scheduled tick has come; keep the rest for later.
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

    stepCells(dt);
    world_.rebuildGrid();
    world_.advanceTick();
}

void Simulation::applyCommand(const Command& cmd) {
    if (auto* m = std::get_if<MoveCmd>(&cmd.payload)) {
        for (auto& c : world_.cellsMut()) {
            if (c.owner == cmd.player) {
                c.target = m->target;
            }
        }
        return;
    }
    // SplitCmd, EjectCmd, DashCmd: stubs in Phase 1; implemented in Phase 4 (Core Mechanics).
}

void Simulation::stepCells(float dt) {
    const float base    = tuning_.base_speed;
    const float falloff = tuning_.speed_falloff;

    for (auto& c : world_.cellsMut()) {
        Vec2  to_target = c.target - c.pos;
        float dist      = length(to_target);
        if (dist < 1e-3f) {
            c.vel = {0.0f, 0.0f};
            continue;
        }

        float r     = std::max(1.0f, cellRadius(c.mass));
        float speed = base * std::pow(30.0f / r, falloff);
        Vec2  dir   = to_target * (1.0f / dist);
        float step  = std::min(speed * dt, dist);
        c.pos = c.pos + dir * step;
        c.vel = dir * speed;
    }
}

Snapshot Simulation::buildSnapshot() const {
    Snapshot s;
    s.tick      = world_.currentTick();
    s.rng_state = world_.rng().state;
    s.cells.reserve(world_.cells().size());
    for (const auto& c : world_.cells()) {
        s.cells.push_back(CellSnap{c.id, c.owner, c.pos, c.vel, c.mass});
    }
    s.food.reserve(world_.food().size());
    for (const auto& f : world_.food()) {
        s.food.push_back(FoodSnap{f.id, f.pos});
    }
    return s;
}

} // namespace cr
