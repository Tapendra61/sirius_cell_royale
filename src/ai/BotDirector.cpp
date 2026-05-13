#include "BotDirector.h"

#include <algorithm>
#include <cmath>

namespace cr::ai {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

BotDirector::BotDirector(uint64_t seed)
    : rng_(seed ^ 0xB07C0DEull) {}

void BotDirector::tick(World& world, const Tuning& t,
                       std::vector<Command>& out) {
    // Track the human's peak cell mass and position. The mass decays slowly so a momentarily
    // shrunk player (post-split) doesn't make bots forget their threat tier. Position
    // tracks the largest current cell so elite spawns can be aimed at the player.
    float current_max = 0.0f;
    Vec2  best_pos    = player_pos_;
    bool  any_seen    = false;
    for (const auto& c : world.cells()) {
        if (c.owner < kFirstBotPlayerId) {
            any_seen = true;
            if (c.mass > current_max) {
                current_max = c.mass;
                best_pos    = c.pos;
            }
        }
    }
    player_max_mass_ = std::max(player_max_mass_ * 0.997f, std::max(100.0f, current_max));
    if (any_seen) {
        player_pos_  = best_pos;
        player_seen_ = true;
    }

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
        const Cell* anchor    = nullptr;
        float       best_mass = 0.0f;
        for (const auto& c : world.cells()) {
            if (c.owner == bot.player && c.mass > best_mass) {
                anchor    = &c;
                best_mass = c.mass;
            }
        }
        if (!anchor) continue;

        BotDecision d = decide(bot, *anchor, world, t, rng_, now, player_max_mass_);
        out.push_back(Command{bot.player, now, MoveCmd{d.move_target}});
        if (d.split) out.push_back(Command{bot.player, now, SplitCmd{}});
        if (d.eject) out.push_back(Command{bot.player, now, EjectCmd{}});
        if (d.dash)  out.push_back(Command{bot.player, now, DashCmd{}});
    }
}

void BotDirector::respawnBots(World& world, const Tuning& t) {
    while (static_cast<int>(bots_.size()) < t.bot_target_count) {
        PlayerId pid = next_player_id_++;

        // Elite spawn: as the player grows, an increasing share of new bots are scaled-up
        // threats. Ramp is faster than before -- by player ~1000 mass, ~57% of spawns are
        // elites. Cap 60%.
        float          spawn_mass  = t.start_mass;
        BotPersonality personality = pickPersonality();
        bool           is_elite    = false;

        if (player_max_mass_ > 150.0f) {
            float elite_chance =
                std::min(0.60f, (player_max_mass_ - 150.0f) / 1500.0f);
            if (rng_.nextFloat() < elite_chance) {
                is_elite = true;
                // 70-130% of player mass; cap 8000. So some elites are directly above the
                // player, capable of eating them on contact.
                float frac = 0.70f + rng_.nextFloat() * 0.60f;
                spawn_mass = std::max(t.start_mass,
                                      std::min(8000.0f, player_max_mass_ * frac));
                // Elite distribution rebalanced so Hunter no longer dominates the field:
                // 35% Hunter, 25% Reckless, 22% Hoarder, 18% Greedy. Cautious never
                // elite-spawns -- they're survival specialists, not aggressors.
                float pick = rng_.nextFloat();
                if (pick < 0.35f)      personality = BotPersonality::Hunter;
                else if (pick < 0.60f) personality = BotPersonality::Reckless;
                else if (pick < 0.82f) personality = BotPersonality::Hoarder;
                else                   personality = BotPersonality::Greedy;
            }
        }

        // Spawn location. Elite Hunters AND Reckless drop near the player so they engage
        // fast; everyone else (Hoarder, Greedy, non-elites) spawns elsewhere with a clear
        // zone from existing big cells. Hoarder picks its own corner; Greedy roams.
        Vec2 pos{};
        const bool spawn_near_player = is_elite
                                    && (personality == BotPersonality::Hunter
                                        || personality == BotPersonality::Reckless)
                                    && player_seen_;
        if (spawn_near_player) {
            float angle = rng_.rangeFloat(0.0f, 2.0f * kPi);
            float dist  = rng_.rangeFloat(2000.0f, 3500.0f);
            pos = {
                std::clamp(player_pos_.x + std::cos(angle) * dist,
                           300.0f, static_cast<float>(world.width()) - 300.0f),
                std::clamp(player_pos_.y + std::sin(angle) * dist,
                           300.0f, static_cast<float>(world.height()) - 300.0f),
            };
        } else {
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
        }

        world.spawnCell(pid, pos, spawn_mass);

        BotMind mind;
        mind.player        = pid;
        mind.personality   = personality;
        mind.wander_target = pos;
        mind.wander_set_at = world.currentTick();
        mind.is_elite      = is_elite;
        bots_.push_back(mind);
    }
}

BotPersonality BotDirector::pickPersonality() {
    int n = static_cast<int>(BotPersonality::Count);
    return static_cast<BotPersonality>(rng_.rangeInt(0, n - 1));
}

void BotDirector::recordPlayerFinishingMass(float mass) {
    player_skill_ = player_skill_ + 0.2f * (mass - player_skill_);
}

} // namespace cr::ai
