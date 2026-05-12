#pragma once

#include "BotController.h"
#include "core/Command.h"
#include "core/Rng.h"
#include "core/Tuning.h"
#include "sim/World.h"

#include <vector>

namespace cr::ai {

// Maintains the bot population and runs decision-making each tick. Bot decisions go
// through the same Command pipeline as the player, so determinism + replay still hold:
// same seed -> same Rng state -> same decisions.
class BotDirector {
public:
    explicit BotDirector(uint64_t seed);

    void tick(World& world, const Tuning& t, std::vector<Command>& out_commands);

    // Optional skill input from the client when the player dies. Used to scale bot caps.
    void recordPlayerFinishingMass(float mass);

    const std::vector<BotMind>& bots() const { return bots_; }

private:
    void           respawnBots(World& world, const Tuning& t);
    BotPersonality pickPersonality();

    Rng                  rng_;
    std::vector<BotMind> bots_;
    PlayerId             next_player_id_ = kFirstBotPlayerId;
    float                player_skill_   = 500.0f;
};

} // namespace cr::ai
