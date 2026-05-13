#include "Rules.h"

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
void spawnExplosionChildren(World& world, PlayerId owner, Vec2 pos, float total_mass,
                            const Tuning& t, std::vector<GameEvent>& events,
                            EntityId source_id) {
    constexpr int kMaxPieces = 8;
    int existing  = world.playerCellCount(owner);
    int allowed   = std::max(1, t.max_cells_per_player - existing);
    int pieces    = std::min(kMaxPieces, allowed);
    if (pieces <= 0) return;
    float each_mass = total_mass / static_cast<float>(pieces);
    const Tick recombine_at =
        world.currentTick() + secondsToTicks(t.recombine_delay_sec);

    for (int i = 0; i < pieces; ++i) {
        float angle = i * (2.0f * kPi / pieces);
        Vec2  dir{std::cos(angle), std::sin(angle)};
        EntityId nid = world.spawnCell(owner, pos, each_mass);
        // spawnCell may realloc; resolve by id, never by index.
        if (auto* nc = world.findCell(nid)) {
            nc->launch_vel   = dir * t.launch_velocity;
            nc->target       = pos + dir * 200.0f;
            nc->recombine_at = recombine_at;
        }
    }
    events.push_back(SplitEvent{owner, source_id, INVALID_ENTITY});
}

} // namespace

void stepCells(World& world, const Tuning& t, float dt) {
    const Tick now = world.currentTick();
    for (auto& c : world.cellsMut()) {
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
                              && cells[i].mass >= prey.mass * t.mass_ratio_required;

            if (can_eat) {
                float gained  = prey.mass;
                bool  is_crit = world.rng().nextFloat() < t.chance_per_absorb;
                if (is_crit) gained *= t.bonus_multiplier;
                cells[i].mass += gained;
                events.push_back(AbsorbEvent{cells[i].id, prey.id, prey.pos, gained});
                if (is_crit) events.push_back(CritEvent{cells[i].id, prey.pos, gained});
                events.push_back(DeathEvent{prey.owner, cells[i].id});
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
                pending_explosions.push_back(PendingExplosion{
                    cells[i].owner, cells[i].pos, cells[i].mass, cells[i].id});
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
                               exp.source_id);
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

void processRecombine(World& world, const Tuning& /*t*/) {
    auto& cells = world.cellsMut();
    if (cells.size() < 2) return;
    const Tick now = world.currentTick();
    std::vector<char> dead(cells.size(), 0);

    for (size_t i = 0; i < cells.size(); ++i) {
        if (dead[i]) continue;
        if (cells[i].recombine_at > now) continue;

        for (size_t j = i + 1; j < cells.size(); ++j) {
            if (dead[j]) continue;
            if (cells[j].owner != cells[i].owner) continue;
            if (cells[j].recombine_at > now) continue;

            float ar = cellRadius(cells[i].mass);
            float br = cellRadius(cells[j].mass);
            float d  = distance(cells[i].pos, cells[j].pos);
            // Touching is enough -- recombine_at has already enforced the cooldown.
            if (d > ar + br) continue;

            cells[i].mass += cells[j].mass;
            dead[j] = 1;
        }
    }
    compactDead(cells, dead);
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

        EntityId child_id = world.spawnCell(player, c_pos, new_mass);
        if (auto* nc = world.findCell(child_id)) {
            nc->target       = c_target;
            nc->launch_vel   = dir * t.launch_velocity;
            nc->recombine_at = recombine_at;
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

} // namespace cr::rules
