// Prairie Bird Voice — the panel: eight sections in signal order, like
// Cubase's Retrologue, with only the controls bird voices need. Every
// control can be MIDI-learned from its right-click menu.
#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace pbvui
{
    namespace colours
    {
        const juce::Colour ground   { 0xff171918 };
        const juce::Colour panel    { 0xff222523 };
        const juce::Colour edge     { 0xff333733 };
        const juce::Colour control  { 0xff2c302d };
        const juce::Colour track    { 0xff3a3f3b };
        const juce::Colour ink      { 0xffece8dc };
        const juce::Colour quiet    { 0xffb4b6ac };
        const juce::Colour wheat    { 0xffd8a657 };
        const juce::Colour sky      { 0xff7fb0d6 };
        const juce::Colour paper    { 0xffd8d3c4 };
        const juce::Colour graphite { 0xff3f3e3a };
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;
    };

    // Adds a right-click (popup menu) hook to a JUCE control without
    // disturbing its normal left-click behaviour.
    template <class Base>
    class Learnable : public Base
    {
    public:
        using Base::Base;
        std::function<void()> onPopup;

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { if (onPopup) onPopup(); return; }
            Base::mouseDown (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return;
            Base::mouseUp (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return;
            Base::mouseDrag (e);
        }
    };

    // One control with its name above it.
    class Cell : public juce::Component
    {
    public:
        Cell (int paramIndex, std::unique_ptr<juce::Component> control, bool showName, int width, int height);
        void resized() override;

        const int param;
        const int cellWidth, cellHeight;
        juce::Label name;
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<juce::SliderParameterAttachment> sliderAttachment;
        std::unique_ptr<juce::ButtonParameterAttachment> buttonAttachment;
        std::unique_ptr<juce::ComboBoxParameterAttachment> comboAttachment;
        juce::String baseText;
    };

    class BeakSketch : public juce::Component
    {
    public:
        BeakSketch();
        void paint (juce::Graphics&) override;

    private:
        juce::Path strokes;
    };

    class BreathMeter : public juce::Component
    {
    public:
        void setLevel (float l) { if (std::abs (l - level) > 0.002f) { level = l; repaint(); } }
        void paint (juce::Graphics&) override;

    private:
        float level = 0.0f;
    };
}

class PrairieBirdEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit PrairieBirdEditor (PrairieBirdProcessor&);
    ~PrairieBirdEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Section
    {
        juce::String title;
        juce::Rectangle<int> bounds;
        std::vector<juce::Component*> items;
    };

    void timerCallback() override;
    Section& section (const juce::String& title, juce::Rectangle<int> bounds);
    pbvui::Cell& addCell (Section&, std::unique_ptr<pbvui::Cell>);
    void knob (Section&, int param);
    void toggle (Section&, int param, int width = 110);
    void choice (Section&, int param, int width = 120);
    void stepper (Section&, int param);
    void tuneBar (Section&, int degree);
    void tapButton (Section&);
    void showLearnMenu (int param);
    void layoutSection (Section&);
    void refreshLabels();

    PrairieBirdProcessor& proc;
    pbvui::LookAndFeel lnf;

    std::vector<Section> sections;
    std::vector<std::unique_ptr<pbvui::Cell>> cells;
    std::vector<std::unique_ptr<juce::Component>> extras;

    juce::Label title, status;
    juce::ComboBox presets;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, controllerMenu { "Controller" }, allSettings { "All settings" };
    pbvui::BreathMeter meter;
    juce::Label meterLabel;
    juce::TextButton equalButton { "Equal" };
    std::unique_ptr<juce::GenericAudioProcessorEditor> generic;
    juce::Rectangle<int> panelArea;
    int lastProgram = -1, lastLearning = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrairieBirdEditor)
};
