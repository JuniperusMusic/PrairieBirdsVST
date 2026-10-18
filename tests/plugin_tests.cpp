// Plugin-level tests: drive the real processor with MIDI and check MIDI learn,
// switches, saved mappings, the nanoKONTROL2 map and preset loading.
#include "PluginProcessor.h"

#include <iostream>

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
        if (! ok) ++failures;
    }

    float value (PrairieBirdProcessor& p, int index)
    {
        return p.state.getRawParameterValue (pbv::paramDef (index).id)->load();
    }

    // One block with the given controller messages, then hand the results to the "host".
    void sendCC (PrairieBirdProcessor& p, std::initializer_list<std::pair<int, int>> ccs)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        int t = 0;
        for (auto [cc, v] : ccs) midi.addEvent (juce::MidiMessage::controllerEvent (1, cc, v), t++);
        p.processBlock (buffer, midi);
        p.flushMidiToHost();
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    PrairieBirdProcessor p;
    p.prepareToPlay (48000.0, 256);

    std::cout << "MIDI learn\n";
    p.startMidiLearn (pbv::kTrillOn);
    check (p.getLearningParam() == pbv::kTrillOn, "learning the Trill switch");
    sendCC (p, { { 32, 127 } });
    check (p.ccForParam (pbv::kTrillOn) == 32, "CC 32 assigned to Trill");
    check (p.getLearningParam() < 0, "learning ends after one controller");
    check (value (p, pbv::kTrillOn) >= 0.5f, "the learning press switches Trill on");
    sendCC (p, { { 32, 0 } });
    check (value (p, pbv::kTrillOn) >= 0.5f, "releasing the button leaves it on");
    sendCC (p, { { 32, 127 }, { 32, 0 } });
    check (value (p, pbv::kTrillOn) < 0.5f, "the next press switches it off");

    p.startMidiLearn (pbv::kPurity);
    sendCC (p, { { 2, 90 } });
    check (p.ccForParam (pbv::kPurity) < 0, "CC2 (breath) is never assigned");
    check (p.getLearningParam() == pbv::kPurity, "still waiting for another controller");
    sendCC (p, { { 7, 64 } });
    const float expected = p.state.getParameter ("purity")->convertFrom0to1 (64.0f / 127.0f);
    check (p.ccForParam (pbv::kPurity) == 7, "CC 7 assigned to Purity");
    check (std::abs (value (p, pbv::kPurity) - expected) < 0.01f, "a knob or fader sets the value across its range");

    p.startMidiLearn (pbv::kVolume);
    sendCC (p, { { 7, 10 } });
    check (p.ccForParam (pbv::kVolume) == 7 && p.ccForParam (pbv::kPurity) < 0, "learning a CC moves it to the new setting");
    p.forgetMidi (pbv::kVolume);
    check (p.ccForParam (pbv::kVolume) < 0, "Forget removes the mapping");

    std::cout << "Saved with the project\n";
    p.startMidiLearn (pbv::kTrillOn);
    sendCC (p, { { 40, 0 } });
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    PrairieBirdProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    check (restored.ccForParam (pbv::kTrillOn) == 40, "the mapping comes back with the project");
    check (std::abs (value (restored, pbv::kPurity) - value (p, pbv::kPurity)) < 1.0e-4f, "settings come back too");

    std::cout << "nanoKONTROL2 map\n";
    p.loadNanoKontrolMap();
    check (p.ccForParam (pbv::kPurity) == 0 && p.ccForParam (pbv::kOnset) == 16, "faders on CC 0-7, knobs on CC 16-23");
    check (p.ccForParam (pbv::kBeakTracking) < 0, "fader 3 (CC2) stays free for the breath controller");
    const float octave = value (p, pbv::kOctave);
    sendCC (p, { { 59, 127 }, { 59, 0 } });
    check (value (p, pbv::kOctave) == octave + 1.0f, "Track > raises the octave");
    sendCC (p, { { 58, 127 }, { 58, 0 } });
    check (value (p, pbv::kOctave) == octave, "Track < lowers it");

    std::cout << "Presets\n";
    p.state.getParameter ("trillOn")->setValueNotifyingHost (1.0f);
    p.setCurrentProgram (1);
    check (value (p, pbv::kTrillOn) >= 0.5f, "loading a bird leaves the Trill switch alone");
    check (value (p, pbv::kOctave) == 3.0f, "the sparrow preset opens at octave +3");

    std::cout << (failures == 0 ? "All plugin tests passed\n" : "Plugin tests FAILED\n");
    return failures == 0 ? 0 : 1;
}
