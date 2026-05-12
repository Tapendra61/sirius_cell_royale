#pragma once

#include "core/Command.h"
#include "core/Snapshot.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cr {

// On-disk format (little-endian, packed):
//   magic[4]  = "CRRP"
//   version   uint32   (current = 1)
//   seed      uint64
//   world_w   int32
//   world_h   int32
//   initial_cells: uint32 count, then count * { id u32, owner u16, pos[2]f, mass f }
//   commands:      uint32 count, then count * { player u16, tick u32, tag u8, payload }
// Tuning is NOT captured; recorder and player must run with the same tuning.ini.
class Replay {
public:
    void recordSetup(uint64_t seed, int world_w, int world_h,
                     std::vector<CellSnap> initial_cells);
    void recordCommand(Command cmd);

    uint64_t                       seed() const { return seed_; }
    int                            worldWidth() const { return world_w_; }
    int                            worldHeight() const { return world_h_; }
    const std::vector<CellSnap>&   initialCells() const { return initial_cells_; }
    const std::vector<Command>&    commands() const { return commands_; }

    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);

    static constexpr uint32_t kCurrentVersion = 1;

private:
    uint64_t               seed_    = 0;
    int                    world_w_ = 0;
    int                    world_h_ = 0;
    std::vector<CellSnap>  initial_cells_;
    std::vector<Command>   commands_;
};

} // namespace cr
