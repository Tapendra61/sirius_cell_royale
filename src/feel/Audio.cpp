#include "Audio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace cr {

namespace {

constexpr int   kSampleRate = 22050;
constexpr float kTwoPi      = 6.28318530718f;

// Build a Sound from a per-sample generator. The Wave's data is freed by UnloadWave
// (which calls RL_FREE -- defaults to std::free, matching std::malloc here).
template <typename Gen>
Sound generateSound(int duration_ms, Gen gen) {
    int       frame_count = kSampleRate * duration_ms / 1000;
    int16_t*  samples = static_cast<int16_t*>(std::malloc(frame_count * sizeof(int16_t)));
    for (int i = 0; i < frame_count; ++i) {
        float t = static_cast<float>(i) / kSampleRate;
        float s = std::clamp(gen(t), -1.0f, 1.0f);
        samples[i] = static_cast<int16_t>(s * 32767.0f);
    }
    Wave w{};
    w.frameCount = static_cast<unsigned int>(frame_count);
    w.sampleRate = kSampleRate;
    w.sampleSize = 16;
    w.channels   = 1;
    w.data       = samples;
    Sound sound = LoadSoundFromWave(w);
    UnloadWave(w);
    return sound;
}

// Per-sound generators ----------------------------------------------------------------

float gChomp(float t) {
    // Clean "boop": no FM wobble (which was producing the fart-like buzz). Higher base
    // frequency (550 Hz) with a snappy downward sweep + onset transient click gives a
    // percussive "pickup" feel that doesn't get muddier when pitched up by combo.
    if (t > 0.06f) return 0.0f;
    float env  = std::exp(-t * 60.0f);
    float freq = 550.0f * std::exp(-t * 12.0f); // 550 → 220 over 60ms
    float main = std::sin(t * freq * kTwoPi);
    float click = (t < 0.0025f)
        ? std::sin(t * 3500.0f * kTwoPi) * (1.0f - t / 0.0025f) * 0.45f
        : 0.0f;
    return (main * 0.65f + click) * env * 0.75f;
}

float gSplit(float t) {
    if (t > 0.18f) return 0.0f;
    float env = std::exp(-t * 12.0f);
    // Downward-swept fundamental + crisp click at onset.
    float freq  = 420.0f * std::exp(-t * 8.0f);
    float click = (t < 0.006f) ? (1.0f - t / 0.006f) * 0.4f : 0.0f;
    return (std::sin(t * freq * kTwoPi) * 0.70f + click) * env * 0.60f;
}

float gEject(float t) {
    if (t > 0.10f) return 0.0f;
    float env = std::exp(-t * 28.0f);
    // Pseudo-noise from stacked detuned sinusoids.
    float n  = std::sin(t * 1100.0f * kTwoPi) * 0.30f;
    n       += std::sin(t * 1700.0f * kTwoPi) * 0.25f;
    n       += std::sin(t * 2300.0f * kTwoPi) * 0.20f;
    n       += std::sin(t * 3100.0f * kTwoPi) * 0.15f;
    return n * env * 0.55f;
}

float gDash(float t) {
    if (t > 0.22f) return 0.0f;
    float env  = std::exp(-t * 6.5f);
    float freq = 480.0f * (1.0f + t * 4.0f); // rising swoop
    float w    = std::sin(t * freq * kTwoPi) * 0.40f;
    w         += std::sin(t * freq * 1.7f * kTwoPi) * 0.20f;
    w         += std::sin(t * freq * 2.4f * kTwoPi) * 0.15f;
    return w * env * 0.60f;
}

float gVirus(float t) {
    if (t > 0.30f) return 0.0f;
    float env = std::exp(-t * 8.0f);
    float low = std::sin(t * 85.0f  * kTwoPi) * 0.65f;
    float mid = std::sin(t * 240.0f * kTwoPi) * 0.20f * std::exp(-t * 14.0f);
    float pop = std::sin(t * 2500.0f * kTwoPi) * 0.35f * std::exp(-t * 22.0f);
    return (low + mid + pop) * env * 0.60f;
}

float gDeath(float t) {
    if (t > 0.45f) return 0.0f;
    float env  = std::exp(-t * 3.5f);
    float freq = 300.0f * std::exp(-t * 2.6f); // slow descend
    return std::sin(t * freq * kTwoPi) * env * 0.65f;
}

float gNearMiss(float t) {
    if (t > 0.18f) return 0.0f;
    float env  = std::exp(-t * 12.0f);
    float freq = (t < 0.08f) ? 900.0f : 660.0f;
    return std::sin(t * freq * kTwoPi) * env * 0.55f;
}

float gBlast(float t) {
    // Mass-blast 4th ability. Deep low-frequency thump with a brief high-frequency
    // crack on the very first millisecond -- "WHUUUMP-tk" -- the low fundamental
    // dominates so it reads as concussive rather than chirpy.
    if (t > 0.40f) return 0.0f;
    float env_low  = std::exp(-t * 6.5f);
    float env_crack = (t < 0.012f) ? (1.0f - t / 0.012f) : 0.0f;
    float freq_low = 60.0f * std::exp(-t * 3.5f);     // 60 Hz -> 14 Hz sub-bass slide
    float freq_mid = 180.0f * std::exp(-t * 5.0f);    // tonal body
    float low      = std::sin(t * freq_low * kTwoPi) * 0.75f;
    float mid      = std::sin(t * freq_mid * kTwoPi) * 0.30f * env_low;
    float crack    = std::sin(t * 3200.0f * kTwoPi) * env_crack * 0.35f;
    return (low * env_low + mid + crack) * 0.85f;
}

float gCrit(float t) {
    // Rising harmonics sting (original design, dropped one octave). Two sine partials
    // sweep up over the duration with a slow exp decay -- same character as the first
    // version but pitched into the warmer mid-range instead of the bright top end.
    if (t > 0.32f) return 0.0f;
    float env   = std::exp(-t * 4.0f);
    float freq1 = 330.0f * (1.0f + t * 1.6f); // ~330 Hz -> 500 Hz
    float freq2 = 495.0f * (1.0f + t * 2.0f); // ~495 Hz -> 812 Hz (perfect-5th interval)
    float main  = std::sin(t * freq1 * kTwoPi) * 0.55f
                + std::sin(t * freq2 * kTwoPi) * 0.40f;
    return main * env * 0.72f;
}

// Procedurally synthesised ambient pad. 16-second seamless loop -- every component
// (note frequencies + LFO envelopes) completes an integer number of cycles in 16s so
// the buffer's first and last samples line up without a click. Three slow-moving
// sines at A2 / E3 / A3 modulated by independent LFOs give a gently shifting drone.
// Soft-clipped through tanh for a warmer, less hollow sound. Replace with a real
// music file (LoadMusicStream) when an asset is available.
float gMusicPad(float t) {
    // LFOs (1, 2, 3 cycles per 16s loop = seamless at the boundary).
    float lfo1 = 0.5f + 0.5f * std::sin(t * (1.0f  / 16.0f) * kTwoPi);
    float lfo2 = 0.5f + 0.5f * std::sin(t * (2.0f  / 16.0f) * kTwoPi);
    float lfo3 = 0.5f + 0.5f * std::sin(t * (3.0f  / 16.0f) * kTwoPi);

    // Notes at A2 (110 Hz), E3 (165 Hz, close enough to true 164.81), A3 (220 Hz).
    // 110*16=1760, 165*16=2640, 220*16=3520 -- all integer cycles per loop.
    float a = std::sin(t * 110.0f * kTwoPi) * (0.30f + 0.45f * lfo1);
    float b = std::sin(t * 165.0f * kTwoPi) * (0.20f + 0.35f * lfo2);
    float c = std::sin(t * 220.0f * kTwoPi) * (0.15f + 0.25f * lfo3);

    float sum = (a + b + c) * 0.35f;
    return std::tanh(sum * 1.3f) * 0.55f;
}

} // namespace

AudioSystem::AudioSystem() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        device_ready_ = false;
        return;
    }
    device_ready_ = true;

    chomp_pool_[0] = generateSound(90, gChomp);
    for (int i = 1; i < kChompPoolSize; ++i) {
        chomp_pool_[i] = LoadSoundAlias(chomp_pool_[0]);
    }
    split_       = generateSound(180,   gSplit);
    eject_       = generateSound(110,   gEject);
    dash_        = generateSound(220,   gDash);
    blast_       = generateSound(420,   gBlast);
    virus_       = generateSound(300,   gVirus);
    death_       = generateSound(450,   gDeath);
    near_miss_   = generateSound(180,   gNearMiss);
    crit_        = generateSound(320,   gCrit);
    music_sound_ = generateSound(16000, gMusicPad); // 16 seconds, seamless

    applyVolumes();
    applyMusicVolume();
}

AudioSystem::~AudioSystem() {
    if (!device_ready_) return;
    if (IsSoundPlaying(music_sound_)) StopSound(music_sound_);
    for (int i = 1; i < kChompPoolSize; ++i) {
        UnloadSoundAlias(chomp_pool_[i]);
    }
    UnloadSound(chomp_pool_[0]);
    UnloadSound(split_);
    UnloadSound(eject_);
    UnloadSound(dash_);
    UnloadSound(blast_);
    UnloadSound(virus_);
    UnloadSound(death_);
    UnloadSound(near_miss_);
    UnloadSound(crit_);
    UnloadSound(music_sound_);
    CloseAudioDevice();
}

void AudioSystem::update() {
    // Re-trigger the ambient loop seamlessly when the previous play finishes. Sounds
    // (unlike Music streams) don't loop natively, so we drive the loop ourselves.
    if (!device_ready_) return;
    if (music_started_ && music_enabled_ && !IsSoundPlaying(music_sound_)) {
        PlaySound(music_sound_);
        applyMusicVolume();
    }
}

void AudioSystem::setMasterVolume(float v) { master_ = std::clamp(v, 0.0f, 1.0f); applyVolumes(); }
void AudioSystem::setSfxVolume(float v)    { sfx_    = std::clamp(v, 0.0f, 1.0f); applyVolumes(); }
void AudioSystem::setMusicVolume(float v)  {
    music_ = std::clamp(v, 0.0f, 1.0f);
    applyMusicVolume();
}

void AudioSystem::applyVolumes() {
    if (!device_ready_) return;
    SetMasterVolume(master_);
    // Per-sound base volume is set on the fly by play* functions.
}

void AudioSystem::applyMusicVolume() {
    if (!device_ready_) return;
    // Effective music volume = base * duck. Master volume is applied separately by
    // raylib via SetMasterVolume so we don't multiply it in here.
    SetSoundVolume(music_sound_, music_ * music_duck_);
}

void AudioSystem::playMusic() {
    if (!device_ready_) return;
    music_started_ = true;
    if (!music_enabled_) return;
    if (!IsSoundPlaying(music_sound_)) {
        PlaySound(music_sound_);
        applyMusicVolume();
    }
}

void AudioSystem::stopMusic() {
    if (!device_ready_) return;
    if (IsSoundPlaying(music_sound_)) StopSound(music_sound_);
    music_started_ = false;
}

void AudioSystem::setMusicEnabled(bool v) {
    music_enabled_ = v;
    if (!device_ready_) return;
    // Disabling stops playback immediately. Enabling does NOT auto-start -- it just
    // un-gates a future playMusic() call. This keeps applyLoadedSave from blasting
    // the procedural pad in everyone's ears every time a save is loaded.
    if (!v) {
        if (IsSoundPlaying(music_sound_)) StopSound(music_sound_);
        music_started_ = false;
    }
}

void AudioSystem::setMusicDuck(float v) {
    music_duck_ = std::clamp(v, 0.0f, 1.0f);
    applyMusicVolume();
}

void AudioSystem::playAbsorb(float mass_gained, int combo, bool by_player) {
    if (!device_ready_) return;
    // Background bot-vs-bot chomps would be a fog of small noises; gate them by mass.
    if (!by_player && mass_gained < 25.0f) return;
    float pitch  = by_player ? std::clamp(1.0f + combo * 0.07f, 1.0f, 2.50f) : 0.92f;
    float volume = (by_player ? 1.0f : 0.30f) * sfx_;
    Sound& s = chomp_pool_[chomp_rr_];
    chomp_rr_ = (chomp_rr_ + 1) % kChompPoolSize;
    SetSoundPitch(s, pitch);
    SetSoundVolume(s, volume);
    PlaySound(s);
}

void AudioSystem::playSplit() {
    if (!device_ready_) return;
    SetSoundVolume(split_, 1.0f * sfx_);
    PlaySound(split_);
}

void AudioSystem::playEject() {
    if (!device_ready_) return;
    SetSoundVolume(eject_, 0.85f * sfx_);
    PlaySound(eject_);
}

void AudioSystem::playDash() {
    if (!device_ready_) return;
    SetSoundVolume(dash_, 0.95f * sfx_);
    PlaySound(dash_);
}

void AudioSystem::playBlast() {
    if (!device_ready_) return;
    SetSoundVolume(blast_, 1.0f * sfx_);
    PlaySound(blast_);
}

void AudioSystem::playVirusPop() {
    if (!device_ready_) return;
    SetSoundVolume(virus_, 1.0f * sfx_);
    PlaySound(virus_);
}

void AudioSystem::playPlayerDeath() {
    if (!device_ready_) return;
    SetSoundVolume(death_, 1.0f * sfx_);
    PlaySound(death_);
}

void AudioSystem::playNearMiss() {
    if (!device_ready_) return;
    SetSoundVolume(near_miss_, 0.85f * sfx_);
    PlaySound(near_miss_);
}

void AudioSystem::playCrit() {
    if (!device_ready_) return;
    SetSoundVolume(crit_, 1.0f * sfx_);
    PlaySound(crit_);
}

} // namespace cr
