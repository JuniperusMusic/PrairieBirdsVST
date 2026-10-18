#include "PluginEditor.h"

using namespace pbvui;

// ---------------------------------------------------------------- look

LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::ground);
    setColour (juce::Label::textColourId, colours::ink);

    setColour (juce::TextButton::buttonColourId, colours::control);
    setColour (juce::TextButton::buttonOnColourId, colours::wheat);
    setColour (juce::TextButton::textColourOffId, colours::ink);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1712));

    setColour (juce::ComboBox::backgroundColourId, colours::control);
    setColour (juce::ComboBox::textColourId, colours::ink);
    setColour (juce::ComboBox::outlineColourId, colours::track);
    setColour (juce::ComboBox::arrowColourId, colours::quiet);

    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1d201e));
    setColour (juce::PopupMenu::textColourId, colours::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::wheat);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff1a1712));

    setColour (juce::Slider::textBoxTextColourId, colours::ink);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, colours::wheat);
    setColour (juce::Slider::rotarySliderOutlineColourId, colours::track);
    setColour (juce::Slider::trackColourId, colours::wheat.withAlpha (0.55f));
    setColour (juce::Slider::backgroundColourId, juce::Colour (0xff151716));
    setColour (juce::Slider::thumbColourId, colours::ink);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::track);
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (pos > 0.002f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour (colours::wheat);
        g.strokePath (value, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float body = radius * 0.66f;
    g.setColour (colours::control);
    g.fillEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f);
    g.setColour (juce::Colour (0xff454a46));
    g.drawEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f, 1.0f);

    const auto tip = centre.getPointOnCircumference (body * 0.85f, angle);
    g.setColour (colours::ink);
    g.drawLine ({ centre, tip }, 2.0f);
}

// ---------------------------------------------------------------- pieces

namespace
{
    // Shorter names where the full one (which Cubase shows) doesn't fit under a knob.
    juce::String panelName (int param)
    {
        switch (param)
        {
            case pbv::kSecondInterval:  return "Interval";
            case pbv::kBreathAttack:    return "Breath attack";
            case pbv::kVelResponse:     return "Velocity";
            case pbv::kChirpThreshold:  return "Chirp above";
            case pbv::kOverblowHyst:    return "Hysteresis";
            case pbv::kBeakModAmount:   return "Follow amount";
            case pbv::kBeakTracking:    return "Tracking";
            case pbv::kBeakOpen:        return "Openness";
            case pbv::kDriftBreathLink: return "Breath link";
            case pbv::kLegatoDip:       return "Dip";
            case pbv::kLegatoDipTime:   return "Dip time";
            case pbv::kBreathCeiling:   return "Ceiling";
            case pbv::kBreathFloor:     return "Floor";
            case pbv::kBreathCurve:     return "Curve";
            case pbv::kFlurryDensity:   return "Flurry density";
            default:                    return pbv::paramDef (param).name;
        }
    }
}

Cell::Cell (int paramIndex, std::unique_ptr<juce::Component> c, bool showName, int width, int height)
    : param (paramIndex), cellWidth (width), cellHeight (height), control (std::move (c))
{
    if (showName)
    {
        name.setJustificationType (juce::Justification::centred);
        name.setFont (juce::FontOptions (11.5f));
        name.setColour (juce::Label::textColourId, colours::quiet);
        name.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (name);
    }
    addAndMakeVisible (*control);
    // Text boxes copy their colours when created, before they reach the editor's look.
    control->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    control->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    control->setColour (juce::Slider::textBoxTextColourId, colours::ink);
    if (auto* s = dynamic_cast<juce::Slider*> (control.get())) s->lookAndFeelChanged();
    setSize (width, height);
}

void Cell::resized()
{
    auto r = getLocalBounds();
    if (name.isVisible()) name.setBounds (r.removeFromTop (15));
    control->setBounds (r);
}

BeakSketch::BeakSketch()
{
    // The mock-up's graphite sketch of a white-throated sparrow's conical beak.
    for (const char* d : { "M18 108c-6-26-2-52 16-70 14-14 34-20 52-18 16 2 28 10 34 20",
                           "M120 40c2 6 1 12-2 16", "M117 38l35 12-35 7z", "M117 57l30-3-32 11z", "M119 51l30-1",
                           "M36 40c18-14 44-20 70-14", "M44 30c16-10 36-14 54-12", "M60 50c12-6 26-8 40-6",
                           "M104 44c4-2 8-2 12 0", "M82 62c10 6 22 8 32 4", "M70 72c8 10 20 16 34 14",
                           "M30 60l10 8M28 70l12 9M30 80l12 9M34 90l12 8M40 98l10 7", "M54 84l8 6M60 92l8 6M66 98l6 5" })
        strokes.addPath (juce::Drawable::parseSVGPath (d));
    strokes.addEllipse (93.8f, 35.8f, 8.4f, 8.4f);
    setSize (150, 104);
}

void BeakSketch::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::paper);
    g.fillRoundedRectangle (r, 6.0f);
    const auto fit = juce::RectanglePlacement (juce::RectanglePlacement::centred)
                         .getTransformToFit ({ 0.0f, 0.0f, 160.0f, 116.0f }, r.reduced (4.0f));
    g.setColour (colours::graphite);
    g.strokePath (strokes, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), fit);
    g.fillEllipse (juce::Rectangle<float> (96.4f, 38.4f, 3.2f, 3.2f).transformedBy (fit));
}

void BreathMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::control);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (colours::sky);
    g.fillRoundedRectangle (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, level)), r.getHeight() * 0.5f);
}

// ---------------------------------------------------------------- editor

PrairieBirdEditor::Section& PrairieBirdEditor::section (const juce::String& t, juce::Rectangle<int> b)
{
    sections.push_back ({ t, b, {} });
    return sections.back();
}

Cell& PrairieBirdEditor::addCell (Section& s, std::unique_ptr<Cell> cell)
{
    addAndMakeVisible (*cell);
    s.items.push_back (cell.get());
    cells.push_back (std::move (cell));
    return *cells.back();
}

void PrairieBirdEditor::knob (Section& s, int param)
{
    const auto& def = pbv::paramDef (param);
    auto slider = std::make_unique<Learnable<juce::Slider>> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    slider->onPopup = [this, param] { showLearnMenu (param); };
    auto* raw = slider.get();
    auto& cell = addCell (s, std::make_unique<Cell> (param, std::move (slider), true, 64, 88));
    cell.baseText = panelName (param);
    cell.sliderAttachment = std::make_unique<juce::SliderParameterAttachment> (*proc.state.getParameter (def.id), *raw, nullptr);
}

void PrairieBirdEditor::toggle (Section& s, int param, int width)
{
    const auto& def = pbv::paramDef (param);
    auto button = std::make_unique<Learnable<juce::TextButton>> (def.name);
    button->setClickingTogglesState (true);
    button->onPopup = [this, param] { showLearnMenu (param); };
    auto* raw = button.get();
    auto& cell = addCell (s, std::make_unique<Cell> (param, std::move (button), false, width, 30));
    cell.baseText = panelName (param);
    cell.buttonAttachment = std::make_unique<juce::ButtonParameterAttachment> (*proc.state.getParameter (def.id), *raw, nullptr);
}

void PrairieBirdEditor::choice (Section& s, int param, int width)
{
    const auto& def = pbv::paramDef (param);
    auto combo = std::make_unique<Learnable<juce::ComboBox>>();
    combo->addItemList (juce::StringArray::fromTokens (def.choices, "|", ""), 1);
    combo->onPopup = [this, param] { showLearnMenu (param); };
    auto* raw = combo.get();
    auto& cell = addCell (s, std::make_unique<Cell> (param, std::move (combo), true, width, 46));
    cell.baseText = panelName (param);
    cell.comboAttachment = std::make_unique<juce::ComboBoxParameterAttachment> (*proc.state.getParameter (def.id), *raw, nullptr);
}

void PrairieBirdEditor::stepper (Section& s, int param)
{
    const auto& def = pbv::paramDef (param);
    auto slider = std::make_unique<Learnable<juce::Slider>> (juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft);
    slider->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 48, 26);
    slider->setIncDecButtonsMode (juce::Slider::incDecButtonsNotDraggable);
    slider->onPopup = [this, param] { showLearnMenu (param); };
    auto* raw = slider.get();
    auto& cell = addCell (s, std::make_unique<Cell> (param, std::move (slider), true, 120, 46));
    cell.baseText = panelName (param);
    cell.sliderAttachment = std::make_unique<juce::SliderParameterAttachment> (*proc.state.getParameter (def.id), *raw, nullptr);
}

void PrairieBirdEditor::tuneBar (Section& s, int degree)
{
    const int param = pbv::kTune0 + degree;
    const auto& def = pbv::paramDef (param);
    auto slider = std::make_unique<Learnable<juce::Slider>> (juce::Slider::LinearBarVertical, juce::Slider::TextBoxBelow);
    slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
    slider->onPopup = [this, param] { showLearnMenu (param); };
    auto* raw = slider.get();
    auto& cell = addCell (s, std::make_unique<Cell> (param, std::move (slider), true, 56, 118));
    cell.sliderAttachment = std::make_unique<juce::SliderParameterAttachment> (*proc.state.getParameter (def.id), *raw, nullptr);
}

void PrairieBirdEditor::tapButton (Section& s)
{
    // Momentary: pressing is one tap, like a hardware tap button.
    auto button = std::make_unique<Learnable<juce::TextButton>> ("Tap");
    button->onPopup = [this] { showLearnMenu (pbv::kTapTempo); };
    auto* raw = button.get();
    auto* param = proc.state.getParameter (pbv::paramDef (pbv::kTapTempo).id);
    raw->onStateChange = [raw, param, wasDown = false] () mutable
    {
        const bool down = raw->isDown();
        if (down == wasDown) return;
        wasDown = down;
        param->beginChangeGesture();
        param->setValueNotifyingHost (down ? 1.0f : 0.0f);
        param->endChangeGesture();
    };
    auto& cell = addCell (s, std::make_unique<Cell> (pbv::kTapTempo, std::move (button), false, 64, 30));
    cell.baseText = "Tap";
}

PrairieBirdEditor::PrairieBirdEditor (PrairieBirdProcessor& owner)
    : AudioProcessorEditor (owner), proc (owner)
{
    setLookAndFeel (&lnf);
    sections.reserve (12);

    // Header
    title.setText ("Prairie Bird Voice", juce::dontSendNotification);
    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    addAndMakeVisible (title);

    for (int i = 0; i < proc.getNumPrograms(); ++i) presets.addItem (proc.getProgramName (i), i + 1);
    presets.onChange = [this]
    {
        const int index = presets.getSelectedId() - 1;
        if (index >= 0 && index != proc.getCurrentProgram()) proc.setCurrentProgram (index);
    };
    addAndMakeVisible (presets);
    prevPreset.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + proc.getNumPrograms() - 1) % proc.getNumPrograms()); };
    nextPreset.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    addAndMakeVisible (prevPreset);
    addAndMakeVisible (nextPreset);

    // The safe pointer is made here, not inside the nested lambda: MSVC reads
    // `this` there as the outer lambda.
    controllerMenu.onClick = [this, safe = juce::Component::SafePointer<PrairieBirdEditor> (this)]
    {
        juce::PopupMenu m;
        m.addItem (1, "Load nanoKONTROL2 map");
        m.addItem (2, "Clear all MIDI mappings");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (controllerMenu),
                         [safe] (int r)
                         {
                             if (safe == nullptr) return;
                             if (r == 1) safe->proc.loadNanoKontrolMap();
                             if (r == 2) safe->proc.clearMidiMap();
                         });
    };
    addAndMakeVisible (controllerMenu);

    allSettings.setClickingTogglesState (true);
    allSettings.onClick = [this]
    {
        const bool all = allSettings.getToggleState();
        generic->setVisible (all);
        for (auto& c : cells) c->setVisible (! all);
        for (auto& e : extras) e->setVisible (! all);
        repaint();
    };
    addAndMakeVisible (allSettings);

    status.setFont (juce::FontOptions (13.0f));
    status.setColour (juce::Label::textColourId, colours::quiet);
    addAndMakeVisible (status);

    meterLabel.setText ("BREATH", juce::dontSendNotification);
    meterLabel.setFont (juce::FontOptions (11.0f));
    meterLabel.setColour (juce::Label::textColourId, colours::quiet);
    addAndMakeVisible (meterLabel);
    addAndMakeVisible (meter);

    // Sections, in signal order. Rows: 260, 300 and 184 px high.
    const int x0 = 12, y1 = 64, y2 = 334, y3 = 644;

    auto& voice = section ("1  Voice", { x0, y1, 420, 260 });
    stepper (voice, pbv::kOctave);
    stepper (voice, pbv::kTransposeSemis);
    stepper (voice, pbv::kTransposeCents);
    toggle (voice, pbv::kBreathMode, 120);
    for (int p : { pbv::kPurity, pbv::kSecondVoice, pbv::kSecondInterval, pbv::kVolume }) knob (voice, p);

    auto& breathSec = section ("Breath and overblow", { 442, y1, 400, 260 });
    for (int p : { pbv::kBreathFloor, pbv::kBreathCeiling, pbv::kBreathCurve, pbv::kOverblow2, pbv::kOverblow3,
                   pbv::kOverblow4, pbv::kOverblow5, pbv::kOverblowHyst, pbv::kOverblowTime })
        knob (breathSec, p);
    toggle (breathSec, pbv::kBreathLearn, 150);
    toggle (breathSec, pbv::kOverblowOn, 100);

    auto& attack = section ("2  Attack", { 852, y1, 436, 260 });
    for (int p : { pbv::kOnset, pbv::kRelease, pbv::kBreathAttack, pbv::kScoopDepth, pbv::kBreathBurst,
                   pbv::kVelResponse, pbv::kChirpThreshold, pbv::kChirpInterval, pbv::kChirpLevel })
        knob (attack, p);
    choice (attack, pbv::kScoopDir, 130);

    auto& beak = section ("3  Beak", { x0, y2, 300, 300 });
    {
        auto sketch = std::make_unique<BeakSketch>();
        addAndMakeVisible (*sketch);
        beak.items.push_back (sketch.get());
        extras.push_back (std::move (sketch));
    }
    for (int p : { pbv::kBeakTracking, pbv::kBeakOpen, pbv::kBeakModAmount }) knob (beak, p);
    choice (beak, pbv::kBeakModSource, 130);

    auto& glide = section ("4  Glide", { 322, y2, 320, 300 });
    choice (glide, pbv::kLegatoMode, 130);
    for (int p : { pbv::kLegatoBreak, pbv::kGlideAmount, pbv::kGlideTime, pbv::kGlideCurve, pbv::kLegatoDip, pbv::kLegatoDipTime })
        knob (glide, p);

    auto& drift = section ("5  Drift", { 652, y2, 230, 300 });
    for (int p : { pbv::kDriftAmount, pbv::kDriftSpeed, pbv::kDriftBreathLink }) knob (drift, p);

    auto& motion = section ("6  Motion", { 892, y2, 396, 300 });
    for (int p : { pbv::kWarbleRate, pbv::kWarbleDepth, pbv::kTrillRate, pbv::kTrillGap, pbv::kFlurryDensity,
                   pbv::kFlurrySpread, pbv::kTypedBpm })
        knob (motion, p);
    toggle (motion, pbv::kTrillOn, 84);
    choice (motion, pbv::kTrillPattern, 120);
    choice (motion, pbv::kSyncMode, 100);
    choice (motion, pbv::kSyncDivision, 90);
    choice (motion, pbv::kTempoSource, 100);
    tapButton (motion);

    auto& tuning = section ("7  Tuning", { x0, y3, 976, 184 });
    choice (tuning, pbv::kTuneRoot, 90);
    equalButton.setSize (84, 30);
    equalButton.onClick = [this]
    {
        for (int d = 0; d < 12; ++d)
        {
            auto* t = proc.state.getParameter (pbv::paramDef (pbv::kTune0 + d).id);
            t->beginChangeGesture();
            t->setValueNotifyingHost (t->convertTo0to1 (0.0f));
            t->endChangeGesture();
        }
    };
    addAndMakeVisible (equalButton);
    tuning.items.push_back (&equalButton);
    for (int d = 0; d < 12; ++d) tuneBar (tuning, d);

    auto& space = section ("8  Space", { 998, y3, 290, 184 });
    for (int p : { pbv::kEchoWet, pbv::kEchoDecay }) knob (space, p);

    generic = std::make_unique<juce::GenericAudioProcessorEditor> (proc);
    addChildComponent (*generic);

    setSize (1300, 840);
    refreshLabels();
    startTimerHz (20);
}

PrairieBirdEditor::~PrairieBirdEditor()
{
    stopTimer();
    generic.reset();
    setLookAndFeel (nullptr);
}

void PrairieBirdEditor::layoutSection (Section& s)
{
    juce::FlexBox fb;
    fb.flexWrap = juce::FlexBox::Wrap::wrap;
    fb.justifyContent = juce::FlexBox::JustifyContent::flexStart;
    fb.alignContent = juce::FlexBox::AlignContent::flexStart;
    fb.alignItems = juce::FlexBox::AlignItems::flexStart;
    for (auto* c : s.items)
        fb.items.add (juce::FlexItem (*c).withWidth ((float) c->getWidth()).withHeight ((float) c->getHeight())
                                         .withMargin ({ 0.0f, 4.0f, 4.0f, 0.0f }));
    fb.performLayout (s.bounds.reduced (10).withTrimmedTop (22));
}

void PrairieBirdEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (52).reduced (12, 10);
    title.setBounds (header.removeFromLeft (210));
    prevPreset.setBounds (header.removeFromLeft (32));
    presets.setBounds (header.removeFromLeft (240).reduced (4, 0));
    nextPreset.setBounds (header.removeFromLeft (32));
    header.removeFromLeft (12);
    controllerMenu.setBounds (header.removeFromLeft (110));
    header.removeFromLeft (8);
    allSettings.setBounds (header.removeFromLeft (110));
    meter.setBounds (header.removeFromRight (140).withSizeKeepingCentre (140, 10));
    meterLabel.setBounds (header.removeFromRight (60));
    status.setBounds (header.reduced (12, 0));

    for (auto& s : sections) layoutSection (s);

    panelArea = getLocalBounds().withTrimmedTop (56).reduced (12, 6);
    generic->setBounds (panelArea);
}

void PrairieBirdEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::ground);
    g.setColour (juce::Colour (0xff1d201e));
    g.fillRect (getLocalBounds().removeFromTop (52));
    g.setColour (juce::Colour (0xff2e322f));
    g.drawHorizontalLine (52, 0.0f, (float) getWidth());

    if (allSettings.getToggleState()) return;

    for (auto& s : sections)
    {
        const auto r = s.bounds.toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (colours::edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        g.setColour (colours::wheat);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (s.title.toUpperCase(), s.bounds.reduced (12, 8).removeFromTop (16), juce::Justification::centredLeft);
    }
}

void PrairieBirdEditor::showLearnMenu (int param)
{
    const bool learningThis = proc.getLearningParam() == param;
    const int cc = proc.ccForParam (param);
    juce::PopupMenu m;
    m.addSectionHeader (pbv::paramDef (param).name);
    m.addItem (1, learningThis ? "Cancel MIDI learn" : "MIDI Learn");
    if (cc >= 0) m.addItem (2, "Forget CC " + juce::String (cc));
    m.showMenuAsync (juce::PopupMenu::Options(),
                     [safe = juce::Component::SafePointer<PrairieBirdEditor> (this), param, learningThis] (int r)
                     {
                         if (safe == nullptr) return;
                         if (r == 1) { if (learningThis) safe->proc.cancelMidiLearn(); else safe->proc.startMidiLearn (param); }
                         if (r == 2) safe->proc.forgetMidi (param);
                         safe->refreshLabels();
                     });
}

void PrairieBirdEditor::refreshLabels()
{
    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int learning = proc.getLearningParam();
    const int root = (int) proc.state.getRawParameterValue ("tuneRoot")->load();

    for (auto& c : cells)
    {
        int cc = proc.ccForParam (c->param);
        if (cc < 0 && c->param == pbv::kTapTempo) cc = proc.ccForParam (PrairieBirdProcessor::kActionTap);
        juce::String text = c->baseText;
        if (c->param >= pbv::kTune0 && c->param <= pbv::kTune11)
            text = noteNames[(root + c->param - pbv::kTune0) % 12];
        if (c->param == pbv::kOctave && cc < 0)
        {
            const int down = proc.ccForParam (PrairieBirdProcessor::kActionOctaveDown);
            const int up = proc.ccForParam (PrairieBirdProcessor::kActionOctaveUp);
            if (down >= 0 || up >= 0) text << "  CC " << down << "/" << up;
        }
        else if (cc >= 0)
        {
            text << "  CC " << cc;
        }
        if (c->param == learning) text = "Move a control...";

        if (auto* b = dynamic_cast<juce::Button*> (c->control.get()))
        {
            if (b->getButtonText() != text) b->setButtonText (text);
        }
        else if (c->name.getText() != text)
        {
            c->name.setText (text, juce::dontSendNotification);
        }
        c->name.setColour (juce::Label::textColourId, c->param == learning ? colours::wheat : colours::quiet);
    }

    if (learning != lastLearning)
    {
        lastLearning = learning;
        status.setText (learning >= 0 ? juce::String ("MIDI learn: move a knob, fader or button for ") + pbv::paramDef (learning).name
                                      : juce::String ("Right-click any control to MIDI-learn it"),
                        juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, learning >= 0 ? colours::wheat : colours::quiet);
    }
}

void PrairieBirdEditor::timerCallback()
{
    meter.setLevel (proc.getBreathLevel());
    if (proc.getCurrentProgram() != lastProgram)
    {
        lastProgram = proc.getCurrentProgram();
        presets.setSelectedId (lastProgram + 1, juce::dontSendNotification);
    }
    refreshLabels();
}
