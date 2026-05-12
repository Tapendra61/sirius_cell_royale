#include "BotDirector.h"

#include <algorithm>

namespace cr::ai {

BotDirector::BotDirector(uint64_t seed)
    : rng_(seed ^ 0xB07C0DEull) {}

void BotDirector::tick(World& world, const Tuning& t,
                       std::vector<Command>& out) {
    // Cull bots whose cells were eaten this tick.
    bots_.erase(std::remove_if(bots_.begin(), bots_.end(),
                               [&](const BotMind& b) {
                                   return world.playerCellCount(b.player) == 0;
                               }),
                bots_.end());

    respawnBots(world, t);

    const Tick now = world.currentTick();
    for (auto& bot : bots_) {
        // Use the largest cell as the brain anchor; secondary cells follow same target.
        const Cell* anchor   = nullptr;
        float       best_mass = 0.0f;
        for (const auto& c : world.cells()) {
            if (c.owner == bot.player && c.mass > best_mass) {
                anchor    = &c;
                best_mass = c.mass;
            }
        }
        if (!anchor) continue;

        BotDecision d = decide(bot, *anchor, world, t, rng_, now);
        out.push_back(Command{bot.player, now, MoveCmd{d.move_target}});
        if (d.split) out.push_back(Command{bot.player, now, SplitCmd{}});
        if (d.eject) out.push_back(Command{bot.player, now, EjectCmd{}});
        if (d.dash)  out.push_back(Command{bot.player, now, DashCmd{}});
    }
}

void BotDirector::respawnBots(World& world, const Tuning& t) {
    while (static_cast<int>(bots_.size()) < t.bot_target_count) {
        PlayerId pid = next_player_id_++;

        // Spawn somewhere with a small clear-zone from existing big cells.
        Vec2  pos{};
        for (int attempt = 0; attempt < 6; ++attempt) {
            pos = {
                rng_.rangeFloat(200.0f, static_cast<float>(world.width())  - 200.0f),
                rng_.rangeFloat(200.0f, static_cast<float>(world.height()) - 200.0f),
            };
            bool too_close = false;
            for (const auto& c : world.cells()) {
                if (c.mass < 200.0f) continue;
                if (distance(c.pos, pos) < cellRadius(c.mass) + 200.0f) {
                    too_close = true;
                    break;
                }
            }
            if (!too_close) break;
        }

        world.spawnCell(pid, pos, t.start_mass);

        BotMind mind;
        mind.player        = pid;
        mind.personality   = pickPersonality();
        mind.wander_target = pos;
        mind.wander_set_at = world.currentTick();
        bots_.push_back(mind);
    }
}

BotPersonality BotDirector::pickPersonality() {
    // For Phase 5: uniform mix. Skill-based skew (more Greedy if player is small;
    // more Hunters if player is dominating) lands in Phase 5b once we accumulate
    // finishing-mass samples.
    int n = static_cast<int>(BotPersonality::Count);
    return static_cast<BotPersonality>(rng_.rangeInt(0, n - 1));
}

void BotDirector::recordPlayerFinishingMass(float mass) {
    // Exponential moving average (alpha = 0.2).
    player_skill_ = player_skill_ + 0.2f * (mass - player_skill_);
}

} // namespace cr::ai
