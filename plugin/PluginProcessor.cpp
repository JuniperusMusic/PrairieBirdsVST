#include "PluginProcessor.h"

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
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    id, def.name, range, def.def,
                    juce::AudioParameterFloatAttributes()
                        .withLabel (def.unit)
                        .withStringFromValueFunction ([suffix] (float v, int) { return juce::String (v, 2) + suffix; })));
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
    }
    loadFactoryPresets();
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
    else if (m.isController()) engine.controller (m.getControllerNumber(), m.getControllerValue());
    else if (m.isPitchWheel()) engine.pitchBend (m.getPitchWheelValue());
    else if (m.isChannelPressure()) engine.channelPressure (m.getChannelPressureValue());
    else if (m.isAftertouch()) engine.channelPressure (m.getAfterTouchValue()); // poly pressure: mono voice, treat as channel
    else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
}

void PrairieBirdProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (int i = 0; i < pbv::kNumParams; ++i) values[i] = raw[(size_t) i]->load();
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
    // A plain test panel until build stage 9 (the Retrologue-style panel).
    return new juce::GenericAudioProcessorEditor (*this);
}

void PrairieBirdProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    copy.setProperty ("program", currentProgram, nullptr);
    if (auto xml = copy.createXml()) copyXmlToBinary (*xml, destData);
}

void PrairieBirdProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram = tree.getProperty ("program", 0);
            state.replaceState (tree);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PrairieBirdProcessor(); }
