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

    // Dev / cheat hook: force the next crashing-comet world event to fire on the
    // *next* tick (regardless of the regular cadence). Subsequent comets still respect
    // the normal interval. The actual spawn still happens deterministically inside
    // processComets so replay round-trips remain valid as long as the same forced
    // trigger sequence is replayed -- the dev console doesn't record commands so this
    // is intentionally a cheat hook, not a gameplay command.
    void triggerCometSpawn() { next_comet_spawn_tick_ = world_.currentTick(); }

    // Dev / cheat hook: same as triggerCometSpawn but for the comet-shower event.
    // Forces a full formation (main + 3..6 satellites) on the next tick. Subsequent
    // showers respect the normal cadence.
    void triggerCometShower() { next_shower_spawn_tick_ = world_.currentTick(); }

private:
    void applyCommand(const Command& cmd);

    World                  world_;
    Tuning                 tuning_;
    ai::BotDirector        director_;
    std::vector<Command>   pending_;
    std::vector<GameEvent> events_;
    // Tick at which the next crashing-comet world event should fire. Owned here (not
    // in Rules) so processComets stays stateless. Initialised in the constructor.
    Tick                   next_comet_spawn_tick_  = 0;
    Tick                   next_shower_spawn_tick_ = 0;

    // Match-end bookkeeping. match_started_tick_ is the value of currentTick at the
    // first tick() call (i.e. the start of the match). When Tuning::match_duration_sec
    // > 0 the sim watches elapsed time and emits MatchEndEvent the moment it hits
    // the duration -- the match_ended_ flag prevents the event firing more than once.
    bool                   match_started_      = false;
    Tick                   match_started_tick_ = 0;
    bool                   match_ended_        = false;
};

} // namespace cr
