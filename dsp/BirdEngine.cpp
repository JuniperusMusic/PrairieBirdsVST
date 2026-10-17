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
    breathRaw = 0;
    aftertouch = 0.0f;
    dipT = 1.0e9;
    glidePending = false;
    overtone = 1;
    overblowSemis = 0.0;
    breakPending = false;
    warblePhase = trillT = 0.0;
    trillGain = 1.0f;
    flurryCents = flurryTarget = 0.0f;
    flurryHold = 0.0;
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
    breathTarget = shapeBreath (breathRaw); // floor, ceiling or curve may have changed
}

// Breath response: the floor (just above the controller's resting value) reads
// as silence, the ceiling as full breath, and the curve shapes what is between.
float BirdEngine::shapeBreath (int cc) const
{
    const float lo = params[kBreathFloor];
    const float hi = std::max (lo + 1.0f, params[kBreathCeiling]);
    const float x = std::clamp ((cc - lo) / (hi - lo), 0.0f, 1.0f);
    return std::pow (x, params[kBreathCurve]);
}

void BirdEngine::beginBreathLearn()
{
    learning = true;
    learnMin = 127;
    learnMax = 0;
}

bool BirdEngine::endBreathLearn (int& floor, int& ceiling)
{
    learning = false;
    if (learnMax - learnMin < 8) return false; // not enough breath seen to trust
    floor = std::min (learnMin + 2, 125);
    ceiling = std::max (learnMax, floor + 1);
    return true;
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

    const bool connected = breakPending || stage == Stage::Waiting || stage == Stage::Attack || stage == Stage::Sustain;
    if (connected && stackSize > 1)
        legatoTo (note, velocity);
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
    {
        breakPending = false; // nothing left to come back to
        startRelease();
    }
    else if (note == currentNote || (breakPending && note == breakNote))
    {
        legatoTo (stack[stackSize - 1], (int) std::lround (noteVel * 127.0f)); // fall back to the key still held
    }
}

void BirdEngine::controller (int cc, int value)
{
    switch (cc)
    {
        case 2:   // breath controller
            breathRaw = value;
            if (learning) { learnMin = std::min (learnMin, value); learnMax = std::max (learnMax, value); }
            breathTarget = shapeBreath (value);
            break;
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
    breakPending = false;
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
    const bool breathMode = params[kBreathMode] >= 0.5f;
    if (breathMode)
    {
        // The breath shapes the attack; this only stops a note started on
        // strong breath from snapping on faster than the minimum.
        envDur = std::max (0.001, params[kBreathAttack] * 0.001);
    }
    else
    {
        const float resp = params[kVelResponse];
        // Hard hits start faster: velocity 127 halves the onset time at full response.
        const double speed = 1.0 + ((1.5 - noteVel) - 1.0) * resp;
        envDur = std::max (0.001, params[kOnset] * 0.001 * speed);
    }
    envT = 0.0;
    envStart = env;
    stage = Stage::Attack;
    scoopT = 0.0;
    scoopDur = std::max (0.001, params[kScoopTime] * 0.001);
    trillT = 0.0;

    // The burst is a puff of air: in breath mode it follows the breath, not the key.
    burst = params[kBreathBurst] * 0.3f * (breathMode ? breathTarget : velGain);
    burstDecay = (float) std::exp (-1.0 / (0.012 * sr));
}

void BirdEngine::legatoTo (int note, int velocity)
{
    const int mode = (int) params[kLegatoMode]; // 0 Break, 1 Dip, 2 Glide

    // Break: like the bird's gaps between whistles. The current note releases,
    // the break passes in silence, then the new key starts as a fresh note.
    if (mode == 0 || breakPending)
    {
        breakNote = note;
        breakVel = velocity;
        if (! breakPending)
        {
            breakPending = true;
            const bool waiting = stage == Stage::Waiting;
            startRelease();
            breakWait = (waiting ? 0.0 : params[kRelease] * 0.001) + params[kLegatoBreak] * 0.001;
        }
        return;
    }

    currentNote = note;
    const double target = semitonesForKey (note);

    if (stage == Stage::Waiting)
    {
        basePitch = target; // the whistle hasn't started yet
        sagActive = false;
        return;
    }

    // Dip: articulate by dipping the loudness, changing pitch at the quietest point.
    if (mode == 1 && params[kLegatoDip] > 0.0f)
    {
        dipT = 0.0;
        dipDur = std::max (0.005, params[kLegatoDipTime] * 0.001);
        dipFloor = dbToGain (-params[kLegatoDip]);
        pendingTarget = target;
        pendingDelay = dipDur * 0.5;
        glidePending = true;
        return;
    }

    startGlide (target);
}

// The new note after a break: a later note of the phrase, so no chirp and
// (in Auto) a scoop rising in from below.
void BirdEngine::startBreakNote()
{
    breakPending = false;
    currentNote = breakNote;
    noteVel = breakVel / 127.0f;
    velGain = 1.0f + (std::pow (noteVel, 1.5f) - 1.0f) * params[kVelResponse];
    basePitch = semitonesForKey (breakNote);
    gliding = glidePending = false;
    firstInPhrase = false;
    sagActive = false;
    noteAge = 0.0;
    const int dirMode = (int) params[kScoopDir];
    const float dir = dirMode == 2 ? 1.0f : -1.0f;
    scoopStart = dir * params[kScoopDepth] * noteVel;
    startAttack();
}

void BirdEngine::tapTempo()
{
    const double gap = clock - lastTap;
    if (gap > 0.2 && gap < 2.0) // 30-300 BPM; a longer pause starts a new count
        tapBpm = tapBpm > 0.0 ? 0.5 * tapBpm + 0.5 * (60.0 / gap) : 60.0 / gap;
    lastTap = clock;
}

double BirdEngine::motionTempo() const
{
    switch ((int) params[kTempoSource])
    {
        case 0:  if (hostBpm > 0.0) return hostBpm; break; // Cubase
        case 2:  if (tapBpm > 0.0) return tapBpm; break;   // Tap
        default: break;
    }
    return params[kTypedBpm];
}

void BirdEngine::startGlide (double target)
{
    glidePending = false;
    trillT = 0.0;

    // basePitch is where the glide (if any) has reached; carry the first
    // note's sag into the move so the pitch never jumps back.
    double from = basePitch;
    if (sagActive) from += params[kDriftSag] * noteAge / 100.0;
    sagActive = false;
    noteAge = 0.0;

    const float amount = params[kGlideAmount];
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
    clock += dt;

    if (breakPending)
    {
        breakWait -= dt;
        if (breakWait <= 0.0 && stackSize > 0) startBreakNote();
    }

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

    // Legato articulation
    if (glidePending)
    {
        pendingDelay -= dt;
        if (pendingDelay <= 0.0) startGlide (pendingTarget);
    }
    float dipGain = 1.0f;
    if (dipT < dipDur)
    {
        const float bump = 0.5f - 0.5f * (float) std::cos (kTwoPi * dipT / dipDur); // 0 -> 1 -> 0
        dipGain = 1.0f - (1.0f - dipFloor) * bump;
        dipT += dt;
    }

    // Motion. Free: rates in Hz. Sync: warble and trill follow the tempo at the chosen note value.
    double warbleHz = params[kWarbleRate], trillHz = params[kTrillRate];
    if (params[kSyncMode] >= 0.5f)
    {
        static constexpr double beats[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 }; // 1/4 .. 1/32
        const double hz = motionTempo() / 60.0 / beats[std::clamp ((int) params[kSyncDivision], 0, 5)];
        warbleHz = trillHz = hz;
    }

    // Trill: the note re-articulates at the trill rate. Each cycle the note
    // sounds, then falls silent for the trill gap. Every articulation is
    // shaped like the bird's own note: it releases into the gap over the
    // release time and comes back with the onset curve (in breath mode, the
    // breath attack minimum) and a scoop rising in from below, both shortened
    // to fit when the cycle is short. Doublet and Triplet play 2 or 3
    // articulations and hold the last.
    trillGain = 1.0f;
    if (params[kTrillGap] > 0.0f && (stage == Stage::Attack || stage == Stage::Sustain))
    {
        const double period = 1.0 / std::max (0.1, trillHz);
        const double gap = std::min ((double) params[kTrillGap] * 0.001, period * 0.8);
        const double sound = period - gap;                 // when the gap starts in each cycle
        const double release = std::min ((double) params[kRelease] * 0.001, 0.4 * sound);
        const double onsetFull = breathMode ? params[kBreathAttack] * 0.001
                                            : params[kOnset] * 0.001 * (1.0 + ((1.5 - noteVel) - 1.0) * params[kVelResponse]);
        const double attack = std::max (0.001, std::min (onsetFull, 0.5 * sound));
        const int n = (int) std::floor (trillT / period);  // articulation number, from 0
        const double tc = trillT - n * period;
        const int pattern = (int) params[kTrillPattern];   // 0 Continuous, 1 Doublet, 2 Triplet
        const bool last = pattern > 0 && n >= pattern;     // the held final articulation

        double g = 1.0;
        if (n > 0 && (pattern == 0 || n <= pattern) && tc < attack)
            g = raisedCosine (tc / attack);                             // the bird's onset, again
        else if (! last && tc >= sound - release && tc < sound)
            g = 1.0 - raisedCosine ((tc - (sound - release)) / release); // the bird's release
        else if (! last && tc >= sound)
            g = 0.0;                                                    // the gap
        trillGain = (float) g;

        // Each new articulation scoops in from below, like a later note of the phrase.
        if (n > 0 && (pattern == 0 || n <= pattern) && tc < dt * 1.5 && (int) params[kScoopDir] != 2)
        {
            scoopStart = -params[kScoopDepth] * noteVel;
            scoopT = 0.0;
            scoopDur = std::max (0.001, std::min ((double) params[kScoopTime] * 0.001, 0.5 * sound));
        }
        trillT += dt;
    }

    // Flurry: quick random jumps around the note, density events per second up to 12.
    if (params[kFlurryDensity] > 0.0f && sounding)
    {
        if ((rng.uniform() + 1.0f) * 0.5f < params[kFlurryDensity] * 12.0f * (float) dt)
        {
            flurryTarget = params[kFlurrySpread] * rng.uniform();
            flurryHold = 0.06;
        }
        if (flurryHold > 0.0 && (flurryHold -= dt) <= 0.0) flurryTarget = 0.0f;
    }
    else
    {
        flurryTarget = 0.0f;
    }
    flurryCents += (flurryTarget - flurryCents) * (float) (1.0 - std::exp (-dt / 0.004));

    // Overblow: harder breath climbs the overtone series (2x, 3x, 4x, 5x the
    // note), each step at its own threshold; a step back down needs the breath
    // to fall below that threshold by the hysteresis.
    if (breathMode && params[kOverblowOn] >= 0.5f && sounding)
    {
        const float hyst = params[kOverblowHyst];
        while (overtone < 5 && breath > params[kOverblow2 + overtone - 1]) ++overtone;
        while (overtone > 1 && breath < params[kOverblow2 + overtone - 2] - hyst) --overtone;
    }
    else
    {
        overtone = 1;
    }
    {
        const double target = 12.0 * std::log2 ((double) overtone);
        const double tau = std::max (0.0005, params[kOverblowTime] * 0.001 / 3.0);
        overblowSemis += (target - overblowSemis) * (1.0 - std::exp (-dt / tau));
    }

    // Pitch movement, in cents
    double cents = 0.0;

    if (scoopT < scoopDur)
    {
        cents += scoopStart * (1.0 - raisedCosine (scoopT / scoopDur));
        scoopT += dt;
    }

    if (sagActive && sounding) cents += params[kDriftSag] * noteAge;
    if (sounding) noteAge += dt;

    const float link = breathMode ? params[kDriftBreathLink] * breath : 0.0f;
    cents += drift.next (rng, params[kDriftSpeed], sr) * params[kDriftAmount] * (1.0f + 2.0f * link);
    cents += link * 8.0f; // hard breath pushes slightly sharp
    cents += wobbleP.next (rng, params[kWobbleRate], sr) * params[kWobblePitch];

    warblePhase += warbleHz * dt;
    if (warblePhase >= 1.0) warblePhase -= std::floor (warblePhase);
    cents += params[kWarbleDepth] * std::sin (kTwoPi * warblePhase) + flurryCents;

    if (gliding)
    {
        glideT += dt;
        double p = std::clamp (glideT / glideDur, 0.0, 1.0);
        const double c = params[kGlideCurve];
        p = c >= 0.0 ? 1.0 - std::pow (1.0 - p, 1.0 + 3.0 * c) : std::pow (p, 1.0 - 3.0 * c);
        basePitch = glideFrom + (glideTo - glideFrom) * p;
        if (glideT >= glideDur) { basePitch = glideTo; gliding = false; }
    }

    const double f0 = std::min (semisToHz (basePitch + overblowSemis + bendSemis + cents / 100.0), sr * 0.45);

    // Loudness
    float amp = env * dipGain * trillGain * (breathMode ? breath : velGain); // breath is already shaped by the curve
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
