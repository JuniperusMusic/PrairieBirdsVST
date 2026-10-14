#include "BirdEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pbv
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 2.0 * kPi;
    constexpr float kBaseLevel = 0.25f;     // -12 dBFS whistle at volume 0 dB
    constexpr double kChirpUp = 0.018;      // measured: 18 ms upsweep
    constexpr double kChirpDown = 0.020;    // measured: 20 ms downslur
    constexpr double kChirpLowRatio = 0.5;  // estimate for the fainter lower voice; refine with the matching loop
    constexpr double kBeakFixedHz = 3000.0; // beak resonance when tracking is 0

    inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
    inline double semisToHz (double semis) { return 440.0 * std::exp2 ((semis - 69.0) / 12.0); }
    inline double raisedCosine (double x) // 0..1 -> 0..1, smooth at both ends
    {
        x = std::clamp (x, 0.0, 1.0);
        return 0.5 - 0.5 * std::cos (kPi * x);
    }
}

float BirdEngine::ValueNoise::next (Rng& rng, float rateHz, double sampleRate)
{
    phase += (float) (rateHz / sampleRate);
    while (phase >= 1.0f)
    {
        phase -= 1.0f;
        a = b;
        b = rng.gauss();
    }
    return a + (b - a) * (float) raisedCosine (phase);
}

void BirdEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    echo.prepare (sr);
    echo.setParams (params[kEchoWet], params[kEchoDecay]);
    dryBuf.assign ((size_t) std::max (1, maxBlockSize), 0.0f);
    breathCoeff = (float) std::exp (-1.0 / (0.008 * sr));
    smoothCoeff = (float) std::exp (-1.0 / (0.020 * sr));
    reset();
}

void BirdEngine::reset()
{
    stackSize = 0;
    stage = Stage::Idle;
    currentNote = -1;
    env = envStart = 0.0f;
    gliding = false;
    chirpOn = false;
    burst = 0.0f;
    phase = phase2 = chirpPhase = chirpLowPhase = 0.0;
    ic1 = ic2 = 0.0f;
    breath = breathTarget = 0.0f;
    aftertouch = 0.0f;
    bendSemis = 0.0;
    everPlayed = false;
    timeSinceRelease = 1.0e9;
    scoopT = 1.0e9;
    sagActive = false;
    volGain = dbToGain (params[kVolume]);
    purity = params[kPurity];
    openness = params[kBeakOpen];
    drift = {};
    wobbleP = {};
    wobbleL = {};
    echo.reset();
}

void BirdEngine::setSeed (uint32_t seed) { rng.s = seed != 0 ? seed : 0x9e3779b9u; }

void BirdEngine::setParams (const ParamValues& p)
{
    params = p;
    echo.setParams (p[kEchoWet], p[kEchoDecay]);
}

double BirdEngine::semitonesForKey (int note) const
{
    const int root = (int) params[kTuneRoot];
    const int degree = ((note - root) % 12 + 12) % 12;
    const double cents = params[kTune0 + degree] + params[kFineTune];
    return note + transposeSemitones() + cents / 100.0;
}

// The octave selector with its semitone and cent steps transposes the whole
// voice (+ sounds higher, up to 7 octaves). The bird's intervals never change.
double BirdEngine::transposeSemitones() const
{
    return 12.0 * params[kOctave] + params[kTransposeSemis] + params[kTransposeCents] / 100.0;
}

double BirdEngine::keyFrequency (int note) const { return semisToHz (semitonesForKey (note)); }

// ---------------------------------------------------------------- MIDI

void BirdEngine::noteOn (int note, int velocity)
{
    if (velocity <= 0) { noteOff (note); return; }

    // Remove the key if it is already held, then push it on top.
    for (int i = 0; i < stackSize; ++i)
        if (stack[i] == note)
        {
            std::memmove (stack + i, stack + i + 1, sizeof (int) * (size_t) (stackSize - i - 1));
            --stackSize;
            break;
        }
    if (stackSize < 16) stack[stackSize++] = note;

    const bool connected = stage == Stage::Waiting || stage == Stage::Attack || stage == Stage::Sustain;
    if (connected && stackSize > 1)
        legatoTo (note);
    else
        startNewNote (note, velocity);
}

void BirdEngine::noteOff (int note)
{
    int idx = -1;
    for (int i = 0; i < stackSize; ++i)
        if (stack[i] == note) { idx = i; break; }
    if (idx < 0) return;

    std::memmove (stack + idx, stack + idx + 1, sizeof (int) * (size_t) (stackSize - idx - 1));
    --stackSize;

    if (stackSize == 0)
        startRelease();
    else if (note == currentNote)
        legatoTo (stack[stackSize - 1]); // fall back to the key still held
}

void BirdEngine::controller (int cc, int value)
{
    switch (cc)
    {
        case 2:   breathTarget = value / 127.0f; break; // breath controller
        case 120: reset(); break;                       // all sound off
        case 123: allNotesOff(); break;
        default:  break; // MIDI learn and the nanoKONTROL2 map live in the plugin (stage 8)
    }
}

void BirdEngine::pitchBend (int value14bit)
{
    bendSemis = (value14bit - 8192) / 8192.0 * params[kBendRange];
}

void BirdEngine::channelPressure (int value) { aftertouch = value / 127.0f; }

void BirdEngine::allNotesOff()
{
    stackSize = 0;
    startRelease();
}

// ---------------------------------------------------------------- note logic

void BirdEngine::startNewNote (int note, int velocity)
{
    firstInPhrase = ! everPlayed || timeSinceRelease * 1000.0 > params[kPhraseGap];
    everPlayed = true;
    timeSinceRelease = 0.0;

    currentNote = note;
    noteVel = velocity / 127.0f;
    const float resp = params[kVelResponse];
    velGain = 1.0f + (std::pow (noteVel, 1.5f) - 1.0f) * resp;

    basePitch = semitonesForKey (note);
    gliding = false;
    noteAge = 0.0;
    sagActive = firstInPhrase;

    // Scoop: velocity sets the depth; Auto falls in from above on the first
    // note of a phrase and rises in from below on later ones.
    float dir = 0.0f;
    switch ((int) params[kScoopDir])
    {
        case 1:  dir = -1.0f; break;
        case 2:  dir = 1.0f; break;
        default: dir = firstInPhrase ? 1.0f : -1.0f; break;
    }
    scoopStart = dir * params[kScoopDepth] * noteVel;

    // Intro chirp on hard hits: it sounds first, the whistle follows after the lead time.
    if (velocity >= (int) params[kChirpThreshold] && params[kChirpThreshold] <= 127.0f)
    {
        chirpOn = true;
        chirpT = 0.0;
        chirpPhase = chirpLowPhase = 0.0;
        chirpBaseHz = semisToHz (basePitch);
        chirpGain = dbToGain (params[kChirpLevel]) * velGain;
        waitLeft = params[kChirpLead] * 0.001;
        if (waitLeft > 0.0)
        {
            envStart = env;
            stage = Stage::Waiting;
            return;
        }
    }

    startAttack();
}

void BirdEngine::startAttack()
{
    const float resp = params[kVelResponse];
    // Hard hits start faster: velocity 127 halves the onset time at full response.
    const double speed = 1.0 + ((1.5 - noteVel) - 1.0) * resp;
    envDur = std::max (0.001, params[kOnset] * 0.001 * speed);
    envT = 0.0;
    envStart = env;
    stage = Stage::Attack;
    scoopT = 0.0;

    // The burst is a puff of air: in breath mode it follows the breath, not the key.
    const bool breathMode = params[kBreathMode] >= 0.5f;
    burst = params[kBreathBurst] * 0.3f * (breathMode ? breathTarget : velGain);
    burstDecay = (float) std::exp (-1.0 / (0.012 * sr));
}

void BirdEngine::legatoTo (int note)
{
    currentNote = note;
    const double target = semitonesForKey (note);
    sagActive = false;
    noteAge = 0.0;

    if (stage == Stage::Waiting)
    {
        basePitch = target; // the whistle hasn't started yet
        return;
    }

    const float amount = params[kGlideAmount];
    double from = basePitch;
    if (gliding)
    {
        const double p = std::clamp (glideT / glideDur, 0.0, 1.0);
        from = glideFrom + (glideTo - glideFrom) * p;
    }

    if (amount <= 0.0f)
    {
        basePitch = target;
        gliding = false;
        return;
    }

    // Jump part of the way, glide the rest: 0 = clean jump, 1 = full sweep.
    glideFrom = target + (from - target) * amount;
    glideTo = target;
    glideT = 0.0;
    glideDur = std::max (0.001, params[kGlideTime] * 0.001);
    gliding = true;
    basePitch = glideFrom;
}

void BirdEngine::startRelease()
{
    if (stage == Stage::Idle || stage == Stage::Release) return;
    if (stage == Stage::Waiting)
    {
        stage = env > 0.0f ? Stage::Release : Stage::Idle; // key let go during the chirp lead
    }
    else
    {
        stage = Stage::Release;
    }
    envStart = env;
    envT = 0.0;
    envDur = std::max (0.001, params[kRelease] * 0.001);
}

// ---------------------------------------------------------------- audio

float BirdEngine::renderSample()
{
    const double dt = 1.0 / sr;

    // Controllers and smoothed settings
    breath = breathTarget + (breath - breathTarget) * breathCoeff;
    const bool breathMode = params[kBreathMode] >= 0.5f;
    volGain = dbToGain (params[kVolume]) + (volGain - dbToGain (params[kVolume])) * smoothCoeff;

    float purityTarget = params[kPurity] - params[kBreathEdge] * breath * 0.5f;
    purity = purityTarget + (purity - purityTarget) * smoothCoeff;

    float modSrc = 0.0f;
    switch ((int) params[kBeakModSource])
    {
        case 1:  modSrc = breath; break;
        case 2:  modSrc = noteVel; break;
        case 3:  modSrc = aftertouch; break;
        default: break;
    }
    const float openTarget = std::clamp (params[kBeakOpen] + params[kBeakModAmount] * modSrc, 0.0f, 1.0f);
    openness = openTarget + (openness - openTarget) * smoothCoeff;

    if (stackSize == 0) timeSinceRelease += dt;

    // Envelope
    switch (stage)
    {
        case Stage::Idle:
            env = 0.0f;
            break;
        case Stage::Waiting:
            waitLeft -= dt;
            if (waitLeft <= 0.0) startAttack();
            break;
        case Stage::Attack:
            envT += dt;
            env = envStart + (1.0f - envStart) * (float) raisedCosine (envT / envDur);
            if (envT >= envDur) { env = 1.0f; stage = Stage::Sustain; }
            break;
        case Stage::Sustain:
            env = 1.0f;
            break;
        case Stage::Release:
            envT += dt;
            env = envStart * (1.0f - (float) raisedCosine (envT / envDur));
            if (envT >= envDur) { env = 0.0f; stage = Stage::Idle; }
            break;
    }

    const bool sounding = stage == Stage::Attack || stage == Stage::Sustain || stage == Stage::Release;

    // Pitch movement, in cents
    double cents = 0.0;

    if (scoopT < params[kScoopTime] * 0.001)
    {
        cents += scoopStart * (1.0 - raisedCosine (scoopT / (params[kScoopTime] * 0.001)));
        scoopT += dt;
    }

    if (sagActive && sounding) cents += params[kDriftSag] * noteAge;
    if (sounding) noteAge += dt;

    const float link = breathMode ? params[kDriftBreathLink] * breath : 0.0f;
    cents += drift.next (rng, params[kDriftSpeed], sr) * params[kDriftAmount] * (1.0f + 2.0f * link);
    cents += link * 8.0f; // hard breath pushes slightly sharp
    cents += wobbleP.next (rng, params[kWobbleRate], sr) * params[kWobblePitch];

    if (gliding)
    {
        glideT += dt;
        double p = std::clamp (glideT / glideDur, 0.0, 1.0);
        const double c = params[kGlideCurve];
        p = c >= 0.0 ? 1.0 - std::pow (1.0 - p, 1.0 + 3.0 * c) : std::pow (p, 1.0 - 3.0 * c);
        basePitch = glideFrom + (glideTo - glideFrom) * p;
        if (glideT >= glideDur) { basePitch = glideTo; gliding = false; }
    }

    const double f0 = std::min (semisToHz (basePitch + bendSemis + cents / 100.0), sr * 0.45);

    // Loudness
    float amp = env * (breathMode ? std::pow (breath, 1.4f) : velGain);
    amp *= dbToGain (wobbleL.next (rng, params[kWobbleRate] * 1.13f, sr) * params[kWobbleLevel]);

    // Voice source: whistle plus overtones set by purity, second voice, breath noise.
    float x = 0.0f;
    if (sounding || burst > 1.0e-5f)
    {
        phase += kTwoPi * f0 / sr;
        if (phase >= kTwoPi) phase -= kTwoPi;

        const float h = dbToGain (-12.0f - 48.0f * purity);
        float tone = (float) std::sin (phase);
        if (2.0 * f0 < sr * 0.45) tone += h * (float) std::sin (2.0 * phase);
        if (3.0 * f0 < sr * 0.45) tone += 0.7f * h * (float) std::sin (3.0 * phase);

        const float blend = params[kSecondVoice];
        if (blend > 0.0f)
        {
            const double f2 = std::min (f0 * std::exp2 (params[kSecondInterval] / 12.0), sr * 0.45);
            phase2 += kTwoPi * f2 / sr;
            if (phase2 >= kTwoPi) phase2 -= kTwoPi;
            tone += blend * (float) std::sin (phase2);
        }

        const float noise = rng.uniform();
        tone += dbToGain (-30.0f - 40.0f * purity) * noise;

        x = amp * tone + burst * noise;
        burst *= burstDecay;
    }

    // Beak and throat: a bandpass whose centre follows the pitch.
    const double fixedHz = kBeakFixedHz;
    const double fc = std::min (std::exp (std::log (fixedHz) + (std::log (f0) - std::log (fixedHz)) * params[kBeakTracking]), sr * 0.45);
    const float q = 8.0f + (1.0f - 8.0f) * openness;
    const float g = (float) std::tan (kPi * fc / sr);
    const float k = 1.0f / q;
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    float y = k * v1; // unity gain at the centre

    // Intro chirp: a quick upsweep with a fainter lower voice, then a downslur into the note.
    if (chirpOn)
    {
        const double total = kChirpUp + kChirpDown;
        const double peak = params[kChirpInterval];
        const double start = peak - params[kChirpSweep];
        const double off = chirpT < kChirpUp ? start + (peak - start) * raisedCosine (chirpT / kChirpUp)
                                             : peak * (1.0 - raisedCosine ((chirpT - kChirpUp) / kChirpDown));
        const double fc1 = std::min (chirpBaseHz * std::exp2 (off / 12.0), sr * 0.45);
        chirpPhase += kTwoPi * fc1 / sr;
        chirpLowPhase += kTwoPi * fc1 * kChirpLowRatio / sr;
        const float win = (float) std::sin (kPi * chirpT / total);
        y += chirpGain * win * win
             * ((float) std::sin (chirpPhase) + dbToGain (params[kChirpLowLevel]) * (float) std::sin (chirpLowPhase));
        chirpT += dt;
        if (chirpT >= total) chirpOn = false;
    }

    return y * kBaseLevel * volGain;
}

void BirdEngine::process (float* outL, float* outR, int numSamples)
{
    // Work in chunks no larger than prepare() promised, so the audio thread never allocates.
    const int chunk = (int) dryBuf.size();
    for (int start = 0; start < numSamples; start += chunk)
    {
        const int len = std::min (chunk, numSamples - start);
        for (int n = 0; n < len; ++n)
        {
            const float s = renderSample();
            dryBuf[(size_t) n] = s;
            outL[start + n] = s;
            outR[start + n] = s;
        }
        echo.process (dryBuf.data(), outL + start, outR + start, len);
    }
}

} // namespace pbv
