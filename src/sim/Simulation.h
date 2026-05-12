#pragma once

#include "World.h"
#include "ai/BotDirector.h"
#include "core/Command.h"
#include "core/Events.h"
#include "core/Snapshot.h"
#include "core/Tuning.h"

#include <utility>
#include <vector>

namespace cr {

// Authoritative simulation. Pure: no input polling, no clock reads, no rendering.
// Determinism contract: same seed + same Tuning + same command stream =>
// byte-identical snapshots at every tick.
class Simulation {
public:
    Simulation(uint64_t seed, Tuning tuning);

    // Queue a command for application on or after its tick.
    void queueCommand(Command cmd);

    // Advance one fixed-step tick (dt = 1 / sim_hz).
    void tick(float dt);

    // Capture the visible state at the current tick.
    Snapshot buildSnapshot() const;

    Tick currentTick() const { return world_.currentTick(); }

    World&       world() { return world_; }
    const World& world() const { return world_; }

    const Tuning& tuning() const { return tuning_; }
    void          setTuning(Tuning t) { tuning_ = std::move(t); }

    ai::BotDirector&       director() { return director_; }
    const ai::BotDirector& director() const { return director_; }

    // Events produced during the most recent tick(). Cleared at the start of each tick().
    const std::vector<GameEvent>& events() const { return events_; }
    std::vector<GameEvent>        takeEvents();

private:
    void applyCommand(const Command& cmd);

    World                  world_;
    Tuning                 tuning_;
    ai::BotDirector        director_;
    std::vector<Command>   pending_;
    std::vector<GameEvent> events_;
};

} // namespace cr
