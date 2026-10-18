#pragma once

#include "BirdEngine.h"
#include "Preset.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

class PrairieBirdProcessor : public juce::AudioProcessor,
                             private juce::AsyncUpdater,
                             private juce::Timer
{
public:
    PrairieBirdProcessor();
    ~PrairieBirdProcessor() override;

    // ---- MIDI learn
    // A controller (CC) drives one setting. Learn: the next CC that arrives is
    // assigned to the setting. CC2 (breath) and channel-mode CCs (120-127)
    // are never assigned. Mappings are saved with the project.
    static constexpr int kActionOctaveDown = 1000, kActionOctaveUp = 1001, kActionTap = 1002;
    void startMidiLearn (int paramIndex) { learnParam = paramIndex; }
    void cancelMidiLearn() { learnParam = -1; }
    int getLearningParam() const { return learnParam.load(); }
    int ccForParam (int paramIndex) const;   // -1 if none
    void forgetMidi (int paramIndex);
    void loadNanoKontrolMap();
    void clearMidiMap();

    float getBreathLevel() const { return breathMeter.load(); }

    // Hands MIDI-set values to the host now instead of on the next timer tick (tests).
    void flushMidiToHost() { timerCallback(); }

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
    void timerCallback() override;
    void handleController (int cc, int value);
    void setFromMidi (int paramIndex, float value);
    void assignCC (int cc, int target);

    pbv::BirdEngine engine;
    pbv::ParamValues values;
    std::vector<std::atomic<float>*> raw;
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<pbv::Preset> factory;
    std::vector<float> scratch;
    int currentProgram = 0;

    // Learn breath range: the audio thread records it, the message thread writes the sliders.
    bool wasLearning = false;
    bool wasTapping = false;
    std::atomic<int> learnedFloor { -1 }, learnedCeiling { -1 };

    // MIDI learn state. The audio thread writes values from MIDI into `pending`
    // and uses them at once; the timer then hands them to the host.
    std::array<std::atomic<int>, 128> ccMap;
    std::array<int, 128> lastCC {};
    std::atomic<int> learnParam { -1 };
    std::array<std::atomic<float>, pbv::kNumParams> pending;
    std::array<std::atomic<bool>, pbv::kNumParams> pendingFlag;
    std::atomic<float> breathMeter { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrairieBirdProcessor)
};
