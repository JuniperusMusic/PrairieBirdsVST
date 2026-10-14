// Prairie Bird Voice — parameter table.
//
// This is the single source of truth for every setting a preset stores. The
// plugin builds its host parameters from it, the render tool reads presets
// against it, and the analysis scripts read the same ids from preset JSON.
// Never renumber or rename an id once presets exist: ids are stored in
// presets and Cubase projects.
#pragma once

namespace pbv
{

enum class Section { Voice, Attack, Beak, Glide, Drift, Motion, Tuning, Space };
enum class Kind { Float, Int, Bool, Choice };

struct ParamDef
{
    const char* id;
    const char* name;
    Section section;
    Kind kind;
    float min, max, def;
    const char* unit;
    const char* choices; // '|' separated, Choice only
    bool onPanel;        // false = preset-level setting, edited by the matching loop
};

// X(Enum, id, name, section, kind, min, max, default, unit, choices, onPanel)
// Defaults are the "Init" sound: equal temperament, plain whistle.
#define PBV_PARAMS(X) \
    /* Voice */ \
    X(BreathMode,     "breathMode",     "Breath mode",          Voice,  Bool,   0,    1,    0,     "",      "",  true)  \
    X(Octave,         "octave",         "Octave",               Voice,  Int,   -3,    3,   -2,     "oct",   "",  true)  \
    X(Purity,         "purity",         "Purity",               Voice,  Float,  0,    1,    0.8f,  "",      "",  true)  \
    X(SecondVoice,    "secondVoice",    "Second voice",         Voice,  Float,  0,    1,    0,     "",      "",  true)  \
    X(SecondInterval, "secondInterval", "Second voice interval",Voice,  Float, -24,   24,  -12,    "st",    "",  true)  \
    X(FineTune,       "fineTune",       "Fine tune",            Voice,  Float, -100,  100,  0,     "ct",    "",  false) \
    X(Volume,         "volume",         "Volume",               Voice,  Float, -60,   6,   -6,     "dB",    "",  true)  \
    X(BendRange,      "bendRange",      "Pitch bend range",     Voice,  Int,    0,    12,   2,     "st",    "",  false) \
    X(BreathEdge,     "breathEdge",     "Breath edge",          Voice,  Float,  0,    1,    0.3f,  "",      "",  false) \
    /* Attack */ \
    X(Onset,          "onset",          "Onset",                Attack, Float,  1,    500,  83,    "ms",    "",  true)  \
    X(Release,        "release",        "Release",              Attack, Float,  2,    1000, 10,    "ms",    "",  true)  \
    X(ScoopDepth,     "scoopDepth",     "Scoop depth",          Attack, Float,  0,    200,  48,    "ct",    "",  true)  \
    X(ScoopDir,       "scoopDir",       "Scoop direction",      Attack, Choice, 0,    2,    0,     "",      "Auto|From below|From above", true) \
    X(ScoopTime,      "scoopTime",      "Scoop time",           Attack, Float,  5,    300,  70,    "ms",    "",  false) \
    X(ChirpThreshold, "chirpThreshold", "Chirp threshold",      Attack, Int,    1,    128,  110,   "vel",   "",  true)  \
    X(ChirpInterval,  "chirpInterval",  "Chirp interval",       Attack, Float, -24,   24,   1.49f, "st",    "",  true)  \
    X(ChirpLevel,     "chirpLevel",     "Chirp level",          Attack, Float, -40,   0,   -6,     "dB",    "",  true)  \
    X(ChirpSweep,     "chirpSweep",     "Chirp sweep",          Attack, Float,  0,    24,   9.23f, "st",    "",  false) \
    X(ChirpLead,      "chirpLead",      "Chirp lead",           Attack, Float,  0,    400,  180,   "ms",    "",  false) \
    X(ChirpLowLevel,  "chirpLowLevel",  "Chirp lower voice",    Attack, Float, -60,   0,   -15,    "dB",    "",  false) \
    X(BreathBurst,    "breathBurst",    "Breath burst",         Attack, Float,  0,    1,    0.1f,  "",      "",  true)  \
    X(VelResponse,    "velResponse",    "Velocity response",    Attack, Float,  0,    1,    0.6f,  "",      "",  true)  \
    /* Beak */ \
    X(BeakTracking,   "beakTracking",   "Beak tracking",        Beak,   Float,  0,    1,    0.9f,  "",      "",  true)  \
    X(BeakOpen,       "beakOpen",       "Beak openness",        Beak,   Float,  0,    1,    0.5f,  "",      "",  true)  \
    X(BeakModSource,  "beakModSource",  "Openness follows",     Beak,   Choice, 0,    3,    0,     "",      "None|Breath|Velocity|Aftertouch", true) \
    X(BeakModAmount,  "beakModAmount",  "Openness amount",      Beak,   Float, -1,    1,    0,     "",      "",  true)  \
    /* Glide */ \
    X(GlideAmount,    "glideAmount",    "Glide amount",         Glide,  Float,  0,    1,    0.1f,  "",      "",  true)  \
    X(GlideTime,      "glideTime",      "Glide time",           Glide,  Float,  5,    1000, 60,    "ms",    "",  true)  \
    X(GlideCurve,     "glideCurve",     "Glide curve",          Glide,  Float, -1,    1,    0,     "",      "",  true)  \
    /* Drift */ \
    X(DriftAmount,    "driftAmount",    "Drift amount",         Drift,  Float,  0,    50,   2,     "ct",    "",  true)  \
    X(DriftSpeed,     "driftSpeed",     "Drift speed",          Drift,  Float,  0.05f,10,   0.5f,  "Hz",    "",  true)  \
    X(DriftBreathLink,"driftBreathLink","Drift breath link",    Drift,  Float,  0,    1,    0.5f,  "",      "",  true)  \
    X(DriftSag,       "driftSag",       "First-note sag",       Drift,  Float, -100,  100,  0,     "ct/s",  "",  false) \
    X(WobblePitch,    "wobblePitch",    "Wobble pitch",         Drift,  Float,  0,    20,   3.5f,  "ct",    "",  false) \
    X(WobbleLevel,    "wobbleLevel",    "Wobble level",         Drift,  Float,  0,    6,    1,     "dB",    "",  false) \
    X(WobbleRate,     "wobbleRate",     "Wobble rate",          Drift,  Float,  2,    40,   17,    "Hz",    "",  false) \
    X(PhraseGap,      "phraseGap",      "Phrase gap",           Drift,  Float,  50,   2000, 400,   "ms",    "",  false) \
    /* Motion (stage 6: stored and automatable now, sounding later) */ \
    X(WarbleRate,     "warbleRate",     "Warble rate",          Motion, Float,  0.5f, 40,   8,     "Hz",    "",  true)  \
    X(WarbleDepth,    "warbleDepth",    "Warble depth",         Motion, Float,  0,    200,  0,     "ct",    "",  true)  \
    X(TrillRate,      "trillRate",      "Trill rate",           Motion, Float,  2,    40,   12,    "Hz",    "",  true)  \
    X(TrillDepth,     "trillDepth",     "Trill depth",          Motion, Float,  0,    1,    0,     "",      "",  true)  \
    X(TrillPattern,   "trillPattern",   "Trill pattern",        Motion, Choice, 0,    2,    0,     "",      "Off|Doublet|Triplet", true) \
    X(FlurryDensity,  "flurryDensity",  "Flurry density",       Motion, Float,  0,    1,    0,     "",      "",  true)  \
    X(FlurrySpread,   "flurrySpread",   "Flurry spread",        Motion, Float,  0,    1200, 300,   "ct",    "",  true)  \
    X(SyncMode,       "syncMode",       "Free/Sync",            Motion, Choice, 0,    1,    0,     "",      "Free|Sync", true) \
    X(SyncDivision,   "syncDivision",   "Sync division",        Motion, Choice, 0,    5,    3,     "",      "1/4|1/8|1/8T|1/16|1/16T|1/32", true) \
    X(TempoSource,    "tempoSource",    "Tempo source",         Motion, Choice, 0,    2,    0,     "",      "Cubase|Typed|Tap", true) \
    X(TypedBpm,       "typedBpm",       "Typed BPM",            Motion, Float,  30,   300,  120,   "bpm",   "",  true)  \
    /* Tuning: 12 offsets in cents from equal temperament, counted up from the root */ \
    X(TuneRoot,       "tuneRoot",       "Root note",            Tuning, Choice, 0,    11,   0,     "",      "C|C#|D|D#|E|F|F#|G|G#|A|A#|B", true) \
    X(Tune0,          "tune0",          "Root",                 Tuning, Float, -100,  100,  0,     "ct",    "",  true)  \
    X(Tune1,          "tune1",          "Root +1",              Tuning, Float, -100,  100,  0,     "ct",    "",  true)  \
    X(Tune2,          "tune2",          "Root +2",              Tuning, Float, -100,  100,  0,     "ct",    "",  true)  \
    X(Tune3,          "tune3",          "Root +3",              Tuning, Float, -100,  100,  0,     "ct",    "",  true)  \
    X(Tune4,          "tune4",          "Root +4",              Tuning, Float, -100,  100,  0,     "ct",    "",  true)  \
    X(Tune5,          "tune5",          "Root +5",              Tuning, Float, -100,  100,  0,     "ct",    "",  true)  \
    X(Tune6,          "tune6",          "Root +6",              Tuning, Float, -100,  100,  0,     "ct",    "",  true)  \
    X(Tune7,          "tune7",          "Root +7",              Tuning, Float, -100,  100,  0,     "ct",    "",  true)  \
    X(Tune8,          "tune8",          "Root +8",              Tuning, Float, -100,  100,  0,     "ct",    "",  true)  \
    X(Tune9,          "tune9",          "Root +9",              Tuning, Float, -100,  100,  0,     "ct",    "",  true)  \
    X(Tune10,         "tune10",         "Root +10",             Tuning, Float, -100,  100,  0,     "ct",    "",  true)  \
    X(Tune11,         "tune11",         "Root +11",             Tuning, Float, -100,  100,  0,     "ct",    "",  true)  \
    /* Space */ \
    X(EchoWet,        "echoWet",        "Forest echo",          Space,  Float,  0,    1,    0,     "",      "",  true)  \
    X(EchoDecay,      "echoDecay",      "Echo decay",           Space,  Float,  0.1f, 5,    1,     "s",     "",  false)

enum ParamIndex : int
{
#define PBV_ENUM(e, ...) k##e,
    PBV_PARAMS (PBV_ENUM)
#undef PBV_ENUM
    kNumParams
};

inline const ParamDef& paramDef (int index)
{
    static const ParamDef defs[] = {
#define PBV_DEF(e, id, name, sec, kind, mn, mx, df, unit, ch, panel) \
        { id, name, Section::sec, Kind::kind, (float) (mn), (float) (mx), (float) (df), unit, ch, panel },
        PBV_PARAMS (PBV_DEF)
#undef PBV_DEF
    };
    return defs[index];
}

// Returns -1 if the id is unknown.
int findParam (const char* id);

// A full set of parameter values, indexed by ParamIndex.
struct ParamValues
{
    float v[kNumParams];

    ParamValues() { for (int i = 0; i < kNumParams; ++i) v[i] = paramDef (i).def; }
    float operator[] (int i) const { return v[i]; }
    float& operator[] (int i) { return v[i]; }
};

} // namespace pbv
