#pragma once

#include "raylib.h"

namespace cr {

// Procedurally-synthesised SFX. No asset files — each sound is generated as a Wave at
// construction time and uploaded once. Replace generateSound calls with LoadSound("…wav")
// when actual recorded SFX become available.
//
// Chomp uses a pool of aliases so rapid absorbs can overlap; pitch shifts up with combo.
class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();
    AudioSystem(const AudioSystem&)            = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    void update(); // re-triggers the ambient loop when it finishes

    // Event-driven SFX. `by_player` controls whether the loud-and-pitched player chomp
    // fires vs the quiet background bot-vs-bot variant.
    void playAbsorb(float mass_gained, int combo_count, bool by_player);
    void playSplit();
    void playEject();
    void playDash();
    void playBlast();
    void playVirusPop();
    void playPlayerDeath();
    void playNearMiss();
    void playCrit();
    void playCometWarn();   // low rumble + alarm chirp -- world-event telegraph
    void playCometStrike(); // sharper roar when the comet becomes active
    void playFoodRush();    // golden chime -- 3x-food world event announcement

    // Procedural ambient music. Generated as a 16-second seamless loop at ctor time;
    // playMusic starts the loop, stopMusic halts it, update() re-triggers when the
    // sound finishes a cycle. Music respects the music_enabled toggle, the global
    // music volume, AND a transient duck multiplier (set to <1.0 during death cam).
    void  playMusic();
    void  stopMusic();
    void  setMusicEnabled(bool v);
    bool  isMusicEnabled() const { return music_enabled_; }
    // Transient music volume multiplier (1.0 = full, e.g. 0.3 during death cam).
    void  setMusicDuck(float v);
    float musicDuck() const { return music_duck_; }

    void  setMasterVolume(float v);
    void  setSfxVolume(float v);
    void  setMusicVolume(float v);
    float masterVolume() const { return master_; }
    float sfxVolume()    const { return sfx_; }
    float musicVolume()  const { return music_; }
    bool  isReady()      const { return device_ready_; }

private:
    void applyVolumes();
    void applyMusicVolume(); // sets the music Sound's runtime volume

    static constexpr int kChompPoolSize = 8;

    bool  device_ready_  = false;
    float master_        = 1.0f;
    float sfx_           = 0.85f;
    float music_         = 0.5f;
    bool  music_enabled_ = true;
    float music_duck_    = 1.0f; // transient multiplier (death cam etc)
    bool  music_started_ = false; // whether playMusic has been called

    Sound chomp_pool_[kChompPoolSize]{};
    int   chomp_rr_  = 0;
    Sound split_{};
    Sound eject_{};
    Sound dash_{};
    Sound blast_{};
    Sound virus_{};
    Sound death_{};
    Sound near_miss_{};
    Sound crit_{};
    Sound comet_warn_{};
    Sound comet_strike_{};
    Sound food_rush_{};
    Sound music_sound_{};
};

} // namespace cr
