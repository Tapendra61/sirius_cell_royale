#include "Codec.h"

#include <cstring>
#include <type_traits>

namespace cr::codec {

namespace {

// ---- Low-level byte streams ----
// Writer just appends to a vector<uint8_t>; Reader walks a fixed-size buffer and
// returns false the moment it would read past the end. Both are trivial and live in
// the anonymous namespace because no caller outside this file needs them.

struct Writer {
    std::vector<uint8_t>& out;

    void writeBytes(const void* data, size_t n) {
        const auto* p = static_cast<const uint8_t*>(data);
        out.insert(out.end(), p, p + n);
    }
    template <typename T>
    void writePOD(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "writePOD requires trivially-copyable layout");
        writeBytes(&v, sizeof(T));
    }
};

struct Reader {
    const uint8_t* data;
    size_t         len;
    size_t         pos    = 0;
    bool           failed = false;

    bool readBytes(void* dst, size_t n) {
        if (failed || pos + n > len) { failed = true; return false; }
        std::memcpy(dst, data + pos, n);
        pos += n;
        return true;
    }
    template <typename T>
    bool readPOD(T& v) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "readPOD requires trivially-copyable layout");
        return readBytes(&v, sizeof(T));
    }
};

// ---- CellSnap ----
// Bools are packed into a single byte; everything else is plain POD with the same
// little-endian layout the host has in memory. See kSnapshotVersion in the header --
// bump it if any field is added/removed/reordered.

void writeCell(Writer& w, const CellSnap& c) {
    w.writePOD(c.id);
    w.writePOD(c.owner);
    w.writePOD(c.pos);
    w.writePOD(c.vel);
    w.writePOD(c.mass);
    uint8_t flags = static_cast<uint8_t>(
        (c.invuln         ? 0x01 : 0) |
        (c.dashing        ? 0x02 : 0) |
        (c.god            ? 0x04 : 0) |
        (c.is_elite       ? 0x08 : 0) |
        (c.shield_active  ? 0x10 : 0) |
        (c.magnet_active  ? 0x20 : 0) |
        (c.stealth_active ? 0x40 : 0) |
        (c.hiding         ? 0x80 : 0));
    w.writePOD(flags);
    w.writePOD(c.dash_cooldown_norm);
    w.writePOD(c.blast_cooldown_norm);
    w.writePOD(c.personality_tag);
    w.writePOD(c.dash_telegraph_norm);
    w.writePOD(c.shield_norm);
    w.writePOD(c.magnet_norm);
    w.writePOD(c.stealth_norm);
    w.writePOD(c.hiding_in_id);
    w.writePOD(c.blackhole_stamina_norm);
    w.writePOD(c.blackhole_visual_scale);
}

bool readCell(Reader& r, CellSnap& c) {
    r.readPOD(c.id);
    r.readPOD(c.owner);
    r.readPOD(c.pos);
    r.readPOD(c.vel);
    r.readPOD(c.mass);
    uint8_t flags = 0;
    r.readPOD(flags);
    c.invuln         = (flags & 0x01) != 0;
    c.dashing        = (flags & 0x02) != 0;
    c.god            = (flags & 0x04) != 0;
    c.is_elite       = (flags & 0x08) != 0;
    c.shield_active  = (flags & 0x10) != 0;
    c.magnet_active  = (flags & 0x20) != 0;
    c.stealth_active = (flags & 0x40) != 0;
    c.hiding         = (flags & 0x80) != 0;
    r.readPOD(c.dash_cooldown_norm);
    r.readPOD(c.blast_cooldown_norm);
    r.readPOD(c.personality_tag);
    r.readPOD(c.dash_telegraph_norm);
    r.readPOD(c.shield_norm);
    r.readPOD(c.magnet_norm);
    r.readPOD(c.stealth_norm);
    r.readPOD(c.hiding_in_id);
    r.readPOD(c.blackhole_stamina_norm);
    r.readPOD(c.blackhole_visual_scale);
    return !r.failed;
}

// ---- FoodSnap ----
void writeFood(Writer& w, const FoodSnap& f) {
    w.writePOD(f.id);
    w.writePOD(f.pos);
    w.writePOD(f.vel);
    w.writePOD(f.mass);
}
bool readFood(Reader& r, FoodSnap& f) {
    r.readPOD(f.id);
    r.readPOD(f.pos);
    r.readPOD(f.vel);
    r.readPOD(f.mass);
    return !r.failed;
}

// ---- VirusSnap ----
void writeVirus(Writer& w, const VirusSnap& v) {
    w.writePOD(v.id);
    w.writePOD(v.pos);
    w.writePOD(v.mass);
}
bool readVirus(Reader& r, VirusSnap& v) {
    r.readPOD(v.id);
    r.readPOD(v.pos);
    r.readPOD(v.mass);
    return !r.failed;
}

// ---- PickupSnap ----
void writePickup(Writer& w, const PickupSnap& p) {
    w.writePOD(p.id);
    w.writePOD(p.pos);
    w.writePOD(static_cast<uint8_t>(p.kind));
}
bool readPickup(Reader& r, PickupSnap& p) {
    r.readPOD(p.id);
    r.readPOD(p.pos);
    uint8_t k = 0;
    r.readPOD(k);
    p.kind = static_cast<PickupKind>(k);
    return !r.failed;
}

// ---- BlackHoleSnap ----
void writeBH(Writer& w, const BlackHoleSnap& b) {
    w.writePOD(b.id);
    w.writePOD(b.pos);
    w.writePOD(b.radius);
    w.writePOD(b.pull_radius);
    w.writePOD(b.occupancy);
}
bool readBH(Reader& r, BlackHoleSnap& b) {
    r.readPOD(b.id);
    r.readPOD(b.pos);
    r.readPOD(b.radius);
    r.readPOD(b.pull_radius);
    r.readPOD(b.occupancy);
    return !r.failed;
}

// ---- CometSnap ----
void writeComet(Writer& w, const CometSnap& c) {
    w.writePOD(c.id);
    w.writePOD(c.pos);
    w.writePOD(c.vel);
    w.writePOD(c.radius);
    w.writePOD(c.telegraph_start);
    w.writePOD(c.telegraph_end);
    w.writePOD(c.telegraph_norm);
}
bool readComet(Reader& r, CometSnap& c) {
    r.readPOD(c.id);
    r.readPOD(c.pos);
    r.readPOD(c.vel);
    r.readPOD(c.radius);
    r.readPOD(c.telegraph_start);
    r.readPOD(c.telegraph_end);
    r.readPOD(c.telegraph_norm);
    return !r.failed;
}

// ---- Event variant tags ----
// Independent from CommandTag so we can extend either without ABI drift on the other.
enum class EventTagWire : uint8_t {
    Absorb           = 0,
    Death            = 1,
    Split            = 2,
    Crit             = 3,
    NearMiss         = 4,
    PickupCollected  = 5,
    Blast            = 6,
    Comet            = 7,
};

void writeAbsorb(Writer& w, const AbsorbEvent& e) {
    w.writePOD(e.predator);
    w.writePOD(e.prey);
    w.writePOD(e.at);
    w.writePOD(e.mass_gained);
}
bool readAbsorb(Reader& r, AbsorbEvent& e) {
    r.readPOD(e.predator);
    r.readPOD(e.prey);
    r.readPOD(e.at);
    r.readPOD(e.mass_gained);
    return !r.failed;
}

void writeDeath(Writer& w, const DeathEvent& e) {
    w.writePOD(e.player);
    w.writePOD(e.by);
    w.writePOD(e.predator_player);
    w.writePOD(e.prey_personality);
    w.writePOD(e.predator_personality);
}
bool readDeath(Reader& r, DeathEvent& e) {
    r.readPOD(e.player);
    r.readPOD(e.by);
    r.readPOD(e.predator_player);
    r.readPOD(e.prey_personality);
    r.readPOD(e.predator_personality);
    return !r.failed;
}

void writeSplit(Writer& w, const SplitEvent& e) {
    w.writePOD(e.player);
    w.writePOD(e.from);
    w.writePOD(e.into);
}
bool readSplit(Reader& r, SplitEvent& e) {
    r.readPOD(e.player);
    r.readPOD(e.from);
    r.readPOD(e.into);
    return !r.failed;
}

void writeCrit(Writer& w, const CritEvent& e) {
    w.writePOD(e.predator);
    w.writePOD(e.at);
    w.writePOD(e.mass_gained);
}
bool readCrit(Reader& r, CritEvent& e) {
    r.readPOD(e.predator);
    r.readPOD(e.at);
    r.readPOD(e.mass_gained);
    return !r.failed;
}

void writeNearMiss(Writer& w, const NearMissEvent& e) {
    w.writePOD(e.hunter);
    w.writePOD(e.prey);
    w.writePOD(e.at);
}
bool readNearMiss(Reader& r, NearMissEvent& e) {
    r.readPOD(e.hunter);
    r.readPOD(e.prey);
    r.readPOD(e.at);
    return !r.failed;
}

void writePickupCollected(Writer& w, const PickupCollectedEvent& e) {
    w.writePOD(e.collector);
    w.writePOD(e.player);
    w.writePOD(static_cast<uint8_t>(e.kind));
    w.writePOD(e.at);
}
bool readPickupCollected(Reader& r, PickupCollectedEvent& e) {
    r.readPOD(e.collector);
    r.readPOD(e.player);
    uint8_t k = 0;
    r.readPOD(k);
    e.kind = static_cast<PickupKind>(k);
    r.readPOD(e.at);
    return !r.failed;
}

void writeBlast(Writer& w, const BlastEvent& e) {
    w.writePOD(e.source);
    w.writePOD(e.player);
    w.writePOD(e.at);
    w.writePOD(e.radius);
}
bool readBlast(Reader& r, BlastEvent& e) {
    r.readPOD(e.source);
    r.readPOD(e.player);
    r.readPOD(e.at);
    r.readPOD(e.radius);
    return !r.failed;
}

void writeCometEvent(Writer& w, const CometEvent& e) {
    w.writePOD(e.id);
    w.writePOD(static_cast<uint8_t>(e.phase));
    w.writePOD(e.at);
    w.writePOD(e.dir);
}
bool readCometEvent(Reader& r, CometEvent& e) {
    r.readPOD(e.id);
    uint8_t p = 0;
    r.readPOD(p);
    e.phase = static_cast<CometEvent::Phase>(p);
    r.readPOD(e.at);
    r.readPOD(e.dir);
    return !r.failed;
}

} // namespace

// ---- Snapshot ----

bool encodeSnapshot(const Snapshot& s, std::vector<uint8_t>& out) {
    out.clear();
    // Pre-reserve roughly the expected size so we don't reallocate during the write.
    // A typical snapshot at 50 cells + 3600 food + ~60 viruses + ~12 pickups + 5 BHs +
    // ~1 comet is about 100 KB, so 128 KB is a safe upper bound for the common case.
    out.reserve(128 * 1024);

    Writer w{out};
    w.writePOD(kSnapshotVersion);
    w.writePOD(s.tick);
    w.writePOD(s.rng_state);

    w.writePOD(static_cast<uint32_t>(s.cells.size()));
    for (const auto& c : s.cells) writeCell(w, c);

    w.writePOD(static_cast<uint32_t>(s.food.size()));
    for (const auto& f : s.food) writeFood(w, f);

    w.writePOD(static_cast<uint32_t>(s.viruses.size()));
    for (const auto& v : s.viruses) writeVirus(w, v);

    w.writePOD(static_cast<uint32_t>(s.pickups.size()));
    for (const auto& p : s.pickups) writePickup(w, p);

    w.writePOD(static_cast<uint32_t>(s.blackholes.size()));
    for (const auto& b : s.blackholes) writeBH(w, b);

    w.writePOD(static_cast<uint32_t>(s.comets.size()));
    for (const auto& c : s.comets) writeComet(w, c);

    return true;
}

bool decodeSnapshot(const uint8_t* data, size_t len, Snapshot& s) {
    Reader r{data, len};
    uint8_t version = 0;
    if (!r.readPOD(version) || version != kSnapshotVersion) return false;

    // Always reset the output to a clean snapshot so partial reads on failure leave
    // the caller with an obviously-empty value rather than a half-populated one.
    s = Snapshot{};
    r.readPOD(s.tick);
    r.readPOD(s.rng_state);

    auto readVec = [&](auto& vec, auto&& readOne) -> bool {
        uint32_t n = 0;
        if (!r.readPOD(n)) return false;
        // Bound the count generously to detect garbage packets without rejecting
        // legitimate snapshots. Largest array is food at ~4000 entries; cap at
        // 1_000_000 so a malformed length never causes a multi-GB allocation.
        if (n > 1'000'000) return false;
        vec.resize(n);
        for (auto& e : vec) {
            if (!readOne(r, e)) return false;
        }
        return true;
    };

    if (!readVec(s.cells,      readCell))   return false;
    if (!readVec(s.food,       readFood))   return false;
    if (!readVec(s.viruses,    readVirus))  return false;
    if (!readVec(s.pickups,    readPickup)) return false;
    if (!readVec(s.blackholes, readBH))     return false;
    if (!readVec(s.comets,     readComet))  return false;

    return !r.failed;
}

// ---- Command ----

bool encodeCommand(const Command& c, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(32);
    Writer w{out};
    w.writePOD(kCommandVersion);
    w.writePOD(c.player);
    w.writePOD(c.tick);
    const uint8_t tag = static_cast<uint8_t>(c.payload.index());
    w.writePOD(tag);
    switch (tag) {
        case static_cast<uint8_t>(CommandTag::Move):
            w.writePOD(std::get<MoveCmd>(c.payload).target);
            return true;
        case static_cast<uint8_t>(CommandTag::Split):
        case static_cast<uint8_t>(CommandTag::Eject):
        case static_cast<uint8_t>(CommandTag::Dash):
        case static_cast<uint8_t>(CommandTag::Blast):
        case static_cast<uint8_t>(CommandTag::Respawn):
            return true; // no payload bytes
        default:
            out.clear(); // unknown variant -- refuse to send garbage
            return false;
    }
}

bool decodeCommand(const uint8_t* data, size_t len, Command& c) {
    Reader r{data, len};
    uint8_t version = 0;
    if (!r.readPOD(version) || version != kCommandVersion) return false;
    c = Command{};
    r.readPOD(c.player);
    r.readPOD(c.tick);
    uint8_t tag = 0;
    if (!r.readPOD(tag)) return false;
    switch (tag) {
        case static_cast<uint8_t>(CommandTag::Move): {
            MoveCmd m;
            if (!r.readPOD(m.target)) return false;
            c.payload = m;
            return true;
        }
        case static_cast<uint8_t>(CommandTag::Split):
            c.payload = SplitCmd{}; return true;
        case static_cast<uint8_t>(CommandTag::Eject):
            c.payload = EjectCmd{}; return true;
        case static_cast<uint8_t>(CommandTag::Dash):
            c.payload = DashCmd{};  return true;
        case static_cast<uint8_t>(CommandTag::Blast):
            c.payload = BlastCmd{}; return true;
        case static_cast<uint8_t>(CommandTag::Respawn):
            c.payload = RespawnCmd{}; return true;
        default:
            return false;
    }
}

// ---- Event ----

bool encodeEvent(const GameEvent& e, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(48);
    Writer w{out};
    w.writePOD(kEventVersion);
    const uint8_t tag = static_cast<uint8_t>(e.index());
    w.writePOD(tag);
    switch (static_cast<EventTagWire>(tag)) {
        case EventTagWire::Absorb:
            writeAbsorb(w, std::get<AbsorbEvent>(e));            return true;
        case EventTagWire::Death:
            writeDeath(w, std::get<DeathEvent>(e));              return true;
        case EventTagWire::Split:
            writeSplit(w, std::get<SplitEvent>(e));              return true;
        case EventTagWire::Crit:
            writeCrit(w, std::get<CritEvent>(e));                return true;
        case EventTagWire::NearMiss:
            writeNearMiss(w, std::get<NearMissEvent>(e));        return true;
        case EventTagWire::PickupCollected:
            writePickupCollected(w, std::get<PickupCollectedEvent>(e)); return true;
        case EventTagWire::Blast:
            writeBlast(w, std::get<BlastEvent>(e));              return true;
        case EventTagWire::Comet:
            writeCometEvent(w, std::get<CometEvent>(e));         return true;
    }
    out.clear();
    return false;
}

// ---- Control-channel name string helpers ----
// Length-prefixed ASCII (u8 length + N bytes). All three control messages use
// the same format, so we factor the read/write into local helpers.
namespace {
void writeName(Writer& w, const std::string& name) {
    uint8_t len = static_cast<uint8_t>(
        std::min<size_t>(name.size(), kMaxWirePlayerNameLen));
    w.writePOD(len);
    w.writeBytes(name.data(), len);
}
bool readName(Reader& r, std::string& out) {
    uint8_t len = 0;
    if (!r.readPOD(len)) return false;
    if (len > kMaxWirePlayerNameLen) return false; // sanity cap
    out.resize(len);
    if (len == 0) return true;
    return r.readBytes(out.data(), len);
}
} // namespace

// ---- Welcome (host -> peer) ----
// On-wire: [type=Welcome][version=2][player_id][cell_id][host_name_len][host_name_bytes]

bool encodeWelcome(const WelcomeMsg& m, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(32);
    Writer w{out};
    w.writePOD(static_cast<uint8_t>(ControlMsgType::Welcome));
    w.writePOD(kWelcomeVersion);
    w.writePOD(m.player_id);
    w.writePOD(m.cell_id);
    writeName(w, m.host_name);
    return true;
}

bool decodeWelcome(const uint8_t* data, size_t len, WelcomeMsg& m) {
    Reader r{data, len};
    uint8_t type = 0;
    if (!r.readPOD(type)) return false;
    if (type != static_cast<uint8_t>(ControlMsgType::Welcome)) return false;
    uint8_t version = 0;
    if (!r.readPOD(version) || version != kWelcomeVersion) return false;
    m = WelcomeMsg{};
    r.readPOD(m.player_id);
    r.readPOD(m.cell_id);
    if (!readName(r, m.host_name)) return false;
    return !r.failed;
}

// ---- ClientHello (peer -> host) ----
// On-wire: [type=ClientHello][version=1][name_len][name_bytes]

bool encodeClientHello(const ClientHelloMsg& m, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(24);
    Writer w{out};
    w.writePOD(static_cast<uint8_t>(ControlMsgType::ClientHello));
    w.writePOD(kClientHelloVersion);
    writeName(w, m.name);
    return true;
}

bool decodeClientHello(const uint8_t* data, size_t len, ClientHelloMsg& m) {
    Reader r{data, len};
    uint8_t type = 0;
    if (!r.readPOD(type)) return false;
    if (type != static_cast<uint8_t>(ControlMsgType::ClientHello)) return false;
    uint8_t version = 0;
    if (!r.readPOD(version) || version != kClientHelloVersion) return false;
    m = ClientHelloMsg{};
    return readName(r, m.name);
}

// ---- PeerInfo (host -> all peers) ----
// On-wire: [type=PeerInfo][version=1][player_id][name_len][name_bytes]

bool encodePeerInfo(const PeerInfoMsg& m, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(28);
    Writer w{out};
    w.writePOD(static_cast<uint8_t>(ControlMsgType::PeerInfo));
    w.writePOD(kPeerInfoVersion);
    w.writePOD(m.player_id);
    writeName(w, m.name);
    return true;
}

bool decodePeerInfo(const uint8_t* data, size_t len, PeerInfoMsg& m) {
    Reader r{data, len};
    uint8_t type = 0;
    if (!r.readPOD(type)) return false;
    if (type != static_cast<uint8_t>(ControlMsgType::PeerInfo)) return false;
    uint8_t version = 0;
    if (!r.readPOD(version) || version != kPeerInfoVersion) return false;
    m = PeerInfoMsg{};
    r.readPOD(m.player_id);
    return readName(r, m.name);
}

bool decodeEvent(const uint8_t* data, size_t len, GameEvent& e) {
    Reader r{data, len};
    uint8_t version = 0;
    if (!r.readPOD(version) || version != kEventVersion) return false;
    uint8_t tag = 0;
    if (!r.readPOD(tag)) return false;
    switch (static_cast<EventTagWire>(tag)) {
        case EventTagWire::Absorb: {
            AbsorbEvent ev; if (!readAbsorb(r, ev)) return false; e = ev; return true;
        }
        case EventTagWire::Death: {
            DeathEvent ev; if (!readDeath(r, ev)) return false; e = ev; return true;
        }
        case EventTagWire::Split: {
            SplitEvent ev; if (!readSplit(r, ev)) return false; e = ev; return true;
        }
        case EventTagWire::Crit: {
            CritEvent ev; if (!readCrit(r, ev)) return false; e = ev; return true;
        }
        case EventTagWire::NearMiss: {
            NearMissEvent ev; if (!readNearMiss(r, ev)) return false; e = ev; return true;
        }
        case EventTagWire::PickupCollected: {
            PickupCollectedEvent ev;
            if (!readPickupCollected(r, ev)) return false;
            e = ev; return true;
        }
        case EventTagWire::Blast: {
            BlastEvent ev; if (!readBlast(r, ev)) return false; e = ev; return true;
        }
        case EventTagWire::Comet: {
            CometEvent ev; if (!readCometEvent(r, ev)) return false; e = ev; return true;
        }
    }
    return false;
}

} // namespace cr::codec
