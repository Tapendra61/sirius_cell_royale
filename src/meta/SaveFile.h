#pragma once

#include "Missions.h"

#include <cstdint>
#include <string>

namespace cr {

// Persistent player state. Owned at the Client level; serialized to disk via save/load
// helpers below. Add new fields by:
//   1. Bump kSaveCurrentVersion in SaveFile.cpp
//   2. Append new field to SaveData
//   3. Handle the new version in loadFromFile()'s migration switch
struct SaveData {
    // ---- Progression ----
    uint32_t total_xp     = 0;
    uint32_t level        = 1;
    uint32_t games_played = 0;
    float    best_mass    = 0.0f;
    uint32_t best_combo   = 0;

    // ---- Settings: audio ----
    float master_volume = 1.0f;
    float sfx_volume    = 0.85f;
    float music_volume  = 0.5f;

    // ---- Settings: input ----
    bool hold_to_move  = false;
    bool invert_thumbs = false;

    // ---- v2 additions ----
    bool     music_enabled         = true;
    uint32_t last_mission_reset_day = 0;
    Mission  daily_missions[kMissionCount]{};

    // ---- v3 additions: accessibility + performance settings ----
    float    screen_shake_scale = 1.0f; // 0..1.5 multiplier applied to all trauma
    uint8_t  colorblind_mode    = 0;    // 0=off, 1=deuteranopia, 2=protanopia, 3=tritanopia
    bool     high_contrast      = false;
    uint16_t fps_cap            = 60;   // 0 = uncapped

    // ---- v4 additions: HUD text scale + first-run intro flag ----
    float    hud_text_scale     = 1.0f; // 0.85..1.30 multiplier on in-match HUD fonts
    bool     first_run_complete = false; // set true after the first-run intro plays
};

// On-disk format (little-endian, packed):
//   magic[4]      = "CRSV"
//   version       uint32  (1 currently)
//   payload_bytes uint32
//   crc32         uint32  (of payload)
//   payload       payload_bytes
//
// Save flow: serialize payload to memory, write {path}.tmp, fsync, atomically rename
// {path} -> {path}.bak (best-effort), rename {path}.tmp -> {path}. Survives power loss.
//
// Load flow: read header, verify magic+version+CRC; on any check failure, fall back to
// {path}.bak (one level of redundancy); on still-failure return false and let the caller
// keep defaults.
bool saveToFile(const SaveData& data, const std::string& path);
bool loadFromFile(SaveData& data, const std::string& path);

constexpr uint32_t kSaveCurrentVersion = 4;

} // namespace cr
