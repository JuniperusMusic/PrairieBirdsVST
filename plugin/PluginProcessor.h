#pragma once

#include "BirdEngine.h"
#include "Preset.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

class PrairieBirdProcessor : public juce::AudioProcessor,
                             private juce::AsyncUpdater
{
public:
    PrairieBirdProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Prairie Bird Voice"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    // Factory presets appear as programs in Cubase's preset menu.
    int getNumPrograms() override { return (int) factory.size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState state;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void loadFactoryPresets();
    void applyPreset (const pbv::Preset& preset);
    void handleMidi (const juce::MidiMessage& m);
    void handleAsyncUpdate() override;

    pbv::BirdEngine engine;
    pbv::ParamValues values;
    std::vector<std::atomic<float>*> raw;
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<pbv::Preset> factory;
    std::vector<float> scratch;
    int currentProgram = 0;

    // Learn breath range: the audio thread records it, the message thread writes the sliders.
    bool wasLearning = false;
    std::atomic<int> learnedFloor { -1 }, learnedCeiling { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrairieBirdProcessor)
};
