#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "PresetData.h"

#include <cmath>

namespace
{
    juce::String unitSuffix (const pbv::ParamDef& def)
    {
        return juce::String (def.unit).isEmpty() ? juce::String() : " " + juce::String (def.unit);
    }

    // Times and rates feel natural on a log-like knob; everything else is linear.
    bool wantsSkew (const pbv::ParamDef& def)
    {
        const juce::String u (def.unit);
        return def.min > 0.0f && (u == "ms" || u == "Hz" || u == "s");
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout PrairieBirdProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < pbv::kNumParams; ++i)
    {
        const auto& def = pbv::paramDef (i);
        const juce::ParameterID id { def.id, 1 };

        switch (def.kind)
        {
            case pbv::Kind::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (id, def.name, def.def >= 0.5f));
                break;

            case pbv::Kind::Int:
                layout.add (std::make_unique<juce::AudioParameterInt> (id, def.name, (int) def.min, (int) def.max, (int) def.def,
                                                                       juce::AudioParameterIntAttributes().withLabel (def.unit)));
                break;

            case pbv::Kind::Choice:
                layout.add (std::make_unique<juce::AudioParameterChoice> (id, def.name,
                                                                          juce::StringArray::fromTokens (def.choices, "|", ""),
                                                                          (int) def.def));
                break;

            case pbv::Kind::Float:
            {
                juce::NormalisableRange<float> range (def.min, def.max);
                if (wantsSkew (def))
                    range.setSkewForCentre (std::sqrt (def.min * def.max));
                const auto suffix = unitSuffix (def);
                // Wide ranges read in whole numbers (83 ms), narrow ones with decimals (0.79).
                const float span = def.max - def.min;
                const int decimals = span >= 50.0f ? 0 : span >= 5.0f ? 1 : 2;
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    id, def.name, range, def.def,
                    juce::AudioParameterFloatAttributes()
                        .withLabel (def.unit)
                        .withStringFromValueFunction ([suffix, decimals] (float v, int) { return juce::String (v, decimals) + suffix; })));
                break;
            }
        }
    }
    return layout;
}

PrairieBirdProcessor::PrairieBirdProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "PrairieBirdVoice", createLayout())
{
    for (int i = 0; i < pbv::kNumParams; ++i)
    {
        raw.push_back (state.getRawParameterValue (pbv::paramDef (i).id));
        params.push_back (state.getParameter (pbv::paramDef (i).id));
        pending[(size_t) i] = 0.0f;
        pendingFlag[(size_t) i] = false;
    }
    for (auto& c : ccMap) c = -1;
    loadFactoryPresets();
    startTimerHz (30);
}

PrairieBirdProcessor::~PrairieBirdProcessor()
{
    stopTimer();
}

// ---------------------------------------------------------------- MIDI learn

int PrairieBirdProcessor::ccForParam (int paramIndex) const
{
    for (int cc = 0; cc < 128; ++cc)
        if (ccMap[(size_t) cc].load() == paramIndex) return cc;
    return -1;
}

void PrairieBirdProcessor::assignCC (int cc, int target)
{
    for (auto& c : ccMap)
        if (c.load() == target) c = -1; // one controller per setting
    ccMap[(size_t) cc] = target;
}

void PrairieBirdProcessor::forgetMidi (int paramIndex)
{
    for (auto& c : ccMap)
        if (c.load() == paramIndex) c = -1;
}

void PrairieBirdProcessor::clearMidiMap()
{
    for (auto& c : ccMap) c = -1;
}

// The design doc's default map for a nanoKONTROL2 in plain CC mode. Fader 3
// sends CC2, the breath controller's message, so it stays unassigned.
void PrairieBirdProcessor::loadNanoKontrolMap()
{
    clearMidiMap();
    const int faders[8] = { pbv::kPurity, pbv::kSecondVoice, -1 /* CC2 = breath */, pbv::kGlideAmount,
                            pbv::kWarbleDepth, pbv::kTrillGap, pbv::kDriftAmount, pbv::kVolume };
    const int knobs[8] = { pbv::kOnset, pbv::kScoopDepth, pbv::kBreathBurst, pbv::kGlideTime,
                           pbv::kWarbleRate, pbv::kTrillRate, pbv::kDriftSpeed, pbv::kBeakOpen };
    for (int i = 0; i < 8; ++i)
    {
        if (faders[i] >= 0) ccMap[(size_t) i] = faders[i];   // faders: CC 0-7
        ccMap[(size_t) (16 + i)] = knobs[i];                 // knobs: CC 16-23
    }
    ccMap[58] = kActionOctaveDown;   // Track <
    ccMap[59] = kActionOctaveUp;     // Track >
    ccMap[46] = pbv::kSyncMode;      // Cycle
    ccMap[60] = kActionTap;          // Marker Set
}

// Called on the audio thread: the value takes effect in this block, and the
// timer passes it to the host so the panel and automation follow.
void PrairieBirdProcessor::setFromMidi (int paramIndex, float value)
{
    pending[(size_t) paramIndex] = value;
    pendingFlag[(size_t) paramIndex] = true;
    values[paramIndex] = value;
    engine.setParams (values);
}

void PrairieBirdProcessor::handleController (int cc, int value)
{
    const int prev = lastCC[(size_t) cc];
    lastCC[(size_t) cc] = value;

    if (cc == 2 || cc >= 120) { engine.controller (cc, value); return; } // breath and channel mode

    if (const int learn = learnParam.load(); learn >= 0)
    {
        assignCC (cc, learn);
        learnParam = -1;
    }

    const int target = ccMap[(size_t) cc].load();
    if (target < 0) { engine.controller (cc, value); return; }

    const bool pressed = value >= 64 && prev < 64; // a button press (or a fader crossing halfway up)
    if (target == kActionOctaveDown || target == kActionOctaveUp)
    {
        if (pressed)
        {
            const auto& def = pbv::paramDef (pbv::kOctave);
            const float step = target == kActionOctaveUp ? 1.0f : -1.0f;
            setFromMidi (pbv::kOctave, juce::jlimit (def.min, def.max, values[pbv::kOctave] + step));
        }
        return;
    }
    if (target == kActionTap || target == pbv::kTapTempo)
    {
        if (pressed) engine.tapTempo();
        return;
    }
    if (target < 0 || target >= pbv::kNumParams) return;

    const auto& def = pbv::paramDef (target);
    const bool isSwitch = def.kind == pbv::Kind::Bool || (def.kind == pbv::Kind::Choice && def.max <= 1.0f);
    if (isSwitch)
    {
        if (pressed) setFromMidi (target, values[target] >= 0.5f ? def.min : def.max);
        return;
    }
    setFromMidi (target, params[(size_t) target]->convertFrom0to1 (value / 127.0f));
}

void PrairieBirdProcessor::timerCallback()
{
    for (int i = 0; i < pbv::kNumParams; ++i)
        if (pendingFlag[(size_t) i].exchange (false))
        {
            auto* p = params[(size_t) i];
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (pending[(size_t) i].load()));
            p->endChangeGesture();
        }
}

void PrairieBirdProcessor::loadFactoryPresets()
{
    // Init first, then the birds.
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < PresetData::namedResourceListSize; ++i)
        {
            const juce::String file (PresetData::originalFilenames[i]);
            if ((file == "init.json") != (pass == 0)) continue;

            int size = 0;
            const char* data = PresetData::getNamedResource (PresetData::namedResourceList[i], size);
            pbv::Preset p;
            std::string error;
            if (data != nullptr && pbv::parsePreset (std::string (data, (size_t) size), p, error))
                factory.push_back (std::move (p));
            else
                jassertfalse; // a factory preset failed to parse: check presets/*.json
        }
}

void PrairieBirdProcessor::applyPreset (const pbv::Preset& preset)
{
    for (int i = 0; i < pbv::kNumParams; ++i)
    {
        // Breath mode and the breath response follow the player's setup, not the bird.
        if (pbv::isPlayerSetting (i)) continue;
        auto* p = params[(size_t) i];
        p->setValueNotifyingHost (p->convertTo0to1 (preset.values[i]));
    }
}

void PrairieBirdProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) factory.size())) return;
    currentProgram = index;
    applyPreset (factory[(size_t) index]);
}

const juce::String PrairieBirdProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) factory.size()) ? juce::String (factory[(size_t) index].name) : juce::String();
}

bool PrairieBirdProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void PrairieBirdProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    for (int i = 0; i < pbv::kNumParams; ++i) values[i] = raw[(size_t) i]->load();
    engine.setParams (values);
    engine.prepare (sampleRate, samplesPerBlock);
    scratch.assign ((size_t) samplesPerBlock, 0.0f);
}

void PrairieBirdProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
    else if (m.isController()) handleController (m.getControllerNumber(), m.getControllerValue());
    else if (m.isPitchWheel()) engine.pitchBend (m.getPitchWheelValue());
    else if (m.isChannelPressure()) engine.channelPressure (m.getChannelPressureValue());
    else if (m.isAftertouch()) engine.channelPressure (m.getAfterTouchValue()); // poly pressure: mono voice, treat as channel
    else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
}

void PrairieBirdProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (int i = 0; i < pbv::kNumParams; ++i)
        values[i] = pendingFlag[(size_t) i].load() ? pending[(size_t) i].load() : raw[(size_t) i]->load();
    engine.setParams (values);

    // Learn breath range: switching it on starts recording CC2; switching it off sets floor and ceiling.
    const bool learning = values[pbv::kBreathLearn] >= 0.5f;
    if (learning && ! wasLearning)
        engine.beginBreathLearn();
    else if (! learning && wasLearning)
    {
        int lo = 0, hi = 127;
        if (engine.endBreathLearn (lo, hi))
        {
            learnedFloor = lo;
            learnedCeiling = hi;
            triggerAsyncUpdate();
        }
    }
    wasLearning = learning;

    // Tap tempo: each switch-on is one tap. Cubase's tempo feeds Sync when it is the tempo source.
    const bool tapping = values[pbv::kTapTempo] >= 0.5f;
    if (tapping && ! wasTapping) engine.tapTempo();
    wasTapping = tapping;

    double bpm = 0.0;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
            if (auto hostBpm = pos->getBpm())
                bpm = *hostBpm;
    engine.setHostTempo (bpm);

    const int numSamples = buffer.getNumSamples();
    const bool stereo = buffer.getNumChannels() > 1;
    float* left = buffer.getWritePointer (0);
    float* right = stereo ? buffer.getWritePointer (1) : nullptr;

    // Mono output: render the right side into scratch space and sum.
    if (! stereo)
    {
        if ((int) scratch.size() < numSamples) scratch.resize ((size_t) numSamples); // only if the host exceeds its promised block size
        right = scratch.data();
    }

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        if (at > pos) engine.process (left + pos, right + pos, at - pos);
        pos = juce::jmax (pos, at);
        handleMidi (meta.getMessage());
    }
    if (pos < numSamples) engine.process (left + pos, right + pos, numSamples - pos);

    if (! stereo)
        for (int n = 0; n < numSamples; ++n) left[n] = 0.5f * (left[n] + right[n]);

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, numSamples);
    breathMeter = engine.breathLevel();
}

void PrairieBirdProcessor::handleAsyncUpdate()
{
    const int lo = learnedFloor.exchange (-1), hi = learnedCeiling.exchange (-1);
    if (lo < 0 || hi < 0) return;
    for (auto [index, value] : { std::pair { (int) pbv::kBreathFloor, lo }, std::pair { (int) pbv::kBreathCeiling, hi } })
    {
        auto* p = params[(size_t) index];
        p->setValueNotifyingHost (p->convertTo0to1 ((float) value));
    }
}

juce::AudioProcessorEditor* PrairieBirdProcessor::createEditor()
{
    return new PrairieBirdEditor (*this);
}

void PrairieBirdProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    copy.setProperty ("program", currentProgram, nullptr);

    juce::ValueTree map ("MidiMap");
    for (int cc = 0; cc < 128; ++cc)
    {
        const int t = ccMap[(size_t) cc].load();
        if (t < 0) continue;
        juce::String target = t == kActionOctaveDown ? "octaveDown" : t == kActionOctaveUp ? "octaveUp"
                            : t == kActionTap ? "tap" : juce::String (pbv::paramDef (t).id);
        map.appendChild (juce::ValueTree ("Map", { { "cc", cc }, { "target", target } }), nullptr);
    }
    copy.appendChild (map, nullptr);
    if (auto xml = copy.createXml()) copyXmlToBinary (*xml, destData);
}

void PrairieBirdProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram = tree.getProperty ("program", 0);

            if (auto map = tree.getChildWithName ("MidiMap"); map.isValid())
            {
                clearMidiMap();
                for (const auto& m : map)
                {
                    const int cc = m.getProperty ("cc", -1);
                    const juce::String target = m.getProperty ("target").toString();
                    int t = target == "octaveDown" ? kActionOctaveDown : target == "octaveUp" ? kActionOctaveUp
                          : target == "tap" ? kActionTap : pbv::findParam (target.toRawUTF8());
                    if (juce::isPositiveAndBelow (cc, 128) && t >= 0) ccMap[(size_t) cc] = t;
                }
                tree.removeChild (map, nullptr);
            }
            state.replaceState (tree);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PrairieBirdProcessor(); }
