#include "SaveFile.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

namespace cr {

namespace {

constexpr char kMagic[4] = {'C', 'R', 'S', 'V'};

// CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320). Plenty fast for ~100-byte
// payloads we generate; ~30ns/byte unoptimized.
uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

template <typename T>
void appendPod(std::vector<uint8_t>& buf, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    buf.insert(buf.end(), p, p + sizeof(T));
}

template <typename T>
bool readPod(const uint8_t* buf, size_t buf_len, size_t& offset, T& out) {
    if (offset + sizeof(T) > buf_len) return false;
    std::memcpy(&out, buf + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

// v1 fields are the original Phase 9 layout. v2 appends music_enabled +
// last_mission_reset_day + 3 missions (kind/target/completed, progress is transient).
// Future schema bumps should append rather than re-order so older payloads stay readable.

// Read the v1 fields starting at `off` and advance `off` past them. Returns false
// if the buffer ran short. Separated out so the v2 deserializer can reuse it.
bool readV1Fields(const uint8_t* buf, size_t len, size_t& off, SaveData& out) {
    if (!readPod(buf, len, off, out.total_xp))      return false;
    if (!readPod(buf, len, off, out.level))         return false;
    if (!readPod(buf, len, off, out.games_played))  return false;
    if (!readPod(buf, len, off, out.best_mass))     return false;
    if (!readPod(buf, len, off, out.best_combo))    return false;
    if (!readPod(buf, len, off, out.master_volume)) return false;
    if (!readPod(buf, len, off, out.sfx_volume))    return false;
    if (!readPod(buf, len, off, out.music_volume))  return false;
    uint8_t flags = 0;
    if (!readPod(buf, len, off, flags))             return false;
    out.hold_to_move  = (flags & 1u) != 0;
    out.invert_thumbs = (flags & 2u) != 0;
    return true;
}

void writeV1Fields(std::vector<uint8_t>& buf, const SaveData& d) {
    appendPod(buf, d.total_xp);
    appendPod(buf, d.level);
    appendPod(buf, d.games_played);
    appendPod(buf, d.best_mass);
    appendPod(buf, d.best_combo);
    appendPod(buf, d.master_volume);
    appendPod(buf, d.sfx_volume);
    appendPod(buf, d.music_volume);
    uint8_t flags = (d.hold_to_move ? 1u : 0u) | (d.invert_thumbs ? 2u : 0u);
    appendPod(buf, flags);
}

// Write the v2 chunk (music_enabled + missions) into buf. Shared between v2 and v3
// payloads so the prefix layout matches and older readers can still parse what they
// understand.
void writeV2Fields(std::vector<uint8_t>& buf, const SaveData& d) {
    uint8_t flags2 = (d.music_enabled ? 1u : 0u);
    appendPod(buf, flags2);
    appendPod(buf, d.last_mission_reset_day);
    for (int i = 0; i < kMissionCount; ++i) {
        const Mission& m = d.daily_missions[i];
        uint8_t kind = static_cast<uint8_t>(m.kind);
        appendPod(buf, kind);
        appendPod(buf, m.target);
        uint8_t completed = m.completed ? 1u : 0u;
        appendPod(buf, completed);
    }
}

std::vector<uint8_t> serializePayload(const SaveData& d) {
    std::vector<uint8_t> buf;
    buf.reserve(160);
    writeV1Fields(buf, d);
    writeV2Fields(buf, d);

    // ---- v3 additions: accessibility / performance ----
    appendPod(buf, d.screen_shake_scale);
    appendPod(buf, d.colorblind_mode);
    uint8_t flags3 = (d.high_contrast ? 1u : 0u);
    appendPod(buf, flags3);
    appendPod(buf, d.fps_cap);
    return buf;
}

bool deserializePayloadV1(const uint8_t* buf, size_t len, SaveData& out) {
    size_t off = 0;
    return readV1Fields(buf, len, off, out);
}

// Reads the v2 chunk starting at `off`. Returns false if buffer is short. v3
// deserializer reuses this.
bool readV2Chunk(const uint8_t* buf, size_t len, size_t& off, SaveData& out) {
    uint8_t flags2 = 0;
    if (!readPod(buf, len, off, flags2)) return false;
    out.music_enabled = (flags2 & 1u) != 0;
    if (!readPod(buf, len, off, out.last_mission_reset_day)) return false;

    for (int i = 0; i < kMissionCount; ++i) {
        uint8_t kind = 0;
        int32_t target = 0;
        uint8_t completed = 0;
        if (!readPod(buf, len, off, kind))      return false;
        if (!readPod(buf, len, off, target))    return false;
        if (!readPod(buf, len, off, completed)) return false;
        out.daily_missions[i].kind      = static_cast<MissionKind>(kind);
        out.daily_missions[i].target    = target;
        out.daily_missions[i].progress  = 0;
        out.daily_missions[i].completed = (completed != 0);
    }
    return true;
}

bool deserializePayloadV2(const uint8_t* buf, size_t len, SaveData& out) {
    size_t off = 0;
    if (!readV1Fields(buf, len, off, out)) return false;
    return readV2Chunk(buf, len, off, out);
}

bool deserializePayloadV3(const uint8_t* buf, size_t len, SaveData& out) {
    size_t off = 0;
    if (!readV1Fields(buf, len, off, out)) return false;
    if (!readV2Chunk(buf, len, off, out))  return false;
    if (!readPod(buf, len, off, out.screen_shake_scale)) return false;
    if (!readPod(buf, len, off, out.colorblind_mode))    return false;
    uint8_t flags3 = 0;
    if (!readPod(buf, len, off, flags3)) return false;
    out.high_contrast = (flags3 & 1u) != 0;
    if (!readPod(buf, len, off, out.fps_cap)) return false;
    return true;
}

struct FileCloser {
    std::FILE* f;
    ~FileCloser() { if (f) std::fclose(f); }
};

bool tryLoadOne(const std::string& path, SaveData& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    FileCloser guard{f};

    char magic[4];
    if (std::fread(magic, 1, 4, f) != 4) return false;
    if (std::memcmp(magic, kMagic, 4) != 0) return false;

    uint32_t version = 0;
    if (std::fread(&version, sizeof(version), 1, f) != 1) return false;
    if (version == 0 || version > kSaveCurrentVersion) return false;

    uint32_t payload_bytes = 0;
    if (std::fread(&payload_bytes, sizeof(payload_bytes), 1, f) != 1) return false;
    if (payload_bytes > 1u << 20) return false; // sanity: <1MB

    uint32_t want_crc = 0;
    if (std::fread(&want_crc, sizeof(want_crc), 1, f) != 1) return false;

    std::vector<uint8_t> payload(payload_bytes);
    if (payload_bytes > 0
        && std::fread(payload.data(), 1, payload_bytes, f) != payload_bytes) return false;

    if (crc32(payload.data(), payload.size()) != want_crc) return false;

    if (version == 1) {
        return deserializePayloadV1(payload.data(), payload.size(), out);
    }
    if (version == 2) {
        return deserializePayloadV2(payload.data(), payload.size(), out);
    }
    if (version == 3) {
        return deserializePayloadV3(payload.data(), payload.size(), out);
    }
    return false; // unsupported version (future-proof spot for migrations)
}

} // namespace

bool saveToFile(const SaveData& data, const std::string& path) {
    auto payload  = serializePayload(data);
    uint32_t crc  = crc32(payload.data(), payload.size());
    uint32_t ver  = kSaveCurrentVersion;
    uint32_t plen = static_cast<uint32_t>(payload.size());

    const std::string tmp = path + ".tmp";
    {
        std::FILE* f = std::fopen(tmp.c_str(), "wb");
        if (!f) return false;
        FileCloser guard{f};

        if (std::fwrite(kMagic, 1, 4, f) != 4) return false;
        if (std::fwrite(&ver,   sizeof(ver),  1, f) != 1) return false;
        if (std::fwrite(&plen,  sizeof(plen), 1, f) != 1) return false;
        if (std::fwrite(&crc,   sizeof(crc),  1, f) != 1) return false;
        if (plen > 0 && std::fwrite(payload.data(), 1, plen, f) != plen) return false;
        std::fflush(f);
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(path, ec)) {
        // Best-effort backup. If rename fails we just lose the previous .bak; never fatal.
        const std::string bak = path + ".bak";
        fs::remove(bak, ec);
        fs::rename(path, bak, ec);
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        // Fallback for platforms where rename can't overwrite an existing file.
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
        if (ec) return false;
    }
    return true;
}

bool loadFromFile(SaveData& data, const std::string& path) {
    if (tryLoadOne(path, data)) return true;
    // Fall back to backup.
    if (tryLoadOne(path + ".bak", data)) return true;
    return false;
}

} // namespace cr
