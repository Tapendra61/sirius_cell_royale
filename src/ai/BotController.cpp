#include "BotController.h"

#include <algorithm>
#include <cmath>

namespace cr::ai {

namespace {

constexpr float kBigNumber = 1e30f;

// Hunter-only dash telegraph: instead of firing the dash this tick we queue a 0.18-0.40s
// windup. The player sees a growing yellow ring around the bot and gets a chance to react.
// All other personalities still dash instantly via d.dash = true.
void queueDashOrFire(BotMind& mind, BotDecision& d, const Cell& self, Rng& rng, Tick now) {
    if (self.dash_cooldown_until > now) return; // dash on cooldown anyway
    if (mind.personality == BotPersonality::Hunter) {
        if (mind.dash_windup_until == 0) {
            Tick ticks = secondsToTicks(0.18f + rng.nextFloat() * 0.22f);
            mind.dash_windup_started = now;
            mind.dash_windup_until   = now + ticks;
        }
    } else {
        d.dash = true;
    }
}

bool dashCommitted(const BotMind& mind, const BotDecision& d) {
    return d.dash || mind.dash_windup_until > 0;
}

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

// Value-weighted food scorer. score = mass_bonus / (1 + dist²/300²) so a 300px-distant
// food is roughly half as attractive as one at 0px, and high-mass food multiplies the
// bonus by personality food_value_weight. With weight=2 (Greedy), epic (mass 12) at
// 300px outranks common (mass 1) at 50px -- the bot commits to the longer trip.
//
// Uses the foods spatial grid (rebuilt at the end of the previous tick) so we only score
// food in the bot's view AABB instead of all 3600 entries. Scratch vector is thread_local
// so we don't allocate per call.
const Food* findBestFood(const Cell& self, const World& world, const PersonalityWeights& w) {
    static thread_local std::vector<uint32_t> nearby;
    nearby.clear();

    Vec2 lo{self.pos.x - w.view_radius, self.pos.y - w.view_radius};
    Vec2 hi{self.pos.x + w.view_radius, self.pos.y + w.view_radius};
    world.foodsGrid().query(lo, hi, nearby);

    const Food*  best       = nullptr;
    float        best_score = -1.0f;
    const float  view_sq    = w.view_radius * w.view_radius;
    const auto&  foods      = world.food();

    for (uint32_t fi : nearby) {
        if (fi >= foods.size()) continue;
        const Food& f = foods[fi];
        float dx  = f.pos.x - self.pos.x;
        float dy  = f.pos.y - self.pos.y;
        float dsq = dx * dx + dy * dy;
        if (dsq > view_sq) continue;
        float mass_bonus = 1.0f + (f.mass - 1.0f) * w.food_value_weight;
        float score = mass_bonus / (1.0f + dsq * 1.111e-5f); // 1.111e-5 ≈ 1/300²
        if (score > best_score) {
            best_score = score;
            best       = &f;
        }
    }
    return best;
}

} // namespace

BotDecision decide(BotMind& mind, const Cell& self, const World& world,
                   const Tuning& t, Rng& rng, Tick now, float player_max_mass) {
    const PersonalityWeights w = weightsFor(mind.personality);
    BotDecision d;
    d.move_target = self.pos;

    const float my_r    = cellRadius(self.mass);
    const float view_sq = w.view_radius * w.view_radius;

    // Hunter dash windup: when the queued windup has elapsed, fire the actual dash.
    if (mind.dash_windup_until > 0 && now >= mind.dash_windup_until) {
        if (self.dash_cooldown_until <= now) {
            d.dash = true;
        }
        mind.dash_windup_until   = 0;
        mind.dash_windup_started = 0;
    }

    // ---------- Single pass over cells: find threat (+sticky threat) and best prey ----------
    const Cell* threat          = nullptr;
    float       threat_d        = kBigNumber;
    const Cell* sticky_threat   = nullptr;
    float       sticky_threat_d = kBigNumber;
    const Cell* prey            = nullptr;
    float       prey_d          = kBigNumber;
    float       prey_score      = 0.0f;
    const Cell* sticky_prey     = nullptr;
    float       sticky_prey_d   = kBigNumber;
    const float sticky_view_sq  = (w.view_radius * 1.5f) * (w.view_radius * 1.5f);

    for (const auto& c : world.cells()) {
        if (c.id == self.id || c.owner == self.owner) continue;
        // Stealth pickup: the holder is invisible to bot AI. Bots can't target them
        // as prey *or* avoid them as a threat -- the player can sneak past safely or
        // bait bots into running into a much larger stealthed cell.
        if (c.stealth_until > now) continue;
        // Cells hiding in a black hole don't exist for AI purposes either, and
        // neither do cells mid-eject (the exit animation grants prey immunity and
        // ends in ~0.35s; bots wasting attention chasing them is just noise).
        if (c.hiding_in != INVALID_ENTITY) continue;
        if (c.exit_anim_until > now) continue;
        float dx  = c.pos.x - self.pos.x;
        float dy  = c.pos.y - self.pos.y;
        float dsq = dx * dx + dy * dy;

        // Sticky chase target -- 1.5x view radius so a fleeing target stays locked briefly.
        if (c.id == mind.chasing_id
            && now < mind.chase_committed_until
            && dsq < sticky_view_sq
            && self.mass > c.mass * t.mass_ratio_required
            && !c.god
            && c.invuln_until <= now) {
            sticky_prey   = &c;
            sticky_prey_d = std::sqrt(dsq);
        }

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

    // Sticky prey overrides the score-based pick: once a Hunter locks onto a target they
    // commit to it even if a "better" prey appears mid-chase. This is what makes them
    // relentless.
    if (sticky_prey) {
        prey   = sticky_prey;
        prey_d = sticky_prey_d;
    } else if (mind.chasing_id != INVALID_ENTITY) {
        // Lost sight (gone, eaten, escaped). Drop the lock.
        mind.chasing_id           = INVALID_ENTITY;
        mind.chase_committed_until = 0;
    }

    // Establish a new lock when we pick up a human prey. Hunter commits for 4s (the
    // relentless tracker); Reckless commits for 2s (the wild card who'll eventually
    // wander off but bites hard while engaged).
    if (prey && prey->owner < kFirstBotPlayerId) {
        if (mind.personality == BotPersonality::Hunter) {
            mind.chasing_id           = prey->id;
            mind.chase_committed_until = now + secondsToTicks(4.0f);
        } else if (mind.personality == BotPersonality::Reckless) {
            mind.chasing_id           = prey->id;
            mind.chase_committed_until = now + secondsToTicks(2.0f);
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
        if (rng.nextFloat() < w.dash_eagerness * 0.10f) queueDashOrFire(mind, d, self, rng, now);
        // Smart dash: when a predator is right on top of us and dash is ready, almost
        // always burn it. Survival over efficiency.
        if (!dashCommitted(mind, d) && self.dash_cooldown_until <= now
            && threat_d < my_r * 3.0f
            && w.dash_eagerness > 0.0f
            && rng.nextFloat() < 0.85f) {
            queueDashOrFire(mind, d, self, rng, now);
        }
        // Panic blast: push the predator away. Same gates as the prey-chasing
        // case (min mass + cooldown + range) but we skip the aggression roll
        // when the threat is dangerously close -- survival overrides flavor.
        if (w.blast_aggression > 0.0f
            && self.mass >= t.blast_min_mass
            && self.blast_cooldown_until <= now
            && threat_d < t.blast_radius * 0.9f
            && rng.nextFloat() < std::min(1.0f, w.blast_aggression * 1.5f)) {
            d.blast = true;
        }
        mind.fled_threat_id = threat->id;
        mind.flee_until     = now + secondsToTicks(0.5f);
    } else if (prey) {
        d.chosen_state = BotState::ChasePrey;
        // Lead-aim: aim where prey will be in prey_lead_seconds, not where it is.
        // Hunter's 0.5s lead turns half-decent chases into clean interceptions.
        Vec2 prey_target = prey->pos;
        if (w.prey_lead_seconds > 0.0f) {
            prey_target = prey_target + prey->vel * w.prey_lead_seconds;
        }
        // Pack-flank: for Hunters / Apex chasing the *human* player, nudge the
        // aim point perpendicularly off the straight chase line by an amount
        // proportional to the bot's own radius. Each bot has a distinct
        // flank_radians (set at spawn), so a swarm of three Hunters ends up
        // approaching the player from three sides instead of stacking. Bots
        // already converge once close (the perpendicular shrinks relative to
        // distance), so the kill move still works.
        if ((mind.personality == BotPersonality::Hunter
             || mind.personality == BotPersonality::Apex)
            && prey->owner < kFirstBotPlayerId) {
            Vec2 to_prey = prey_target - self.pos;
            float dist  = std::sqrt(lengthSq(to_prey));
            if (dist > 1.0f) {
                Vec2 forward{to_prey.x / dist, to_prey.y / dist};
                Vec2 perp{-forward.y, forward.x};
                // Offset magnitude: bot's own radius * sin(flank). Scaled down as
                // the bot closes -- at very close range we want a clean strike,
                // not a brushing pass. ramp factor 0..1 in 0..2000 distance band.
                float ramp = std::clamp(dist / 2000.0f, 0.0f, 1.0f);
                float off  = std::sin(mind.flank_radians) * my_r * 3.5f * ramp;
                prey_target = prey_target + perp * off;
            }
        }
        d.move_target = prey_target;
        const bool in_pounce_range = prey_d < my_r * 2.5f;
        const bool can_split = self.mass >= t.min_mass_to_split
                            && world.playerCellCount(self.owner) < t.max_cells_per_player
                            && self.mass > prey->mass * 2.5f;
        if (w.split_aggression > 0.0f && in_pounce_range && can_split
            && rng.nextFloat() < w.split_aggression) {
            d.split        = true;
            d.chosen_state = BotState::SplitToKill;
        }
        if (rng.nextFloat() < w.dash_eagerness * 0.05f) queueDashOrFire(mind, d, self, rng, now);
        // Smart dash: prey just outside pounce range, dash ready -> close the gap.
        if (!dashCommitted(mind, d) && self.dash_cooldown_until <= now
            && prey_d > my_r * 1.5f && prey_d < my_r * 4.5f
            && w.dash_eagerness > 0.0f
            && rng.nextFloat() < 0.85f) {
            queueDashOrFire(mind, d, self, rng, now);
        }
        // Q-blast: fire when prey is in blast range AND personality favors it.
        // The blast pushes the prey + nearby food / cells outward, so it's most
        // valuable as a *disruption* tool (scatter the player's split pieces;
        // knock the prey away from a black hole they were diving for). The
        // ability has a min-mass requirement and a cooldown -- the sim's
        // doBlast no-ops if either is unmet, so a missed roll doesn't break.
        if (w.blast_aggression > 0.0f
            && self.mass >= t.blast_min_mass
            && self.blast_cooldown_until <= now
            && prey_d < t.blast_radius * 0.8f
            && rng.nextFloat() < w.blast_aggression) {
            d.blast = true;
        }
        mind.fled_threat_id = INVALID_ENTITY;
        mind.flee_until     = 0;
    } else {
        // No cell-target. Score food by value × proximity, skip the search entirely once
        // we've grown past the dynamic mass cap. Cap scales with the player's tracked peak
        // mass so bots remain a credible threat at any player size (Hunter at 1.3 caps
        // ~55% above the player; Cautious at 0.6 stays well below).
        const float cap_base = std::max(500.0f, player_max_mass * 1.2f);
        const float mass_cap = cap_base * w.max_mass_factor;
        const Food* food = (self.mass >= mass_cap) ? nullptr : findBestFood(self, world, w);

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

    // Reckless quirks: free dash, occasional direction flip, random eject. The combination
    // gives them a visibly twitchy "what's wrong with that one" feel.
    if (mind.personality == BotPersonality::Reckless) {
        if (!d.dash && rng.nextFloat() < 0.02f) d.dash = true;
        if (d.chosen_state == BotState::Wander && rng.nextFloat() < 0.015f) {
            // Reflect the move target across self.pos -- bot abruptly reverses course.
            d.move_target = Vec2{2.0f * self.pos.x - d.move_target.x,
                                 2.0f * self.pos.y - d.move_target.y};
        }
        if (!d.eject
            && self.mass >= t.min_mass_to_eject + 50.0f
            && rng.nextFloat() < 0.005f) {
            d.eject = true;
        }
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
