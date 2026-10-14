// Prairie Bird Voice — the monophonic bird voice engine.
//
// Pure C++17 with no JUCE dependency, so the plugin and the offline render
// tool run exactly the same code. Signal path, in the order of the design doc:
//
//   keyboard -> tuning -> pitch movement (scoop, glide, drift, wobble)
//            -> voice source (whistle, overtones, second voice, breath noise)
//            -> beak and throat (pitch-tracking resonance)
//            -> intro chirp -> volume -> forest echo -> stereo out
#pragma once

#include "ForestEcho.h"
#include "Params.h"

#include <cstdint>
#include <vector>

namespace pbv
{

class BirdEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setSeed (uint32_t seed);

    // Call once per block (or whenever settings change) before process().
    void setParams (const ParamValues& p);

    // MIDI input. Velocities and controller values are 0..127, bend 0..16383.
    void noteOn (int note, int velocity);
    void noteOff (int note);
    void controller (int cc, int value);
    void pitchBend (int value14bit);
    void channelPressure (int value);
    void allNotesOff();

    // Renders numSamples into outL/outR (overwrites).
    void process (float* outL, float* outR, int numSamples);

    // Pitch in Hz a key plays with the current tuning, before any movement.
    double keyFrequency (int note) const;

private:
    enum class Stage { Idle, Waiting, Attack, Sustain, Release };

    struct Rng
    {
        uint32_t s = 0x9e3779b9u;
        float uniform()  // -1..1
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return (float) ((double) s / 2147483648.0 - 1.0);
        }
        float gauss() { return (uniform() + uniform() + uniform()) * 1.0f; } // ~unit variance
    };

    // Smoothly interpolated random values at a given rate: drift and wobble.
    struct ValueNoise
    {
        float a = 0.0f, b = 0.0f, phase = 0.0f;
        float next (Rng& rng, float rateHz, double sr);
    };

    double semitonesForKey (int note) const;
    double transposeSemitones() const;
    void startAttack();
    void startNewNote (int note, int velocity);
    void legatoTo (int note);
    void startRelease();
    float renderSample();

    ParamValues params;
    double sr = 48000.0;
    Rng rng;

    // Keys held, oldest first (last-note priority).
    int stack[16] {};
    int stackSize = 0;

    // Note state
    Stage stage = Stage::Idle;
    int currentNote = -1;
    float noteVel = 0.0f;   // 0..1
    float velGain = 1.0f;
    bool firstInPhrase = true;
    bool everPlayed = false;
    double timeSinceRelease = 1.0e9; // seconds with no key held
    double noteAge = 0.0;

    // Envelope
    double envT = 0.0, envDur = 0.0;
    float envStart = 0.0f, env = 0.0f;
    double waitLeft = 0.0;

    // Pitch (semitones, MIDI scale)
    double basePitch = 60.0;
    bool gliding = false;
    double glideFrom = 0.0, glideTo = 0.0, glideT = 0.0, glideDur = 0.0;
    float scoopStart = 0.0f;
    double scoopT = 1.0e9;
    bool sagActive = false;

    // Intro chirp
    bool chirpOn = false;
    double chirpT = 0.0, chirpBaseHz = 440.0;
    float chirpGain = 0.0f;
    double chirpPhase = 0.0, chirpLowPhase = 0.0;

    // Breath burst
    float burst = 0.0f, burstDecay = 0.0f;

    // Oscillators
    double phase = 0.0, phase2 = 0.0;

    // Beak resonance (TPT state-variable bandpass)
    float ic1 = 0.0f, ic2 = 0.0f;

    // Controllers
    float breathTarget = 0.0f, breath = 0.0f, breathCoeff = 0.0f;
    float aftertouch = 0.0f;
    double bendSemis = 0.0;

    // Smoothed settings
    float volGain = 0.0f, purity = 0.8f, openness = 0.5f, smoothCoeff = 0.0f;

    ValueNoise drift, wobbleP, wobbleL;

    ForestEcho echo;
    std::vector<float> dryBuf;
};

} // namespace pbv
