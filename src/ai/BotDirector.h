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

    // Wipe the tracked player peak so a freshly-respawned player at start_mass doesn't
    // face a wave of elite bots that scaled to their previous peak.
    void resetPlayerTracking();

    const std::vector<BotMind>& bots() const { return bots_; }

    // Tracked peak mass of the human player. Used to scale bot caps and elite spawns so
    // the world remains a credible threat at any player size.
    float playerMaxMass() const { return player_max_mass_; }
    Vec2  playerPos() const { return player_pos_; }
    bool  hasPlayerLocation() const { return player_seen_; }

private:
    void           respawnBots(World& world, const Tuning& t);
    BotPersonality pickPersonality();

    Rng                  rng_;
    std::vector<BotMind> bots_;
    PlayerId             next_player_id_ = kFirstBotPlayerId;
    float                player_skill_   = 500.0f;
    float                player_max_mass_ = 100.0f; // smoothed
    Vec2                 player_pos_{4000.0f, 4000.0f};
    bool                 player_seen_    = false;
};

} // namespace cr::ai
