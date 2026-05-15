#pragma once

#include "core/Rng.h"
#include "core/Types.h"

#include <cstdint>
#include <vector>

namespace cr {

struct Cell {
    EntityId id     = INVALID_ENTITY;
    PlayerId owner  = INVALID_PLAYER;
    Vec2     pos;
    Vec2     vel;
    Vec2     target;
    Vec2     launch_vel{};               // additive velocity from splits / dashes; decays each tick
    float    mass   = 0.0f;
    Tick     dash_until           = 0;   // sim-tick when dash speed ends
    Tick     invuln_until         = 0;   // sim-tick when invuln frames end
    Tick     dash_cooldown_until  = 0;   // sim-tick when dash is usable again
    Tick     blast_cooldown_until = 0;   // sim-tick when the 4th ability is usable again
    Tick     recombine_at         = 0;   // earliest tick this cell can merge with same-owner
    // Power-up pickup effects. Each is the sim-tick at which the effect expires.
    Tick     shield_until         = 0;   // can't be absorbed while active
    Tick     magnet_until         = 0;   // nearby food drifts toward this cell
    Tick     stealth_until        = 0;   // bot AI ignores this cell as a target
    // Bot personality tag (0 = player; 1..N = enum personality + 1). Stored on the
    // Cell so death events / kill feed entries can read it without crossing into
    // BotDirector. BotDirector writes this once at spawn.
    uint8_t  personality_tag      = 0;
    // Elite flag mirrored from BotDirector at spawn so the snapshot builder and the
    // renderer can read it directly without scanning bots. (dash_telegraph stays on
    // BotMind because it has to be recomputed every tick, which would just move the
    // O(B×C) scan from buildSnapshot to BotDirector::tick.)
    bool     is_elite             = false;
    // Black-hole hiding state. hiding_in == INVALID_ENTITY means the cell is in the
    // open world. When non-invalid, the cell's position is pinned to that black
    // hole's centre and the cell is excluded from eating / pulls / recombine. The
    // stamina float runs 0..1 and gates entry (must be > kBlackHoleEntryFloor) so a
    // freshly-ejected cell can't immediately re-enter.
    EntityId hiding_in            = INVALID_ENTITY;
    float    blackhole_stamina    = 1.0f;
    // Smooth entry / exit animation. Either *_anim_until is 0 (not animating) or a
    // future tick (animation completes at that tick). During an animation, position
    // is lerp'd from anim_origin to (anim_target OR the BH centre), and the
    // CellSnap reports a visual_scale that the renderer applies to cell radius.
    Tick     entry_anim_until     = 0;
    Tick     exit_anim_until      = 0;
    Vec2     anim_origin{};       // where this animation started
    Vec2     anim_target{};       // where this animation ends (used by exit only;
                                   // entry uses the BH centre, looked up each tick)
    bool     god                  = false; // dev: never gets absorbed
};

struct Food {
    EntityId id   = INVALID_ENTITY;
    Vec2     pos;
    Vec2     vel;                       // non-zero while a pellet is in flight
    float    mass = 1.0f;
    PlayerId from_player = INVALID_PLAYER;
};

struct Virus {
    EntityId id   = INVALID_ENTITY;
    Vec2     pos;
    Vec2     vel;                       // pushed by ejected mass
    float    mass = 200.0f;
};

// Power-up pickup -- consumed by overlap with a cell. Spawned sparsely by the world
// upkeep step in Rules.cpp. There's no spatial grid for pickups because counts are
// tiny (<20); per-tick all-pairs check against cells is cheap.
struct Pickup {
    EntityId   id   = INVALID_ENTITY;
    Vec2       pos;
    PickupKind kind = PickupKind::None;
};

// Black hole hazard / safe-haven. Cells within `pull_radius` are accelerated toward
// the centre; once they cross the visual `radius` they enter the "hiding" state
// (Cell::hiding_in set). Hidden cells can't be eaten / can't eat, position pinned
// to the centre, stamina drains. Spawned once at world construction (no respawn);
// count + min-separation come from Tuning.
struct BlackHole {
    EntityId id          = INVALID_ENTITY;
    Vec2     pos;
    float    radius      = 0.0f; // visual / hide boundary
    float    pull_radius = 0.0f; // outer reach where the pull starts
};

// World-event comet. Spawned periodically by the world upkeep step. Two-phase:
//   1) Telegraph: from `spawned_at` to `start_at` the comet is "incoming" -- a glowing
//      path line is drawn on the world / minimap but the comet itself isn't yet visible
//      and doesn't kill anything. Gives the player a window to dodge.
//   2) Active: from `start_at` onward the comet flies in a straight line from one edge
//      of the map to the other, with `vel` set so it exits in `flight_sec` seconds. Any
//      cell within `radius` of `pos` dies on contact.
// Despawns automatically when `pos` leaves the world bounds + a margin.
//
// The path endpoints (telegraph_start, telegraph_end) are stored so the renderer can
// draw the predicted line without re-deriving it from pos+vel.
struct Comet {
    EntityId id              = INVALID_ENTITY;
    Vec2     pos;
    Vec2     vel;
    float    radius          = 0.0f;
    Tick     spawned_at      = 0;   // sim tick when the telegraph started
    Tick     start_at        = 0;   // sim tick when the comet becomes active
    Vec2     telegraph_start;       // world-edge entry point
    Vec2     telegraph_end;         // world-edge exit point (predicted from vel)
};

// Uniform-grid spatial index. Bucket size = 2 * max expected entity radius (~400px default).
// Stores VECTOR INDICES (not entity ids) so callers can dereference directly without an
// id-to-index lookup. World owns three separate grids (one per type) so queries return
// only entities of the type you want.
//
// Results come back in deterministic order (bucket-major, then insertion order within
// each bucket). An entity that straddles bucket boundaries can appear multiple times in
// the query output; callers should be idempotent (e.g. use a dead-flag guard).
class SpatialGrid {
public:
    SpatialGrid(int world_w, int world_h, int bucket_size);

    void clear();
    void insert(uint32_t index, Vec2 pos, float radius);
    void query(Vec2 min, Vec2 max, std::vector<uint32_t>& out) const;

    int bucketSize() const { return bucket_size_; }
    int cols() const { return cols_; }
    int rows() const { return rows_; }

private:
    int                                bucket_size_;
    int                                cols_;
    int                                rows_;
    std::vector<std::vector<uint32_t>> buckets_;
};

class World {
public:
    World(uint64_t seed, int width, int height, int bucket_size = 400);

    EntityId spawnCell(PlayerId owner, Vec2 pos, float mass);
    EntityId spawnFood(Vec2 pos);
    EntityId spawnFood(Vec2 pos, float mass, Vec2 vel, PlayerId from_player);
    EntityId spawnVirus(Vec2 pos, float mass);
    EntityId spawnPickup(Vec2 pos, PickupKind kind);
    EntityId spawnBlackHole(Vec2 pos, float radius, float pull_radius);
    EntityId spawnComet(Vec2 pos, Vec2 vel, float radius, Tick spawned_at, Tick start_at,
                        Vec2 telegraph_start, Vec2 telegraph_end);

    Cell*            findCell(EntityId id);
    Food*            findFood(EntityId id);
    Virus*           findVirus(EntityId id);
    Pickup*          findPickup(EntityId id);
    BlackHole*       findBlackHole(EntityId id);
    Comet*           findComet(EntityId id);
    const Cell*      findCell(EntityId id) const;
    const Food*      findFood(EntityId id) const;
    const Virus*     findVirus(EntityId id) const;
    const Pickup*    findPickup(EntityId id) const;
    const BlackHole* findBlackHole(EntityId id) const;
    const Comet*     findComet(EntityId id) const;

    const std::vector<Cell>&      cells()      const { return cells_; }
    const std::vector<Food>&      food()       const { return food_; }
    const std::vector<Virus>&     viruses()    const { return viruses_; }
    const std::vector<Pickup>&    pickups()    const { return pickups_; }
    const std::vector<BlackHole>& blackholes() const { return blackholes_; }
    const std::vector<Comet>&     comets()     const { return comets_; }

    std::vector<Cell>&      cellsMut()      { return cells_; }
    std::vector<Food>&      foodMut()       { return food_; }
    std::vector<Virus>&     virusesMut()    { return viruses_; }
    std::vector<Pickup>&    pickupsMut()    { return pickups_; }
    std::vector<BlackHole>& blackholesMut() { return blackholes_; }
    std::vector<Comet>&     cometsMut()     { return comets_; }

    int playerCellCount(PlayerId p) const;

    Rng&       rng() { return rng_; }
    const Rng& rng() const { return rng_; }

    Tick currentTick() const { return tick_; }
    void advanceTick() { ++tick_; }

    int width() const { return width_; }
    int height() const { return height_; }

    // Rebuild all three typed spatial grids from the current cell/food/virus arrays.
    // Callers should invoke this AFTER motion has settled but BEFORE collision-driven
    // interactions that need spatial queries. Grids store vector indices, so they're
    // invalidated by any subsequent push_back / compactDead -- don't query after those.
    void               rebuildGrids();
    const SpatialGrid& cellsGrid()   const { return cells_grid_; }
    const SpatialGrid& foodsGrid()   const { return foods_grid_; }
    const SpatialGrid& virusesGrid() const { return viruses_grid_; }

private:
    int                width_;
    int                height_;
    Rng                rng_;
    Tick               tick_     = 0;
    EntityId           next_id_  = 1;
    std::vector<Cell>      cells_;
    std::vector<Food>      food_;
    std::vector<Virus>     viruses_;
    std::vector<Pickup>    pickups_;
    std::vector<BlackHole> blackholes_;
    std::vector<Comet>     comets_; // tiny vector (0..1 typically); no spatial grid
    SpatialGrid            cells_grid_;
    SpatialGrid            foods_grid_;
    SpatialGrid            viruses_grid_;
};

} // namespace cr
