#include "BotController.h"

#include <algorithm>
#include <cmath>

namespace cr::ai {

namespace {

constexpr float kBigNumber = 1e30f;

const Virus* nearestVirus(const Cell& self, const World& world, float radius) {
    const Virus* best   = nullptr;
    float        best_d = radius * radius;
    for (const auto& v : world.viruses()) {
        float dx = v.pos.x - self.pos.x;
        float dy = v.pos.y - self.pos.y;
        float dsq = dx * dx + dy * dy;
        if (dsq < best_d) {
            best_d = dsq;
            best   = &v;
        }
    }
    return best;
}

} // namespace

BotDecision decide(BotMind& mind, const Cell& self, const World& world,
                   const Tuning& t, Rng& rng, Tick now) {
    const PersonalityWeights w = weightsFor(mind.personality);
    BotDecision d;
    d.move_target = self.pos;

    const float my_r    = cellRadius(self.mass);
    const float view_sq = w.view_radius * w.view_radius;

    // ---------- Single pass over cells: find threat (+sticky threat) and best prey ----------
    const Cell* threat          = nullptr;
    float       threat_d        = kBigNumber;
    const Cell* sticky_threat   = nullptr;
    float       sticky_threat_d = kBigNumber;
    const Cell* prey            = nullptr;
    float       prey_d          = kBigNumber;
    float       prey_score      = 0.0f;

    for (const auto& c : world.cells()) {
        if (c.id == self.id || c.owner == self.owner) continue;
        float dx  = c.pos.x - self.pos.x;
        float dy  = c.pos.y - self.pos.y;
        float dsq = dx * dx + dy * dy;
        if (dsq > view_sq) continue;
        float dist = std::sqrt(dsq);

        const bool is_predator = c.mass > self.mass * t.mass_ratio_required;
        const bool is_huntable = self.mass > c.mass * t.mass_ratio_required
                              && !c.god
                              && c.invuln_until <= now;

        if (is_predator) {
            if (dist < my_r * w.flee_range_mult && dist < threat_d) {
                threat   = &c;
                threat_d = dist;
            }
            if (c.id == mind.fled_threat_id) {
                sticky_threat   = &c;
                sticky_threat_d = dist;
            }
        }
        if (is_huntable && w.chase_range_mult > 0.0f) {
            float chase_dist = my_r * w.chase_range_mult;
            if (dist < chase_dist) {
                float score = c.mass / (dist + 1.0f);
                if (c.owner < kFirstBotPlayerId) score *= w.human_target_bias;
                if (score > prey_score) {
                    prey       = &c;
                    prey_d     = dist;
                    prey_score = score;
                }
            }
        }
    }

    // Flee hysteresis: while committed, the sticky threat counts even at 1.5x normal range.
    if (now < mind.flee_until
        && sticky_threat
        && sticky_threat_d < my_r * w.flee_range_mult * 1.5f) {
        threat   = sticky_threat;
        threat_d = sticky_threat_d;
    } else if (mind.flee_until > 0 && !sticky_threat) {
        // The cell we were fleeing got eaten or moved out of view -- drop the commitment.
        mind.fled_threat_id = INVALID_ENTITY;
        mind.flee_until     = 0;
    }

    // ---------- State + raw target ----------
    if (threat) {
        Vec2 away = normalize(self.pos - threat->pos);
        Vec2 perp{-away.y, away.x};
        d.move_target  = self.pos + (away * 0.85f + perp * 0.15f) * 800.0f;
        d.chosen_state = BotState::FleePredator;
        if (rng.nextFloat() < w.dash_eagerness * 0.10f) d.dash = true;
        mind.fled_threat_id = threat->id;
        mind.flee_until     = now + secondsToTicks(0.5f);
    } else if (prey) {
        d.chosen_state = BotState::ChasePrey;
        d.move_target  = prey->pos;
        const bool in_pounce_range = prey_d < my_r * 2.5f;
        const bool can_split = self.mass >= t.min_mass_to_split
                            && world.playerCellCount(self.owner) < t.max_cells_per_player
                            && self.mass > prey->mass * 2.5f;
        if (w.split_aggression > 0.0f && in_pounce_range && can_split
            && rng.nextFloat() < w.split_aggression) {
            d.split        = true;
            d.chosen_state = BotState::SplitToKill;
        }
        if (rng.nextFloat() < w.dash_eagerness * 0.05f) d.dash = true;
        mind.fled_threat_id = INVALID_ENTITY;
        mind.flee_until     = 0;
    } else {
        // No cell-target: look at food. Cheaper to scan food only when needed.
        const Food* food   = nullptr;
        float       food_d = kBigNumber;
        for (const auto& f : world.food()) {
            float dx  = f.pos.x - self.pos.x;
            float dy  = f.pos.y - self.pos.y;
            float dsq = dx * dx + dy * dy;
            if (dsq > view_sq) continue;
            if (dsq < food_d) {
                food_d = dsq;
                food   = &f;
            }
        }
        if (food) {
            d.chosen_state = BotState::SeekFood;
            d.move_target  = food->pos;
        } else {
            d.chosen_state    = BotState::Wander;
            Tick period_ticks = secondsToTicks(w.wander_period_sec);
            if (now >= mind.wander_set_at + period_ticks
                || lengthSq(mind.wander_target - self.pos) < 1600.0f) {
                mind.wander_target = {
                    rng.rangeFloat(0.0f, static_cast<float>(world.width())),
                    rng.rangeFloat(0.0f, static_cast<float>(world.height())),
                };
                mind.wander_set_at = now;
            }
            d.move_target = mind.wander_target;
            if (w.corner_pull > 0.0f) {
                Vec2 corner{
                    self.pos.x < world.width()  * 0.5f ? 600.0f : world.width()  - 600.0f,
                    self.pos.y < world.height() * 0.5f ? 600.0f : world.height() - 600.0f,
                };
                d.move_target = lerp(d.move_target, corner, w.corner_pull);
            }
        }
        mind.fled_threat_id = INVALID_ENTITY;
        mind.flee_until     = 0;
    }

    // ---------- Virus avoidance via tangent orbit (replaces, doesn't add to, target) ------
    //
    // Exit logic is the load-bearing piece. When the bot enters avoidance we snapshot its
    // original intent (where it wanted to go). The orbit exits once the bot is past the
    // virus relative to that intent -- i.e. its resumed straight-line path won't take it
    // back through the danger zone. A 4s hard fail-safe ensures we never get stuck
    // orbiting forever if the geometry conspires against us.
    const Virus* avoiding = nullptr;
    if (w.fears_viruses && self.mass > 200.0f) {
        const float enter_radius = my_r * 4.0f;
        const float exit_radius  = my_r * 6.0f;

        // Sticky check: keep avoiding the same virus until we're past it on intent's side.
        if (mind.avoiding_virus_id != INVALID_ENTITY) {
            const Virus* old = nullptr;
            for (const auto& vv : world.viruses()) {
                if (vv.id == mind.avoiding_virus_id) { old = &vv; break; }
            }
            if (!old) {
                // Virus disappeared (eaten by an explosion).
                mind.avoiding_virus_id = INVALID_ENTITY;
            } else {
                Vec2  v2intent = mind.avoid_intent - old->pos;
                Vec2  v2bot    = self.pos - old->pos;
                bool  past     = dot(v2bot, v2intent) > 0.0f;
                float dist     = distance(self.pos, old->pos);
                bool  time_up  = now >= mind.virus_avoid_until;

                if ((past && dist > exit_radius) || time_up) {
                    mind.avoiding_virus_id = INVALID_ENTITY;
                } else {
                    avoiding = old;
                }
            }
        }

        // New entry: scan for the nearest virus within trigger range.
        if (!avoiding) {
            if (const Virus* v = nearestVirus(self, world, enter_radius)) {
                avoiding = v;
                mind.avoiding_virus_id = v->id;
                mind.virus_avoid_until = now + secondsToTicks(4.0f); // hard fail-safe
                mind.avoid_intent      = d.move_target;              // snapshot intent
                Vec2 radial = normalize(self.pos - v->pos);
                if (lengthSq(radial) < 0.5f) radial = {1.0f, 0.0f};
                Vec2 tangent_ccw{-radial.y, radial.x};
                Vec2 to_intent = d.move_target - self.pos;
                mind.avoid_tangent_sign =
                    (dot(to_intent, tangent_ccw) >= 0.0f) ? int8_t{1} : int8_t{-1};
            }
        }

        if (avoiding) {
            Vec2 to_self = self.pos - avoiding->pos;
            float dv = length(to_self);
            Vec2 radial = (dv > 1.0f) ? to_self * (1.0f / dv) : Vec2{1.0f, 0.0f};
            Vec2 tangent{-radial.y, radial.x};
            if (mind.avoid_tangent_sign < 0) tangent = tangent * -1.0f;

            // Orbit point: 5r outward + 4r tangent => ~6.4r from virus, safely past
            // exit_radius (6r) so the past-virus + distance exit can actually fire.
            const float safe_radial  = my_r * 5.0f;
            const float tangent_step = my_r * 4.0f;
            d.move_target = avoiding->pos + radial * safe_radial + tangent * tangent_step;
        }
    } else if (mind.avoiding_virus_id != INVALID_ENTITY) {
        // Reckless bots shouldn't carry stale avoidance state; also clears it after dash
        // / split shrinks dropped us below the fears-mass threshold.
        mind.avoiding_virus_id = INVALID_ENTITY;
        mind.virus_avoid_until = 0;
    }

    // Reckless gets a small per-tick free-dash chance even when not fleeing or chasing.
    if (mind.personality == BotPersonality::Reckless && !d.dash) {
        if (rng.nextFloat() < 0.02f) d.dash = true;
    }

    // ---------- EMA smoothing ----------
    // Snap when fleeing or orbiting a virus -- both are safety-critical and rely on the
    // computed target being applied exactly. Otherwise use the personality's EMA alpha.
    if (d.chosen_state == BotState::FleePredator || avoiding || !mind.smoothed_init) {
        mind.smoothed_target = d.move_target;
        mind.smoothed_init   = true;
    } else {
        mind.smoothed_target = lerp(mind.smoothed_target,
                                    d.move_target,
                                    w.target_responsiveness);
    }
    d.move_target = mind.smoothed_target;

    mind.state = d.chosen_state;
    return d;
}

} // namespace cr::ai
