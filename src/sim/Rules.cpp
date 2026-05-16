#include "Rules.h"

#include "ai/BotPersonality.h" // kFirstBotPlayerId (player < this == human player)

#include <algorithm>
#include <cmath>
#include <utility>

namespace cr::rules {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Remove elements where dead[i] is true, preserving the order of survivors.
// Caller must ensure dead.size() == vec.size() (the contract on entry to processEating).
template <typename T>
void compactDead(std::vector<T>& vec, const std::vector<char>& dead) {
    size_t w = 0;
    for (size_t r = 0; r < vec.size(); ++r) {
        if (!dead[r]) {
            if (w != r) vec[w] = std::move(vec[r]);
            ++w;
        }
    }
    vec.resize(w);
}

float cellSpeed(float mass, const Tuning& t) {
    float r = std::max(1.0f, cellRadius(mass));
    return t.base_speed * std::pow(30.0f / r, t.speed_falloff);
}

void decayVec(Vec2& v, float decay_per_sec, float dt) {
    float factor = std::max(0.0f, 1.0f - decay_per_sec * dt);
    v = v * factor;
    if (lengthSq(v) < 0.25f) v = {0.0f, 0.0f};
}

// Spawn `pieces` cells around `pos` in an evenly-spaced fan, each with launch velocity
// outward and a shared recombine timer. Respects max_cells_per_player. Used by virus
// explosions; doSplit has its own variant (splits in the player's aim direction).
//
// Pieces are laid out as a ring tangent to itself -- each piece's centre is at
// `ring_r = piece_radius / sin(pi / N)` from `pos`, so adjacent pieces just
// touch instead of overlapping at the centre. Previously every piece spawned
// at `pos` and visibly stacked into a single blob until launch_vel pushed
// them apart, which read as "one weird cell" for the first half second.
void spawnExplosionChildren(World& world, PlayerId owner, Vec2 pos, float total_mass,
                            const Tuning& t, std::vector<GameEvent>& events,
                            EntityId source_id, uint8_t source_personality_tag) {
    constexpr int kMaxPieces = 8;
    int existing  = world.playerCellCount(owner);
    int allowed   = std::max(1, t.max_cells_per_player - existing);
    int pieces    = std::min(kMaxPieces, allowed);
    if (pieces <= 0) return;
    float each_mass = total_mass / static_cast<float>(pieces);
    const Tick recombine_at =
        world.currentTick() + secondsToTicks(t.recombine_delay_sec);

    // Tangent-ring radius: for N pieces evenly spaced on a circle, adjacent
    // centres are `2 * ring_r * sin(pi/N)` apart. We want that distance to
    // equal `2 * piece_radius` so adjacent pieces just touch. Solve for
    // ring_r => piece_radius / sin(pi/N). With N=2 that's piece_radius (two
    // pieces on opposite sides touching at the original centre); at N=8
    // it's ~2.6 * piece_radius.
    const float piece_r = cellRadius(each_mass);
    float ring_r = 0.0f;
    if (pieces > 1) {
        const float s = std::sin(kPi / static_cast<float>(pieces));
        if (s > 1e-4f) ring_r = piece_r / s;
        // Tiny extra gap so adjacent pieces are non-overlapping by ~1px,
        // which prevents the spatial-grid collision pass from flickering.
        ring_r += 1.0f;
    }

    for (int i = 0; i < pieces; ++i) {
        float angle = i * (2.0f * kPi / pieces);
        Vec2  dir{std::cos(angle), std::sin(angle)};
        Vec2  spawn_pos = pos + dir * ring_r;
        EntityId nid = world.spawnCell(owner, spawn_pos, each_mass);
        // spawnCell may realloc; resolve by id, never by index.
        if (auto* nc = world.findCell(nid)) {
            nc->launch_vel       = dir * t.launch_velocity;
            // Target is well past the spawn ring so launch continues outward.
            nc->target           = pos + dir * (ring_r + 200.0f);
            nc->recombine_at     = recombine_at;
            nc->personality_tag  = source_personality_tag;
        }
    }
    events.push_back(SplitEvent{owner, source_id, INVALID_ENTITY});
}

} // namespace

void stepCells(World& world, const Tuning& t, float dt) {
    const Tick now = world.currentTick();
    for (auto& c : world.cellsMut()) {
        // Cells hiding / mid-entry / mid-exit are driven by processBlackHoles
        // (BH) or processWormholes (WH) -- don't let the normal seek-velocity
        // overwrite the animation positions. The entry_anim check catches
        // wormhole phase 1 (cell pinned at source, shrinking); BH entry
        // already gets caught by the hiding_in check above so this is just
        // a defence-in-depth duplicate for that case.
        if (c.hiding_in != INVALID_ENTITY) continue;
        if (c.entry_anim_until > now)      continue;
        if (c.exit_anim_until > now)       continue;
        decayVec(c.launch_vel, t.launch_decay, dt);

        Vec2  to_target = c.target - c.pos;
        float dist      = length(to_target);
        Vec2  dir{0.0f, 0.0f};
        if (dist > 1e-3f) {
            dir = to_target * (1.0f / dist);
        }

        float speed = cellSpeed(c.mass, t);
        if (now < c.dash_until) {
            speed *= t.speed_multiplier;
        }

        // Anti-jitter: when the cell is closer than a full-speed step (speed * dt)
        // to its target, the original code took a full step anyway and overshot;
        // next tick the cell aimed back the other way and overshot again. Result:
        // the cell oscillates ~step-size pixels around the cursor whenever the
        // cursor is "right above" it. Scaling the speed so the step length equals
        // the distance lands the cell exactly on target and stops it cleanly.
        // Below the 1e-3 threshold we already set dir to zero, so the multiplication
        // produces no motion -- no special-case needed for the truly-on-target case.
        const float max_step = speed * dt;
        if (dist < max_step && dt > 1e-5f) {
            speed = dist / dt;
        }

        Vec2 seek_vel  = dir * speed;
        Vec2 total_vel = seek_vel + c.launch_vel;
        c.pos          = c.pos + total_vel * dt;
        c.vel          = total_vel;
    }
}

void stepFood(World& world, const Tuning& t, float dt) {
    for (auto& f : world.foodMut()) {
        if (lengthSq(f.vel) < 0.25f) {
            f.vel = {0.0f, 0.0f};
            continue;
        }
        f.pos = f.pos + f.vel * dt;
        decayVec(f.vel, t.launch_decay, dt);
    }
    for (auto& v : world.virusesMut()) {
        if (lengthSq(v.vel) < 0.25f) {
            v.vel = {0.0f, 0.0f};
            continue;
        }
        v.pos = v.pos + v.vel * dt;
        decayVec(v.vel, t.launch_decay * 0.5f, dt); // viruses drift longer
    }
}

void applySoftBounds(World& world, const Tuning& /*t*/) {
    const float w = static_cast<float>(world.width());
    const float h = static_cast<float>(world.height());
    const float k = 0.15f; // per-tick pull-back fraction past the margin

    auto clampOne = [&](Vec2& pos, float margin) {
        if (pos.x < margin)          pos.x += (margin - pos.x) * k;
        else if (pos.x > w - margin) pos.x -= (pos.x - (w - margin)) * k;
        if (pos.y < margin)          pos.y += (margin - pos.y) * k;
        else if (pos.y > h - margin) pos.y -= (pos.y - (h - margin)) * k;
    };

    for (auto& c : world.cellsMut()) clampOne(c.pos, cellRadius(c.mass) + 4.0f);
    for (auto& f : world.foodMut()) {
        // Stop fast-moving pellets when they reach the wall, then let them settle as food.
        const float margin = 8.0f;
        if (f.pos.x < margin || f.pos.x > w - margin
            || f.pos.y < margin || f.pos.y > h - margin) {
            f.vel = {0.0f, 0.0f};
        }
        clampOne(f.pos, margin);
    }
    for (auto& v : world.virusesMut()) clampOne(v.pos, cellRadius(v.mass) + 4.0f);
}

void processEating(World& world, const Tuning& t, std::vector<GameEvent>& events) {
    auto& cells   = world.cellsMut();
    auto& foods   = world.foodMut();
    auto& viruses = world.virusesMut();

    std::vector<char> cell_dead(cells.size(), 0);
    std::vector<char> food_dead(foods.size(), 0);
    std::vector<char> virus_dead(viruses.size(), 0);

    // Virus explosions defer their spawning until after compaction so we never push to
    // cells_ while iterating it (which would invalidate the cell_dead bounds).
    struct PendingExplosion {
        PlayerId owner;
        Vec2     pos;
        float    mass;
        EntityId source_id;
        uint8_t  personality_tag;
    };
    std::vector<PendingExplosion> pending_explosions;

    const Tick   now   = world.currentTick();
    const size_t count = cells.size();

    // Reused across the inner queries to avoid per-cell allocations.
    std::vector<uint32_t> nearby;

    for (size_t i = 0; i < count; ++i) {
        if (cell_dead[i]) continue;

        // Precompute the cell's reach + AABB once. pr may grow slightly as we eat food
        // in this iteration; we accept that as a slight conservatism (a tiny piece of
        // food just outside pr will be picked up next tick).
        const float pr = cellRadius(cells[i].mass);
        // AABB slack -- the food/virus grid was built before this loop, and an in-flight
        // pellet may have crept a few px since. 12 px is well beyond max sim-tick travel.
        const float slack  = 12.0f;
        const Vec2  q_lo{cells[i].pos.x - (pr + slack), cells[i].pos.y - (pr + slack)};
        const Vec2  q_hi{cells[i].pos.x + (pr + slack), cells[i].pos.y + (pr + slack)};

        // --- food ---
        nearby.clear();
        world.foodsGrid().query(q_lo, q_hi, nearby);
        for (uint32_t fi : nearby) {
            if (fi >= food_dead.size() || food_dead[fi]) continue;
            const Food& f = foods[fi];
            float dx = cells[i].pos.x - f.pos.x;
            float dy = cells[i].pos.y - f.pos.y;
            float dsq = dx * dx + dy * dy;
            float fr = foodRadius(f.mass);
            float reach = pr + fr;
            if (dsq > reach * reach) continue;
            // Crit roll: rare bonus (5% by default). Crit mass adds on top of base.
            float gained  = f.mass;
            bool  is_crit = world.rng().nextFloat() < t.chance_per_absorb;
            if (is_crit) gained *= t.bonus_multiplier;
            cells[i].mass += gained;
            events.push_back(AbsorbEvent{cells[i].id, f.id, f.pos, gained});
            if (is_crit) events.push_back(CritEvent{cells[i].id, f.pos, gained});
            food_dead[fi] = 1;
        }

        // --- cells ---  bounded by initial count via the cell_dead size guard. The
        // grid contains pre-eating indices and never produces > count (rebuildGrids was
        // called before processEating with cells.size() == count).
        nearby.clear();
        world.cellsGrid().query(q_lo, q_hi, nearby);
        for (uint32_t j_u : nearby) {
            size_t j = j_u;
            if (j == i || j >= count || cell_dead[j]) continue;
            Cell& prey = cells[j];
            if (prey.owner == cells[i].owner) continue;

            float dx = cells[i].pos.x - prey.pos.x;
            float dy = cells[i].pos.y - prey.pos.y;
            float dsq = dx * dx + dy * dy;
            float sr = cellRadius(prey.mass);
            float reach = pr + sr;
            if (dsq > reach * reach) continue;
            float d = std::sqrt(dsq);
            float overlap = reach - d;
            if (overlap < sr * t.overlap_required) continue;

            const bool can_eat = !prey.god
                              && prey.invuln_until <= now
                              && prey.shield_until <= now
                              && prey.hiding_in == INVALID_ENTITY
                              && prey.exit_anim_until <= now    // mid-eject is invulnerable
                              && cells[i].hiding_in == INVALID_ENTITY
                              && cells[i].mass >= prey.mass * t.mass_ratio_required;

            if (can_eat) {
                float gained  = prey.mass;
                bool  is_crit = world.rng().nextFloat() < t.chance_per_absorb;
                if (is_crit) gained *= t.bonus_multiplier;
                cells[i].mass += gained;
                events.push_back(AbsorbEvent{cells[i].id, prey.id, prey.pos, gained});
                if (is_crit) events.push_back(CritEvent{cells[i].id, prey.pos, gained});
                events.push_back(DeathEvent{
                    /*player=*/prey.owner,
                    /*by=*/cells[i].id,
                    /*predator_player=*/cells[i].owner,
                    /*prey_personality=*/prey.personality_tag,
                    /*predator_personality=*/cells[i].personality_tag});
                cell_dead[j] = 1;
            } else if (cells[i].mass > prey.mass
                       && overlap >= sr * t.near_miss_threshold) {
                events.push_back(NearMissEvent{cells[i].id, prey.id, prey.pos});
            }
        }

        // --- viruses ---
        nearby.clear();
        world.virusesGrid().query(q_lo, q_hi, nearby);
        for (uint32_t vi : nearby) {
            if (vi >= virus_dead.size() || virus_dead[vi]) continue;
            const Virus& v = viruses[vi];
            float dx = cells[i].pos.x - v.pos.x;
            float dy = cells[i].pos.y - v.pos.y;
            float dsq = dx * dx + dy * dy;
            float vr = cellRadius(v.mass);
            float reach = pr + vr;
            if (dsq > reach * reach) continue;
            if (cells[i].mass > v.mass) {
                // Shield protects from virus explosions too -- a shielded cell pops the
                // virus harmlessly (so the player can use shield to disarm map hazards).
                if (cells[i].shield_until > now) {
                    virus_dead[vi] = 1;
                    break;
                }
                pending_explosions.push_back(PendingExplosion{
                    cells[i].owner, cells[i].pos, cells[i].mass, cells[i].id,
                    cells[i].personality_tag});
                cell_dead[i]   = 1;
                virus_dead[vi] = 1;
                break; // this cell is now dead; move on
            }
        }
    }

    compactDead(cells,   cell_dead);
    compactDead(foods,   food_dead);
    compactDead(viruses, virus_dead);

    for (const auto& exp : pending_explosions) {
        spawnExplosionChildren(world, exp.owner, exp.pos, exp.mass, t, events,
                               exp.source_id, exp.personality_tag);
    }
}

void processVirusPushes(World& world, const Tuning& t) {
    auto& foods   = world.foodMut();
    auto& viruses = world.virusesMut();

    for (size_t fi = 0; fi < foods.size(); ++fi) {
        Food& f = foods[fi];
        // Only pellets in flight push viruses.
        if (lengthSq(f.vel) < 100.0f * 100.0f) continue;

        for (auto& v : viruses) {
            float vr = cellRadius(v.mass);
            float d  = distance(f.pos, v.pos);
            if (d > vr + foodRadius(f.mass)) continue;
            // Push the virus in the pellet's direction; pellet stops just outside.
            Vec2 dir = normalize(f.vel);
            v.vel = v.vel + dir * (t.eject_velocity * 0.2f);
            f.pos = v.pos + (-1.0f * dir) * (vr + foodRadius(f.mass) + 1.0f);
            f.vel = {0.0f, 0.0f};
            break;
        }
    }
}

void processRecombine(World& world, const Tuning& /*t*/,
                      std::vector<GameEvent>& events) {
    auto& cells = world.cellsMut();
    if (cells.size() < 2) return;
    const Tick now = world.currentTick();
    std::vector<char> dead(cells.size(), 0);

    for (size_t i = 0; i < cells.size(); ++i) {
        if (dead[i]) continue;
        if (cells[i].recombine_at > now) continue;
        if (cells[i].hiding_in != INVALID_ENTITY) continue;

        for (size_t j = i + 1; j < cells.size(); ++j) {
            if (dead[j]) continue;
            if (cells[j].owner != cells[i].owner) continue;
            if (cells[j].recombine_at > now) continue;
            if (cells[j].hiding_in != INVALID_ENTITY) continue;

            float ar = cellRadius(cells[i].mass);
            float br = cellRadius(cells[j].mass);
            float d  = distance(cells[i].pos, cells[j].pos);
            // Touching is enough -- recombine_at has already enforced the cooldown.
            if (d > ar + br) continue;

            // Merge point: roughly the midpoint between the two centres,
            // mass-weighted toward the heavier cell so the visual ring
            // doesn't pop off-centre when a tiny piece merges into a big
            // one.
            const float total_mass = cells[i].mass + cells[j].mass;
            const float wj = cells[j].mass / total_mass;
            Vec2 merge_pos{
                cells[i].pos.x + (cells[j].pos.x - cells[i].pos.x) * wj,
                cells[i].pos.y + (cells[j].pos.y - cells[i].pos.y) * wj,
            };

            cells[i].mass += cells[j].mass;
            dead[j] = 1;

            RecombineEvent ev;
            ev.player   = cells[i].owner;
            ev.at       = merge_pos;
            ev.new_mass = cells[i].mass;
            events.push_back(ev);
        }
    }
    compactDead(cells, dead);
}

void resolveOwnerOverlaps(World& world) {
    auto& cells = world.cellsMut();
    if (cells.size() < 2) return;
    const Tick now = world.currentTick();

    // O(N^2) pairwise sweep over same-owner cells. N is bounded by
    // max_cells_per_player * (1 + bot count), typically <800 worst-case, and
    // most cells are singletons so the inner skip-on-owner-mismatch keeps the
    // actual work tiny in practice.
    //
    // The push only applies while at least one cell of the pair is still in
    // the post-split recombine cooldown (recombine_at > now). Once both
    // cells' cooldowns expire, they touch and merge via processRecombine on
    // the same tick instead -- so this never interferes with the recombine
    // path.
    //
    // The push is symmetric (half the overlap each) which keeps positions
    // deterministic regardless of iteration order. Cells mid-animation
    // (hiding in BH, wormhole transit, BH entry/exit) are skipped so we
    // don't fight those position controllers.
    const size_t n = cells.size();
    for (size_t i = 0; i < n; ++i) {
        Cell& a = cells[i];
        if (a.mass <= 0.0f) continue;
        if (a.hiding_in != INVALID_ENTITY) continue;
        if (a.entry_anim_until > now)      continue;
        if (a.exit_anim_until  > now)      continue;

        for (size_t j = i + 1; j < n; ++j) {
            Cell& b = cells[j];
            if (b.owner != a.owner) continue;
            if (b.mass <= 0.0f) continue;
            if (b.hiding_in != INVALID_ENTITY) continue;
            if (b.entry_anim_until > now)      continue;
            if (b.exit_anim_until  > now)      continue;
            // Only push apart while at least one cell is still in the
            // recombine cooldown. After both expire, processRecombine will
            // merge them on touch.
            if (a.recombine_at <= now && b.recombine_at <= now) continue;

            const float ar = cellRadius(a.mass);
            const float br = cellRadius(b.mass);
            const float min_d = ar + br;
            float dx = b.pos.x - a.pos.x;
            float dy = b.pos.y - a.pos.y;
            float dsq = dx * dx + dy * dy;
            if (dsq >= min_d * min_d) continue; // not overlapping

            // Degenerate co-located case (rare; both cells at exactly the
            // same point). Pick a deterministic axis based on cell id parity
            // so we always pull them apart in a stable direction.
            if (dsq < 1e-4f) {
                dx = (a.id < b.id) ? 1.0f : -1.0f;
                dy = 0.0f;
                dsq = 1.0f;
            }

            const float d   = std::sqrt(dsq);
            const float overlap = min_d - d;
            const float half    = overlap * 0.5f + 0.5f; // +0.5 keeps a 1px
                                                          // gap so the next
                                                          // tick's check
                                                          // doesn't re-fire.
            const float ux = dx / d;
            const float uy = dy / d;
            a.pos.x -= ux * half;
            a.pos.y -= uy * half;
            b.pos.x += ux * half;
            b.pos.y += uy * half;
        }
    }
}

void respawnFood(World& world, const Tuning& t) {
    const int current = static_cast<int>(world.food().size());
    if (current >= t.food_target) return;

    int deficit  = t.food_target - current;
    int to_spawn = std::min(deficit, 8); // per-tick budget

    const float w = static_cast<float>(world.width());
    const float h = static_cast<float>(world.height());

    for (int i = 0; i < to_spawn; ++i) {
        for (int attempt = 0; attempt < 6; ++attempt) {
            Vec2 pos{world.rng().rangeFloat(0.0f, w),
                     world.rng().rangeFloat(0.0f, h)};
            bool too_close = false;
            for (const auto& c : world.cells()) {
                if (c.mass < 400.0f) continue;
                if (distance(c.pos, pos) < cellRadius(c.mass) + 24.0f) {
                    too_close = true;
                    break;
                }
            }
            if (!too_close) {
                world.spawnFood(pos, rollFoodMass(world.rng()),
                                Vec2{0.0f, 0.0f}, INVALID_PLAYER);
                break;
            }
        }
    }
}

void respawnViruses(World& world, const Tuning& t) {
    const int current = static_cast<int>(world.viruses().size());
    if (current >= t.virus_count) return;

    // One per tick at most -- keeps respawn from being instant. With 16k² world and a
    // deficit of e.g. 5 viruses, the field repopulates in <0.2s.
    const float w = static_cast<float>(world.width());
    const float h = static_cast<float>(world.height());
    constexpr float kEdgeMargin   = 300.0f;
    constexpr float kCellClearance = 250.0f;
    constexpr float kVirusClearance = 600.0f;

    for (int attempt = 0; attempt < 10; ++attempt) {
        Vec2 pos{
            world.rng().rangeFloat(kEdgeMargin, w - kEdgeMargin),
            world.rng().rangeFloat(kEdgeMargin, h - kEdgeMargin),
        };
        bool too_close = false;
        for (const auto& c : world.cells()) {
            if (distance(c.pos, pos) < cellRadius(c.mass) + kCellClearance) {
                too_close = true; break;
            }
        }
        if (!too_close) {
            for (const auto& v : world.viruses()) {
                if (distance(v.pos, pos) < kVirusClearance) {
                    too_close = true; break;
                }
            }
        }
        if (!too_close) {
            world.spawnVirus(pos, 200.0f);
            return;
        }
    }
}

// ---- Pickups ----------------------------------------------------------------

void processPickupCollection(World& world, const Tuning& t,
                             std::vector<GameEvent>& events) {
    auto& pickups = world.pickupsMut();
    if (pickups.empty()) return;

    auto& cells = world.cellsMut();
    const Tick now = world.currentTick();
    // Erase-collected pattern: stable_partition then resize. Pickup count is small
    // (< 20) so the all-pairs check against cells is trivially cheap.
    auto first_kept = std::stable_partition(pickups.begin(), pickups.end(),
        [&](const Pickup& p) -> bool {
            const float pr = pickupRadius();
            for (auto& c : cells) {
                if (c.mass <= 0.0f) continue;
                if (c.hiding_in != INVALID_ENTITY) continue; // hidden cells can't pick up
                const float cr = cellRadius(c.mass);
                if (distance(c.pos, p.pos) > cr + pr) continue;

                // Apply the corresponding effect to the consuming cell.
                Tick until = now;
                switch (p.kind) {
                    case PickupKind::Shield:
                        until = now + secondsToTicks(t.pickup_shield_duration_sec);
                        c.shield_until = std::max(c.shield_until, until);
                        break;
                    case PickupKind::Magnet:
                        until = now + secondsToTicks(t.pickup_magnet_duration_sec);
                        c.magnet_until = std::max(c.magnet_until, until);
                        break;
                    case PickupKind::Stealth:
                        until = now + secondsToTicks(t.pickup_stealth_duration_sec);
                        c.stealth_until = std::max(c.stealth_until, until);
                        break;
                    case PickupKind::None: break;
                }
                events.push_back(PickupCollectedEvent{c.id, c.owner, p.kind, p.pos});
                return false; // drop this pickup
            }
            return true; // keep
        });
    pickups.erase(first_kept, pickups.end());
}

void applyMagnetPulls(World& world, const Tuning& t) {
    const Tick now = world.currentTick();
    const auto& cells = world.cells();

    // Collect magneted cells first so we don't redo the predicate per food.
    struct Source { Vec2 pos; };
    std::vector<Source> sources;
    sources.reserve(4);
    for (const auto& c : cells) {
        // Hidden cells lose their magnet field while inside a black hole -- the
        // food up top shouldn't suddenly fly off to the centre of an unreachable
        // pocket dimension.
        if (c.hiding_in != INVALID_ENTITY) continue;
        if (c.magnet_until > now) sources.push_back({c.pos});
    }
    if (sources.empty()) return;

    const float radius    = t.pickup_magnet_radius;
    const float radius_sq = radius * radius;
    const float speed     = t.pickup_magnet_speed;

    auto& foods = world.foodMut();
    if (foods.empty()) return;

    // Instead of an O(F * S) sweep over all 3600 food entries, query the food grid for
    // each magnet source (radius ~kPickupMagnetRadius, so each query touches a handful
    // of buckets). We may still need to compare a food against multiple sources to pick
    // the closest, so track `(best_src_idx, best_dsq)` per food. -1 = not magneted this
    // tick.
    //
    // Determinism: foods are applied in vector-index order (same as the original), and
    // the closest-source choice is identical to the original sweep.
    static thread_local std::vector<int>   best_src_scratch;
    static thread_local std::vector<float> best_dsq_scratch;
    best_src_scratch.assign(foods.size(), -1);
    best_dsq_scratch.assign(foods.size(), 0.0f);

    std::vector<uint32_t> nearby;
    const auto& food_grid = world.foodsGrid();
    for (size_t si = 0; si < sources.size(); ++si) {
        const auto& s = sources[si];
        nearby.clear();
        food_grid.query(Vec2{s.pos.x - radius, s.pos.y - radius},
                        Vec2{s.pos.x + radius, s.pos.y + radius}, nearby);
        for (uint32_t fi : nearby) {
            // The grid may return duplicate indices (entity straddling buckets) -- the
            // best-tracker pattern is idempotent so duplicates are harmless.
            if (fi >= foods.size()) continue;
            const auto& f = foods[fi];
            float dx  = s.pos.x - f.pos.x;
            float dy  = s.pos.y - f.pos.y;
            float dsq = dx * dx + dy * dy;
            if (dsq >= radius_sq) continue;
            if (best_src_scratch[fi] < 0 || dsq < best_dsq_scratch[fi]) {
                best_src_scratch[fi] = static_cast<int>(si);
                best_dsq_scratch[fi] = dsq;
            }
        }
    }

    for (size_t fi = 0; fi < foods.size(); ++fi) {
        int sidx = best_src_scratch[fi];
        if (sidx < 0) continue;
        auto& f = foods[fi];
        const auto& s = sources[static_cast<size_t>(sidx)];
        float dx = s.pos.x - f.pos.x;
        float dy = s.pos.y - f.pos.y;
        float d  = std::sqrt(best_dsq_scratch[fi]);
        if (d < 1.0f) continue;
        // Strength tapers with distance so food at the edge of the radius drifts
        // slowly while food up close zips in.
        float ramp = 1.0f - d / radius; // 1 close, 0 at edge
        ramp = ramp * ramp + 0.25f;     // ease, plus a floor so far-away food still moves
        f.vel = Vec2{dx / d * speed * ramp, dy / d * speed * ramp};
    }
}

void respawnPickups(World& world, const Tuning& t) {
    const int current = static_cast<int>(world.pickups().size());
    if (current >= t.pickup_target) return;

    // One per tick at most so pickups don't all flood back into the world the moment
    // a player grabs several. Spreads spawns over ~0.5s deficit max.
    const float w = static_cast<float>(world.width());
    const float h = static_cast<float>(world.height());
    constexpr float kEdgeMargin       = 200.0f;
    constexpr float kCellClearance    = 200.0f;
    constexpr float kPickupClearance  = 600.0f;

    for (int attempt = 0; attempt < 8; ++attempt) {
        Vec2 pos{
            world.rng().rangeFloat(kEdgeMargin, w - kEdgeMargin),
            world.rng().rangeFloat(kEdgeMargin, h - kEdgeMargin),
        };
        bool too_close = false;
        for (const auto& c : world.cells()) {
            if (c.mass < 200.0f) continue;
            if (distance(c.pos, pos) < cellRadius(c.mass) + kCellClearance) {
                too_close = true; break;
            }
        }
        if (!too_close) {
            for (const auto& p : world.pickups()) {
                if (distance(p.pos, pos) < kPickupClearance) {
                    too_close = true; break;
                }
            }
        }
        if (!too_close) {
            // Roll the kind uniformly across the three powerups (rangeInt skips None).
            int r = world.rng().rangeInt(1, kPickupKindCount); // 1..3 inclusive
            PickupKind kind = static_cast<PickupKind>(r);
            world.spawnPickup(pos, kind);
            return;
        }
    }
}

// ---- Black holes ------------------------------------------------------------

void processBlackHoles(World& world, const Tuning& t, float dt) {
    auto& blackholes = world.blackholesMut();
    if (blackholes.empty()) return;

    auto& cells = world.cellsMut();
    const Tick now             = world.currentTick();
    const Tick anim_ticks_full = std::max<Tick>(1u,
                                    secondsToTicks(t.blackhole_transition_sec));
    const float anim_ticks_f   = static_cast<float>(anim_ticks_full);

    // Stamina rates per second; per tick rates are these * dt.
    const float drain_per_sec  = (t.blackhole_stamina_drain_sec  > 0.01f)
                                   ? 1.0f / t.blackhole_stamina_drain_sec  : 1.0f;
    const float refill_per_sec = (t.blackhole_stamina_refill_sec > 0.01f)
                                   ? 1.0f / t.blackhole_stamina_refill_sec : 1.0f;
    const float drain_per_dt   = drain_per_sec  * dt;
    const float refill_per_dt  = refill_per_sec * dt;

    for (auto& c : cells) {
        if (c.mass <= 0.0f) continue;

        // ---- State: ENTRY ANIMATION (sliding in, shrinking) ----
        // hiding_in is set immediately at trigger so AI/eating treat as hidden, but
        // position lerps from anim_origin to BH centre over `anim_ticks_full`.
        if (c.hiding_in != INVALID_ENTITY && c.entry_anim_until > now) {
            const BlackHole* bh = world.findBlackHole(c.hiding_in);
            if (!bh) { c.hiding_in = INVALID_ENTITY; c.entry_anim_until = 0; continue; }
            float progress = 1.0f - static_cast<float>(c.entry_anim_until - now) / anim_ticks_f;
            progress = std::clamp(progress, 0.0f, 1.0f);
            // Ease-in (slow start, fast finish toward the singularity).
            float ease = progress * progress;
            c.pos.x = c.anim_origin.x + (bh->pos.x - c.anim_origin.x) * ease;
            c.pos.y = c.anim_origin.y + (bh->pos.y - c.anim_origin.y) * ease;
            c.vel        = {0.0f, 0.0f};
            c.launch_vel = {0.0f, 0.0f};
            continue;
        }

        // ---- State: FULLY HIDDEN (post-entry-anim) ----
        if (c.hiding_in != INVALID_ENTITY) {
            const BlackHole* bh = world.findBlackHole(c.hiding_in);
            if (!bh) { c.hiding_in = INVALID_ENTITY; c.blackhole_stamina = 0.0f; continue; }

            // Drain stamina, pin to centre.
            c.blackhole_stamina = std::max(0.0f, c.blackhole_stamina - drain_per_dt);
            c.pos        = bh->pos;
            c.vel        = {0.0f, 0.0f};
            c.launch_vel = {0.0f, 0.0f};

            // Exit conditions:
            //  - Stamina ran out: force eject.
            //  - Player intent: move target outside the disc -> eject in target dir.
            //  - Bots: stamina-only (no mouse cursor).
            const bool is_player = (c.owner < ai::kFirstBotPlayerId);
            bool       want_exit = (c.blackhole_stamina <= 0.0f);
            Vec2       exit_dir{1.0f, 0.0f};
            if (is_player) {
                const float dx = c.target.x - bh->pos.x;
                const float dy = c.target.y - bh->pos.y;
                const float dsq = dx * dx + dy * dy;
                if (dsq > bh->radius * bh->radius) {
                    want_exit = true;
                    float d = std::sqrt(dsq);
                    if (d > 0.5f) exit_dir = Vec2{dx / d, dy / d};
                }
            }
            if (want_exit) {
                if (!is_player) {
                    float dx = c.target.x - bh->pos.x;
                    float dy = c.target.y - bh->pos.y;
                    float dsq = dx * dx + dy * dy;
                    if (dsq > 1.0f) {
                        float d = std::sqrt(dsq);
                        exit_dir = Vec2{dx / d, dy / d};
                    } else {
                        exit_dir = Vec2{1.0f, 0.0f};
                    }
                }
                const float exit_dist = bh->pull_radius + 8.0f;
                c.anim_origin       = bh->pos;
                c.anim_target       = Vec2{bh->pos.x + exit_dir.x * exit_dist,
                                            bh->pos.y + exit_dir.y * exit_dist};
                c.exit_anim_until   = now + anim_ticks_full;
                c.hiding_in         = INVALID_ENTITY; // back in the open world
                c.entry_anim_until  = 0;
                // Position stays at bh->pos for now; exit-anim block will lerp it.
            }
            continue;
        }

        // ---- State: EXIT ANIMATION (sliding out, growing back) ----
        // hiding_in is cleared (open world for AI/eating), but we lerp position and
        // grant prey-immunity (handled in processEating) so the cell isn't snipped
        // mid-eject.
        if (c.exit_anim_until > now) {
            float progress = 1.0f - static_cast<float>(c.exit_anim_until - now) / anim_ticks_f;
            progress = std::clamp(progress, 0.0f, 1.0f);
            // Ease-out (fast launch, slowing to settle).
            float ease = 1.0f - (1.0f - progress) * (1.0f - progress);
            c.pos.x = c.anim_origin.x + (c.anim_target.x - c.anim_origin.x) * ease;
            c.pos.y = c.anim_origin.y + (c.anim_target.y - c.anim_origin.y) * ease;
            c.vel        = {0.0f, 0.0f};
            c.launch_vel = {0.0f, 0.0f};
            if (c.blackhole_stamina < 1.0f) {
                c.blackhole_stamina = std::min(1.0f, c.blackhole_stamina + refill_per_dt);
            }
            continue;
        }

        // ---- State: OPEN WORLD ----
        // Refill stamina, then check for pull / entry.
        if (c.blackhole_stamina < 1.0f) {
            c.blackhole_stamina = std::min(1.0f, c.blackhole_stamina + refill_per_dt);
        }
        if (c.blackhole_stamina < kBlackHoleEntryFloor) continue;

        // Nearest black hole within pull range.
        const BlackHole* picked = nullptr;
        float            picked_dsq = 0.0f;
        for (const auto& b : blackholes) {
            float dx  = b.pos.x - c.pos.x;
            float dy  = b.pos.y - c.pos.y;
            float dsq = dx * dx + dy * dy;
            const float reach = b.pull_radius + cellRadius(c.mass);
            if (dsq > reach * reach) continue;
            if (!picked || dsq < picked_dsq) {
                picked     = &b;
                picked_dsq = dsq;
            }
        }
        if (!picked) continue;

        // Entry trigger: cell centre crossed the inner radius. Start the smooth
        // entry animation -- hiding_in set immediately, anim runs for transition_sec.
        if (picked_dsq <= picked->radius * picked->radius) {
            c.hiding_in         = picked->id;
            c.blackhole_stamina = std::min(1.0f, std::max(c.blackhole_stamina, 0.50f));
            c.anim_origin       = c.pos;
            c.entry_anim_until  = now + anim_ticks_full;
            c.exit_anim_until   = 0;
            c.vel               = {0.0f, 0.0f};
            c.launch_vel        = {0.0f, 0.0f};
            continue;
        }

        // Pull acceleration: gentle at the outer ring, ramps via quadratic ease
        // to peak near the inner edge. Never zero (so you can't camp the boundary).
        float d = std::sqrt(picked_dsq);
        if (d < 1.0f) d = 1.0f;
        float t01 = 1.0f - (d - picked->radius) /
                           (picked->pull_radius - picked->radius); // 0 at edge, 1 at inner
        t01 = std::clamp(t01, 0.0f, 1.0f);
        const float ease = t01 * t01;
        float pull = t.blackhole_pull_min_speed
                   + (t.blackhole_pull_speed - t.blackhole_pull_min_speed) * ease;
        float dirx = (picked->pos.x - c.pos.x) / d;
        float diry = (picked->pos.y - c.pos.y) / d;
        c.vel.x += dirx * pull * dt;
        c.vel.y += diry * pull * dt;
    }
}

void processComets(World& world, const Tuning& t, float dt,
                   Tick& next_spawn_tick_inout,
                   std::vector<GameEvent>& events) {
    auto&       comets = world.cometsMut();
    const Tick  now    = world.currentTick();

    // ---- 1. Step active comets forward. Telegraphed comets sit still. ----
    for (auto& c : comets) {
        if (c.start_at > now) continue;
        c.pos.x += c.vel.x * dt;
        c.pos.y += c.vel.y * dt;
    }

    // ---- 2. Telegraph -> Active transitions emit a CometEvent ----
    for (const auto& c : comets) {
        if (c.start_at == now) {
            Vec2  dir   = c.vel;
            float speed = length(dir);
            if (speed > 1e-3f) dir = dir * (1.0f / speed);
            else               dir = Vec2{1.0f, 0.0f};
            events.push_back(CometEvent{c.id, CometEvent::Active, c.pos, dir});
        }
    }

    // ---- 3. Kill cells inside any *active* comet's radius ----
    auto&             cells     = world.cellsMut();
    std::vector<char> cell_dead(cells.size(), 0);
    bool              any_dead  = false;
    for (const auto& cm : comets) {
        if (cm.start_at > now) continue;
        const float r_sq = cm.radius * cm.radius;
        for (size_t i = 0; i < cells.size(); ++i) {
            auto& c = cells[i];
            if (cell_dead[i])                    continue;
            if (c.mass <= 0.0f)                  continue;
            if (c.god)                           continue;
            if (c.hiding_in != INVALID_ENTITY)   continue; // BH-hidden cells survive
            if (c.shield_until > now)            continue; // shield pickup grants immunity
            float dx = c.pos.x - cm.pos.x;
            float dy = c.pos.y - cm.pos.y;
            if (dx * dx + dy * dy <= r_sq) {
                cell_dead[i] = 1;
                any_dead     = true;
                events.push_back(DeathEvent{
                    /*player=*/c.owner,
                    /*by=*/cm.id,                // points at the comet, not a cell
                    /*predator_player=*/INVALID_PLAYER,
                    /*prey_personality=*/c.personality_tag,
                    /*predator_personality=*/0});
            }
        }
    }
    if (any_dead) {
        // Same compact-in-place pattern as processEating, kept local to avoid coupling
        // this function to that file's templated helper.
        size_t w = 0;
        for (size_t r = 0; r < cells.size(); ++r) {
            if (!cell_dead[r]) {
                if (w != r) cells[w] = std::move(cells[r]);
                ++w;
            }
        }
        cells.resize(w);
    }

    // ---- 4. Despawn comets that have left the world ----
    const float margin = t.comet_radius + 200.0f;
    const float wx0    = -margin;
    const float wy0    = -margin;
    const float wx1    = static_cast<float>(world.width())  + margin;
    const float wy1    = static_cast<float>(world.height()) + margin;
    comets.erase(std::remove_if(comets.begin(), comets.end(),
        [&](const Comet& c) {
            if (c.start_at > now) return false;
            const bool out = (c.pos.x < wx0 || c.pos.x > wx1
                           || c.pos.y < wy0 || c.pos.y > wy1);
            if (out) {
                events.push_back(CometEvent{c.id, CometEvent::Despawn, c.pos, Vec2{0, 0}});
            }
            return out;
        }), comets.end());

    // ---- 5. Spawn a fresh comet when the cadence timer fires ----
    if (now >= next_spawn_tick_inout) {
        Rng&  rng     = world.rng();
        const float W = static_cast<float>(world.width());
        const float H = static_cast<float>(world.height());

        // Pick an entry edge, then an exit point on the opposite edge. This guarantees
        // the comet sweeps across the long axis of the world (not a corner-cut clip).
        const int side = rng.rangeInt(0, 3);
        Vec2      entry{}, exit{};
        const float r = t.comet_radius;
        switch (side) {
            case 0: entry = {rng.rangeFloat(0.0f, W), -r}; break;
            case 1: entry = {W + r, rng.rangeFloat(0.0f, H)}; break;
            case 2: entry = {rng.rangeFloat(0.0f, W), H + r}; break;
            default:entry = {-r, rng.rangeFloat(0.0f, H)}; break;
        }
        const int opp = (side + 2) & 3;
        switch (opp) {
            case 0: exit = {rng.rangeFloat(0.0f, W), -r}; break;
            case 1: exit = {W + r, rng.rangeFloat(0.0f, H)}; break;
            case 2: exit = {rng.rangeFloat(0.0f, W), H + r}; break;
            default:exit = {-r, rng.rangeFloat(0.0f, H)}; break;
        }

        Vec2  dir = exit - entry;
        float L   = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (L < 1.0f) { dir = Vec2{1.0f, 0.0f}; L = 1.0f; }
        const Vec2 unit_dir = dir * (1.0f / L);
        const Vec2 vel      = unit_dir * t.comet_speed;

        const Tick telegraph_ticks = secondsToTicks(t.comet_telegraph_sec);
        EntityId   id              = world.spawnComet(entry, vel, t.comet_radius, now,
                                                      now + telegraph_ticks, entry, exit);
        events.push_back(CometEvent{id, CometEvent::Telegraph, entry, unit_dir});

        // Reschedule. Jitter is symmetric around the mean interval.
        float interval = t.comet_event_interval_sec;
        if (t.comet_interval_jitter > 0.0f) {
            float j = rng.rangeFloat(-t.comet_interval_jitter, t.comet_interval_jitter);
            interval *= (1.0f + j);
        }
        next_spawn_tick_inout = now + secondsToTicks(std::max(5.0f, interval));
    }
}

// ---------------------------------------------------------------------------
// Comet shower world event
// ---------------------------------------------------------------------------

void spawnCometShowerNow(World& world, const Tuning& t,
                         std::vector<GameEvent>& events) {
    Rng&        rng = world.rng();
    const Tick  now = world.currentTick();
    const float W   = static_cast<float>(world.width());
    const float H   = static_cast<float>(world.height());

    // Pick the formation's entry edge + exit point, same world-spanning logic
    // as the single-comet path. The main comet flies along this axis; all
    // satellites share the same velocity direction (with scatter in
    // perpendicular + along directions only).
    const int side = rng.rangeInt(0, 3);
    Vec2      entry{}, exit{};
    const float r_main = t.comet_shower_main_radius;
    switch (side) {
        case 0: entry = {rng.rangeFloat(0.0f, W), -r_main}; break;
        case 1: entry = {W + r_main, rng.rangeFloat(0.0f, H)}; break;
        case 2: entry = {rng.rangeFloat(0.0f, W), H + r_main}; break;
        default:entry = {-r_main, rng.rangeFloat(0.0f, H)}; break;
    }
    const int opp = (side + 2) & 3;
    switch (opp) {
        case 0: exit = {rng.rangeFloat(0.0f, W), -r_main}; break;
        case 1: exit = {W + r_main, rng.rangeFloat(0.0f, H)}; break;
        case 2: exit = {rng.rangeFloat(0.0f, W), H + r_main}; break;
        default:exit = {-r_main, rng.rangeFloat(0.0f, H)}; break;
    }

    Vec2  dir = exit - entry;
    float L   = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (L < 1.0f) { dir = Vec2{1.0f, 0.0f}; L = 1.0f; }
    const Vec2 unit_dir = dir * (1.0f / L);
    // Perpendicular unit vector for satellite scatter (rotated 90° CCW).
    const Vec2 perp     = Vec2{-unit_dir.y, unit_dir.x};
    const Vec2 vel      = unit_dir * t.comet_speed;

    const Tick telegraph_ticks = secondsToTicks(t.comet_telegraph_sec);

    // ---- Main comet (Orange) ----
    EntityId main_id = world.spawnComet(entry, vel, r_main,
                                        now, now + telegraph_ticks,
                                        entry, exit,
                                        CometVariant::Orange);
    events.push_back(CometEvent{main_id, CometEvent::Telegraph, entry, unit_dir});

    // ---- Satellites (Red / Blue) ----
    // Roll count in [min, max]; clamp to [0, 9] just in case the tuning has
    // pathological values. With min=4, max=9 the spawn is 5..10 comets total.
    int sat_min = std::max(0, t.comet_shower_satellite_min);
    int sat_max = std::max(sat_min, t.comet_shower_satellite_max);
    sat_max     = std::min(sat_max, 9); // hard cap so we don't choke the renderer
    const int sat_count = rng.rangeInt(sat_min, sat_max);

    for (int i = 0; i < sat_count; ++i) {
        // Perpendicular offset: symmetric around the main's axis. Cube-rooted
        // so the distribution is roughly uniform across the band (without
        // cube-root the centre would cluster). +/- spread_perp.
        float perp_u  = rng.rangeFloat(-1.0f, 1.0f);
        float perp_o  = perp_u * t.comet_shower_spread_perp;
        // Longitudinal offset: bias to NEGATIVE side (behind the main along
        // entry->exit) so satellites trail the main rather than lead it.
        // Range [-spread_along, +0.4 * spread_along].
        float along_o = rng.rangeFloat(-1.0f, 0.4f) * t.comet_shower_spread_along;

        // Satellite entry position relative to the formation's entry point.
        Vec2 sat_entry{
            entry.x + perp.x * perp_o + unit_dir.x * along_o,
            entry.y + perp.y * perp_o + unit_dir.y * along_o,
        };
        // Compute a matching exit point so the renderer's telegraph line
        // shows where this satellite will end up (parallel to the main's
        // path, offset by perp_o).
        Vec2 sat_exit{
            exit.x  + perp.x * perp_o,
            exit.y  + perp.y * perp_o,
        };

        float radius = rng.rangeFloat(t.comet_shower_satellite_min_radius,
                                      t.comet_shower_satellite_max_radius);

        // Alternate Red / Blue with an RNG roll. Even split feels balanced
        // visually; both palettes are equally "non-orange" so the player
        // doesn't think one color is more dangerous than the other.
        CometVariant variant = (rng.rangeInt(0, 1) == 0)
                                   ? CometVariant::Red
                                   : CometVariant::Blue;

        EntityId sid = world.spawnComet(sat_entry, vel, radius,
                                        now, now + telegraph_ticks,
                                        sat_entry, sat_exit,
                                        variant);
        events.push_back(CometEvent{sid, CometEvent::Telegraph, sat_entry, unit_dir});
    }
}

void processCometShowers(World& world, const Tuning& t,
                         Tick& next_spawn_tick_inout,
                         std::vector<GameEvent>& events) {
    const Tick now = world.currentTick();
    if (now < next_spawn_tick_inout) return;

    spawnCometShowerNow(world, t, events);

    // Reschedule. Re-use the single-comet jitter so the shower cadence has
    // the same "feels variable but bounded" character. Clamped to 30s
    // minimum so a misconfigured tuning can't spawn showers every tick.
    Rng& rng     = world.rng();
    float interval = t.comet_shower_event_interval_sec;
    if (t.comet_interval_jitter > 0.0f) {
        float j = rng.rangeFloat(-t.comet_interval_jitter, t.comet_interval_jitter);
        interval *= (1.0f + j);
    }
    next_spawn_tick_inout = now + secondsToTicks(std::max(30.0f, interval));
}

// ---------------------------------------------------------------------------
// Tidal current bands
// ---------------------------------------------------------------------------
//
// Horizontal flow bands spanning the full world width. A cell is "in" a band
// if its y-coordinate falls inside [band.pos.y - half_height, band.pos.y +
// half_height]. Inside, we DRIFT the cell along `band.dir` (always ±x for
// the standard bands) by `strength * dt` each tick. The drift falls off
// toward the band edges so the transition in / out is smooth, and scales
// inversely with sqrt(mass) so small cells are swept while mega-cells barely
// feel it.
//
// We translate `c.pos` directly (NOT `c.vel`) because `stepCells` overwrites
// `c.vel` every tick with `seek_vel + launch_vel` -- writing to vel here
// would be silently discarded. By contrast, `c.pos` is preserved across the
// step (stepCells only ADDS to it via `pos += total_vel * dt`), so a pre-
// step position translation reads as the band physically carrying the cell.
//
// The player's seek still works on top: aim the cursor north and the cell
// moves north while drifting east, exactly like a river current. To stay
// stationary inside a band the player must aim "upstream" -- which is what
// makes the bands tactical terrain instead of background flavor.
//
// Cells hiding inside a black hole are skipped (they're pinned).
void applyTidalCurrents(World& world, const Tuning& t, float dt) {
    if (world.currents().empty()) return;
    const auto& currents = world.currents();
    const Tick  now = world.currentTick();
    for (auto& c : world.cellsMut()) {
        if (c.hiding_in != INVALID_ENTITY) continue;
        if (c.entry_anim_until > now)      continue;
        if (c.exit_anim_until  > now)      continue;
        // Mass-aware attenuation: a starter cell takes full strength, big
        // cells still feel a meaningful push. We use the 4th root
        // (pow(mass/start_mass, 0.25)) instead of sqrt because sqrt drops
        // off too fast in the medium-mass range -- a 400-mass cell only got
        // 50% of the push with sqrt, which felt like the current "gave up"
        // halfway through normal play. 4th root keeps push roughly
        // proportional to the cell's own natural speed (which itself scales
        // as ~mass^-0.225 via cellSpeed's speed_falloff exponent), so a
        // cell always drifts noticeably faster than it can swim regardless
        // of size. Mega-cells still get ~32% push (down from ~10%) which
        // is enough to feel it without being yeeted.
        const float mass_ratio  = std::max(1.0f, c.mass / t.start_mass);
        const float mass_factor = std::sqrt(std::sqrt(mass_ratio)); // 4th root
        for (const auto& band : currents) {
            const float h = band.half_height;
            if (h <= 0.0f) continue;
            const float ady = std::abs(c.pos.y - band.pos.y);
            if (ady > h) continue;
            // Smoothstep falloff: full strength on the centre line, 0 at
            // the rim. Cells entering / leaving the band don't snap into /
            // out of the flow.
            const float u       = 1.0f - (ady / h);    // 0..1, 1 at centre
            const float falloff = u * u * (3.0f - 2.0f * u);
            const float drift   = (band.strength * falloff / mass_factor) * dt;
            c.pos.x += band.dir.x * drift;
            c.pos.y += band.dir.y * drift;
            // Bands don't overlap by design (count=2 places them at 1/3
            // and 2/3 world height with half_height < that gap), so we
            // stop at the first match for performance.
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Wormholes
// ---------------------------------------------------------------------------
//
// Two-phase transit: SHRINK at source, then GROW at destination. Visually
// the cell collapses into the source wormhole, briefly disappears, then
// emerges and grows out of the partner wormhole.
//
// Phase 1 (SHRINK at source, duration = phase_ticks):
//   * cell.pos held at source (stepCells skips on entry_anim_until > now).
//   * cell.entry_anim_until = now + phase_ticks.
//   * cell.anim_origin = partner_pos (used to seed phase 2 lerp).
//   * cell.anim_target = final_emerge_pos (used to seed phase 2 lerp).
//   * cell.wormhole_transit_phase = 1.
//   * buildSnapshot drives blackhole_visual_scale 1 -> 0 via the entry-anim
//     code path -- the cell visibly shrinks at the source wormhole.
//
// Phase 1 -> Phase 2 transition (when entry_anim_until expires):
//   * cell.pos snapped from source to partner centre. At this exact tick the
//     visual_scale is 0 (entry-anim just ended, exit-anim just begun) so the
//     teleport is invisible -- prev_snap shows source/tiny, curr shows
//     partner/scale=0; client lerps pos but scale=0 hides it.
//   * cell.exit_anim_until = now + phase_ticks.
//   * cell.wormhole_transit_phase = 2.
//
// Phase 2 (GROW at destination, duration = phase_ticks):
//   * cell.pos lerped from partner centre -> final_emerge_pos by
//     processBlackHoles' shared exit-anim block (line 686).
//   * blackhole_visual_scale grows 0 -> 1 via buildSnapshot's exit-anim path.
//   * stepCells skips on exit_anim_until > now so the player's seek doesn't
//     fight the emergence lerp.
//   * processEating already grants prey-immunity to mid-exit-anim cells.
//
// Phase 2 -> idle (when exit_anim_until expires):
//   * wormhole_transit_phase = 0. Normal play resumes.
//
// Total transit duration = 2 * blackhole_transition_sec (~0.7s by default).
// Matched to that constant because buildSnapshot's scale ease references it
// as the anim_ticks reference -- with phase_ticks = blackhole_transition_sec
// the scale exactly spans 1 -> 0 (entry) and 0 -> 1 (exit) over each phase.
//
// Hiding cells (in a BH) and cells already mid-BH-animation are skipped so
// we don't stomp on existing animations.
void processWormholes(World& world, const Tuning& t) {
    if (world.wormholes().empty()) return;
    const Tick  now             = world.currentTick();
    const Tick  cooldown_ticks  = std::max<Tick>(1, secondsToTicks(t.wormhole_cooldown_sec));
    // Per-phase ticks. buildSnapshot's scale ease uses
    // tuning.blackhole_transition_sec as the reference duration, so matching
    // phase_ticks to that gives a clean 1->0 (shrink) and 0->1 (grow) curve.
    const Tick  phase_ticks     = std::max<Tick>(1, secondsToTicks(t.blackhole_transition_sec));
    const auto& wormholes       = world.wormholes();

    for (auto& cell : world.cellsMut()) {
        if (cell.mass <= 0.0f) continue;

        // ---- Phase 2 -> idle ----
        if (cell.wormhole_transit_phase == 2 && cell.exit_anim_until <= now) {
            cell.wormhole_transit_phase = 0;
        }

        // ---- Phase 1 -> Phase 2 transition ----
        // Cell finished shrinking at the source. Snap pos to partner centre
        // and hand off to the exit-anim pipeline for the grow-out.
        if (cell.wormhole_transit_phase == 1 && cell.entry_anim_until <= now) {
            // anim_origin holds the partner centre (set at phase 1 start);
            // anim_target holds the final emerge position.
            const Vec2 partner_pos = cell.anim_origin;
            const Vec2 final_pos   = cell.anim_target;

            cell.pos                = partner_pos;
            cell.entry_anim_until   = 0;
            cell.exit_anim_until    = now + phase_ticks;
            cell.target             = final_pos;
            cell.vel                = {0.0f, 0.0f};
            cell.launch_vel         = {0.0f, 0.0f};
            cell.wormhole_transit_phase = 2;
            // Extend invuln through the grow phase.
            cell.invuln_until = std::max(cell.invuln_until, now + phase_ticks);
            // anim_origin/anim_target stay as-is so processBlackHoles'
            // exit-anim block lerps pos from partner -> final_pos.
        }

        // ---- New entry detection ----
        // Only consider cells that aren't already transiting, aren't hiding
        // in a BH, aren't mid-BH-animation, and have served their teleport
        // cooldown.
        if (cell.wormhole_transit_phase != 0) continue;
        if (cell.hiding_in != INVALID_ENTITY) continue;
        if (cell.entry_anim_until > now || cell.exit_anim_until > now) continue;
        if (cell.wormhole_cooldown_until > now) continue;

        for (const auto& w : wormholes) {
            const Vec2 d = cell.pos - w.pos;
            if (lengthSq(d) > w.radius * w.radius) continue;
            // Found a wormhole this cell is inside. Look up its partner.
            const Wormhole* partner = world.findWormhole(w.pair_id);
            if (partner == nullptr) break; // pair missing (shouldn't happen)

            // Compute the final emerge position (partner centre + outward
            // kick along the cell's incoming velocity direction, so a
            // player travelling east emerges heading east). Stored on
            // anim_target now; consumed at the phase 1 -> 2 transition.
            Vec2 final_pos = partner->pos;
            const float vlen = length(cell.vel);
            Vec2 emerge_dir{1.0f, 0.0f};
            if (vlen > 1.0f) {
                emerge_dir = cell.vel * (1.0f / vlen);
            } else {
                // No incoming velocity -- pick a deterministic direction
                // from the cell's cursor target relative to the partner.
                const Vec2 to_target = cell.target - partner->pos;
                const float tlen = length(to_target);
                if (tlen > 1.0f) {
                    emerge_dir = to_target * (1.0f / tlen);
                }
            }
            final_pos.x += emerge_dir.x * (partner->radius * 1.6f);
            final_pos.y += emerge_dir.y * (partner->radius * 1.6f);

            // Phase 1 setup: pin cell at source, start shrink.
            // cell.pos stays at its current value (inside the source's
            // radius). entry_anim_until ticks down for phase_ticks ticks
            // while buildSnapshot drives the visual scale 1 -> 0.
            cell.entry_anim_until        = now + phase_ticks;
            cell.exit_anim_until         = 0;
            cell.anim_origin             = partner->pos;  // for phase 2 lerp start
            cell.anim_target             = final_pos;     // for phase 2 lerp end
            cell.vel                     = {0.0f, 0.0f};
            cell.launch_vel              = {0.0f, 0.0f};
            cell.target                  = cell.pos;       // no drift during shrink
            cell.wormhole_transit_phase  = 1;
            // Invuln spans both phases + a small buffer.
            cell.invuln_until            = std::max(cell.invuln_until,
                                                    now + phase_ticks * 2 + 2);
            cell.wormhole_cooldown_until = now + cooldown_ticks;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Geysers
// ---------------------------------------------------------------------------
//
// State machine per geyser:
//   Idle      -- wait for `next_event_tick`. When reached, advance to
//                Telegraph and set next_event_tick = telegraph_end_tick.
//   Telegraph -- visual warning ring grows. When `next_event_tick` is
//                reached, advance to Erupt and spawn the food burst.
//   Erupt     -- held for exactly one tick (so clients see the flash in at
//                least one snapshot frame), then back to Idle with a fresh
//                next_event_tick = now + jittered interval.
//
// Food is spawned with outward velocity so the burst spreads naturally;
// stepFood's velocity decay does the rest. Mass uses the same rollFoodMass
// as ambient food, weighted slightly toward rare drops so geysers feel
// rewarding to contest.
void processGeysers(World& world, const Tuning& t) {
    if (world.geysers().empty()) return;
    const Tick now              = world.currentTick();
    const Tick telegraph_ticks  = std::max<Tick>(1,
        secondsToTicks(t.geyser_telegraph_sec));

    for (auto& g : world.geysersMut()) {
        switch (g.state) {
            case GeyserState::Idle: {
                if (now >= g.next_event_tick) {
                    g.state           = GeyserState::Telegraph;
                    g.next_event_tick = now + telegraph_ticks;
                }
                break;
            }
            case GeyserState::Telegraph: {
                if (now >= g.next_event_tick) {
                    g.state = GeyserState::Erupt;
                    g.last_erupted_at = now;
                    // Spawn the food burst. Pick a deterministic count from
                    // the world RNG, then drop pellets in a sunburst pattern
                    // with outward velocity.
                    const int n = world.rng().rangeInt(
                        t.geyser_food_count_min,
                        t.geyser_food_count_max + 1);
                    for (int i = 0; i < n; ++i) {
                        const float ang = (static_cast<float>(i) / static_cast<float>(n))
                                         * 6.28318530718f
                                         + world.rng().rangeFloat(-0.18f, 0.18f);
                        const float spread = world.rng().rangeFloat(0.0f,
                                                                    t.geyser_food_spread);
                        const Vec2 dir{std::cos(ang), std::sin(ang)};
                        const Vec2 pos{
                            g.pos.x + dir.x * spread,
                            g.pos.y + dir.y * spread,
                        };
                        const Vec2 vel{
                            dir.x * t.geyser_food_eject_speed,
                            dir.y * t.geyser_food_eject_speed,
                        };
                        // Bias the mass roll slightly toward rares so a
                        // geyser eruption is meaningfully different from
                        // ambient food. We use rollFoodMass and re-roll if
                        // the first roll is common; this keeps the rng
                        // determinism intact while skewing the distribution.
                        float mass = rollFoodMass(world.rng());
                        if (mass < 2.0f) {
                            // ~50% chance to re-roll commons into something better.
                            if (world.rng().nextFloat() < 0.5f) {
                                mass = rollFoodMass(world.rng());
                            }
                        }
                        world.spawnFood(pos, mass, vel, INVALID_PLAYER);
                    }
                }
                break;
            }
            case GeyserState::Erupt: {
                // Hold for exactly one tick so the visual lands at least one
                // frame on every client, then reschedule.
                if (now > g.last_erupted_at) {
                    g.state = GeyserState::Idle;
                    const float jitter = std::clamp(t.geyser_interval_jitter,
                                                    0.0f, 0.9f);
                    const float lo = t.geyser_interval_sec * (1.0f - jitter);
                    const float hi = t.geyser_interval_sec * (1.0f + jitter);
                    const float sec = world.rng().rangeFloat(lo, hi);
                    g.next_event_tick = now + secondsToTicks(sec);
                }
                break;
            }
        }
    }
}

float rollFoodMass(Rng& rng) {
    float r = rng.nextFloat();
    if (r < 0.005f) return 36.0f; // 0.5% legendary (purple-red, strong halo, 3x gold)
    if (r < 0.025f) return 12.0f; //   2% epic     (gold, halo)
    if (r < 0.105f) return  6.0f; //   8% rare     (cyan, halo)
    if (r < 0.305f) return  3.0f; //  20% uncommon (lime)
    return 1.0f;                  // ~70% common   (green)
}

void doSplit(World& world, PlayerId player, const Tuning& t,
             std::vector<GameEvent>& events) {
    auto& cells = world.cellsMut();
    int existing = world.playerCellCount(player);

    const Tick recombine_at = world.currentTick() + secondsToTicks(t.recombine_delay_sec);
    const size_t initial = cells.size();

    for (size_t i = 0; i < initial; ++i) {
        if (cells[i].owner != player) continue;
        if (cells[i].mass < t.min_mass_to_split) continue;
        if (existing >= t.max_cells_per_player) break;

        const Vec2     c_pos    = cells[i].pos;
        const Vec2     c_target = cells[i].target;
        const float    new_mass = cells[i].mass * 0.5f;
        const EntityId p_id     = cells[i].id;

        Vec2 dir = normalize(c_target - c_pos);
        if (lengthSq(dir) < 0.5f) dir = {1.0f, 0.0f};

        cells[i].mass         = new_mass;
        cells[i].recombine_at = recombine_at;

        // Offset the child along `dir` so its edge just touches the parent's
        // edge -- both halves have equal mass now, so the centre-to-centre
        // distance is exactly 2 * new_radius (+ a tiny gap to keep the
        // spatial-grid collision pass from flickering). Previously the child
        // spawned at c_pos and visually overlapped the parent for the first
        // ~0.5s until launch_vel pushed them apart, which read as "one weird
        // small cell" until recombine.
        const float    new_radius = cellRadius(new_mass);
        const Vec2     child_pos  = c_pos + dir * (2.0f * new_radius + 1.0f);

        const uint8_t parent_tag = cells[i].personality_tag;
        EntityId child_id = world.spawnCell(player, child_pos, new_mass);
        if (auto* nc = world.findCell(child_id)) {
            nc->target           = c_target;
            nc->launch_vel       = dir * t.launch_velocity;
            nc->recombine_at     = recombine_at;
            nc->personality_tag  = parent_tag; // child inherits the bot type
        }
        events.push_back(SplitEvent{player, p_id, child_id});
        ++existing;
    }
}

void doEject(World& world, PlayerId player, const Tuning& t) {
    for (auto& c : world.cellsMut()) {
        if (c.owner != player) continue;
        if (c.mass < t.min_mass_to_eject) continue;
        if (c.mass - t.eject_mass < 50.0f) continue; // don't reduce to nothing

        Vec2 dir = normalize(c.target - c.pos);
        if (lengthSq(dir) < 0.5f) continue;

        c.mass -= t.eject_mass;
        Vec2 spawn_pos = c.pos + dir * (cellRadius(c.mass) + foodRadius(t.eject_mass) + 2.0f);
        world.spawnFood(spawn_pos, t.eject_mass, dir * t.eject_velocity, player);
    }
}

void doDash(World& world, PlayerId player, const Tuning& t) {
    const Tick now          = world.currentTick();
    const Tick dash_end     = now + secondsToTicks(t.dash_duration_sec);
    const Tick invuln_end   = now + secondsToTicks(t.invuln_frames_sec);
    const Tick cooldown_end = now + secondsToTicks(t.cooldown_sec);

    for (auto& c : world.cellsMut()) {
        if (c.owner != player) continue;
        if (c.dash_cooldown_until > now) continue;
        c.mass               *= (1.0f - t.cost_percent);
        c.dash_until          = dash_end;
        c.invuln_until        = invuln_end;
        c.dash_cooldown_until = cooldown_end;
    }
}

void doBlast(World& world, PlayerId player, const Tuning& t,
             std::vector<GameEvent>& events) {
    auto&      cells = world.cellsMut();
    const Tick now   = world.currentTick();

    // Source = the player's LARGEST cell. Casting from one source (instead of every
    // cell of the player) keeps multi-cell splits from cheesing the ability, and
    // makes recombining a meaningful prep step before a blast.
    Cell* source     = nullptr;
    float best_mass  = 0.0f;
    for (auto& c : cells) {
        if (c.owner != player) continue;
        if (c.hiding_in != INVALID_ENTITY) continue; // hiding inside a black hole
        if (c.mass > best_mass) {
            best_mass = c.mass;
            source    = &c;
        }
    }
    if (!source) return;
    if (source->mass < t.blast_min_mass) return;
    if (source->blast_cooldown_until > now) return;

    // Spend mass, start cooldown. Capture the SOURCE radius BEFORE we deduct mass --
    // the blast's reach is anchored to the cell that initiated the cast, not the
    // (smaller) cell that exists right after the mass spend.
    const float source_r_at_cast = cellRadius(source->mass);
    source->mass               *= (1.0f - t.blast_cost_percent);
    source->blast_cooldown_until = now + secondsToTicks(t.blast_cooldown_sec);
    const Vec2  origin    = source->pos;
    // Scale the blast reach with the source's size. t.blast_radius is the reach at
    // the minimum cast mass; a bigger caster gets a proportionally bigger shockwave
    // so enemies just outside its body still feel the push. Previously the radius
    // was fixed, so a 4000-mass cell's blast barely cleared its own perimeter.
    const float min_r       = std::max(1.0f, cellRadius(t.blast_min_mass));
    const float size_scale  = std::max(1.0f, source_r_at_cast / min_r);
    const float radius      = t.blast_radius * size_scale;
    const float radius_sq   = radius * radius;
    // Push strength also scales with size, but more conservatively (sqrt rather than
    // linear) so a giant cell doesn't fling enemies across the whole map.
    const float push_peak   = t.blast_push_speed * std::sqrt(size_scale);
    const float food_scale = t.blast_food_push_scale;

    events.push_back(BlastEvent{source->id, player, origin, radius});

    // Push enemy cells (not own; not hiding / exiting cells). stepCells overwrites
    // c.vel each tick with seek_vel + launch_vel, so we apply the push to launch_vel
    // -- which stepCells *adds* to the seek and which decays naturally over time
    // via the existing launch_decay path. Result: a strong outward shove that
    // settles back to normal motion within ~1 second.
    for (auto& c : cells) {
        if (c.owner == player) continue;
        if (c.hiding_in != INVALID_ENTITY) continue;
        if (c.exit_anim_until > now) continue;
        float dx = c.pos.x - origin.x;
        float dy = c.pos.y - origin.y;
        float dsq = dx * dx + dy * dy;
        if (dsq > radius_sq) continue;
        float d = std::sqrt(dsq);
        if (d < 1.0f) continue;
        // Linear falloff: full push at the centre, zero at the edge.
        float t01 = 1.0f - d / radius;
        float push = push_peak * t01;
        c.launch_vel.x += (dx / d) * push;
        c.launch_vel.y += (dy / d) * push;
    }

    // Push food too (weaker). The visible scatter is half the visual feedback for
    // the blast and gives the shockwave physical presence.
    for (auto& f : world.foodMut()) {
        float dx = f.pos.x - origin.x;
        float dy = f.pos.y - origin.y;
        float dsq = dx * dx + dy * dy;
        if (dsq > radius_sq) continue;
        float d = std::sqrt(dsq);
        if (d < 1.0f) continue;
        float t01 = 1.0f - d / radius;
        float push = push_peak * food_scale * t01;
        f.vel.x += (dx / d) * push;
        f.vel.y += (dy / d) * push;
    }
}

void doRespawn(World& world, PlayerId player, const Tuning& t) {
    // Idempotent: if this player already has any cells in the world, do nothing.
    // Prevents a double-click on PLAY AGAIN (or a duplicated network packet) from
    // turning into two cells at once.
    if (world.playerCellCount(player) > 0) return;

    // Pick a clear position using the deterministic world RNG. Same algorithm as
    // the host-side helper in main.cpp -- duplicated here so the command flow
    // doesn't need to ferry a pre-picked position over the wire.
    constexpr float kMargin    = 500.0f;
    constexpr float kClearance = 400.0f;
    Vec2  chosen{static_cast<float>(world.width())  * 0.5f,
                  static_cast<float>(world.height()) * 0.5f};
    for (int attempt = 0; attempt < 8; ++attempt) {
        Vec2 candidate{
            world.rng().rangeFloat(kMargin,
                                    static_cast<float>(world.width())  - kMargin),
            world.rng().rangeFloat(kMargin,
                                    static_cast<float>(world.height()) - kMargin),
        };
        bool clear = true;
        for (const auto& c : world.cells()) {
            if (distance(c.pos, candidate) < cellRadius(c.mass) + kClearance) {
                clear = false; break;
            }
        }
        if (clear) { chosen = candidate; break; }
        chosen = candidate; // accept last try if nothing was clear
    }
    world.spawnCell(player, chosen, t.start_mass);
}

} // namespace cr::rules
