#include "Replay.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

namespace cr {

namespace {

constexpr char kMagic[4] = {'C', 'R', 'R', 'P'};

template <typename T>
bool writeRaw(std::FILE* f, const T& v) {
    return std::fwrite(&v, sizeof(T), 1, f) == 1;
}

template <typename T>
bool readRaw(std::FILE* f, T& v) {
    return std::fread(&v, sizeof(T), 1, f) == 1;
}

bool writeCellSnap(std::FILE* f, const CellSnap& c) {
    return writeRaw(f, c.id) && writeRaw(f, c.owner) && writeRaw(f, c.pos) && writeRaw(f, c.mass);
}

bool readCellSnap(std::FILE* f, CellSnap& c) {
    if (!readRaw(f, c.id))    return false;
    if (!readRaw(f, c.owner)) return false;
    if (!readRaw(f, c.pos))   return false;
    if (!readRaw(f, c.mass))  return false;
    c.vel = {0.0f, 0.0f};
    return true;
}

bool writeCommand(std::FILE* f, const Command& cmd) {
    if (!writeRaw(f, cmd.player)) return false;
    if (!writeRaw(f, cmd.tick))   return false;
    uint8_t tag = static_cast<uint8_t>(cmd.payload.index());
    if (!writeRaw(f, tag)) return false;
    switch (tag) {
        case static_cast<uint8_t>(CommandTag::Move):
            return writeRaw(f, std::get<MoveCmd>(cmd.payload).target);
        case static_cast<uint8_t>(CommandTag::Split):
        case static_cast<uint8_t>(CommandTag::Eject):
        case static_cast<uint8_t>(CommandTag::Dash):
        case static_cast<uint8_t>(CommandTag::Blast):
            return true; // no payload
        default:
            return false;
    }
}

bool readCommand(std::FILE* f, Command& cmd) {
    if (!readRaw(f, cmd.player)) return false;
    if (!readRaw(f, cmd.tick))   return false;
    uint8_t tag;
    if (!readRaw(f, tag)) return false;
    switch (tag) {
        case static_cast<uint8_t>(CommandTag::Move): {
            MoveCmd m;
            if (!readRaw(f, m.target)) return false;
            cmd.payload = m;
            return true;
        }
        case static_cast<uint8_t>(CommandTag::Split):
            cmd.payload = SplitCmd{};
            return true;
        case static_cast<uint8_t>(CommandTag::Eject):
            cmd.payload = EjectCmd{};
            return true;
        case static_cast<uint8_t>(CommandTag::Dash):
            cmd.payload = DashCmd{};
            return true;
        case static_cast<uint8_t>(CommandTag::Blast):
            cmd.payload = BlastCmd{};
            return true;
        default:
            return false;
    }
}

struct FileCloser {
    std::FILE* f;
    ~FileCloser() { if (f) std::fclose(f); }
};

} // namespace

void Replay::recordSetup(uint64_t seed, int world_w, int world_h,
                         std::vector<CellSnap> initial_cells) {
    seed_          = seed;
    world_w_       = world_w;
    world_h_       = world_h;
    initial_cells_ = std::move(initial_cells);
    commands_.clear();
}

void Replay::recordCommand(Command cmd) {
    commands_.push_back(std::move(cmd));
}

bool Replay::saveToFile(const std::string& path) const {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    FileCloser guard{f};

    if (std::fwrite(kMagic, 4, 1, f) != 1) return false;
    uint32_t version = kCurrentVersion;
    if (!writeRaw(f, version)) return false;
    if (!writeRaw(f, seed_))   return false;
    int32_t w = static_cast<int32_t>(world_w_);
    int32_t h = static_cast<int32_t>(world_h_);
    if (!writeRaw(f, w)) return false;
    if (!writeRaw(f, h)) return false;

    uint32_t init_n = static_cast<uint32_t>(initial_cells_.size());
    if (!writeRaw(f, init_n)) return false;
    for (const auto& c : initial_cells_) {
        if (!writeCellSnap(f, c)) return false;
    }

    uint32_t cmd_n = static_cast<uint32_t>(commands_.size());
    if (!writeRaw(f, cmd_n)) return false;
    for (const auto& cmd : commands_) {
        if (!writeCommand(f, cmd)) return false;
    }

    return true;
}

bool Replay::loadFromFile(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    FileCloser guard{f};

    char magic[4];
    if (std::fread(magic, 4, 1, f) != 1)        return false;
    if (std::memcmp(magic, kMagic, 4) != 0)     return false;

    uint32_t version;
    if (!readRaw(f, version))               return false;
    if (version != kCurrentVersion)         return false;
    if (!readRaw(f, seed_))                 return false;
    int32_t w, h;
    if (!readRaw(f, w))                     return false;
    if (!readRaw(f, h))                     return false;
    world_w_ = w;
    world_h_ = h;

    uint32_t init_n;
    if (!readRaw(f, init_n))                return false;
    initial_cells_.clear();
    initial_cells_.resize(init_n);
    for (uint32_t i = 0; i < init_n; ++i) {
        if (!readCellSnap(f, initial_cells_[i])) return false;
    }

    uint32_t cmd_n;
    if (!readRaw(f, cmd_n))                 return false;
    commands_.clear();
    commands_.resize(cmd_n);
    for (uint32_t i = 0; i < cmd_n; ++i) {
        if (!readCommand(f, commands_[i])) return false;
    }

    return true;
}

} // namespace cr
