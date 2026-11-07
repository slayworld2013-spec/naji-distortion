#include "PluginEditor.h"

using namespace naji::ui;

namespace
{
    constexpr int editorW = 760, editorH = 440;

    float textWidth (const juce::Font& f, const juce::String& s)
    {
        return juce::GlyphArrangement::getStringWidth (f, s);
    }
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
            const juce::String& labelText, LookAndFeel& lnf, bool big)
    : laf (lnf), label (labelText), isBig (big)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (big ? 320 : 260);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);

    if (auto* p = state.getParameter (paramID))
    {
        const float def = p->getDefaultValue();
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (def));
        if (def > 0.001f && def < 0.999f && paramID == ParamIDs::output)
            slider.getProperties().set ("arcOrigin", def);   // output arc grows from 0 dB
    }

    slider.onValueChange = [this] { repaint(); };
}

void Knob::resized()
{
    auto b = getLocalBounds();
    const int labelH = isBig ? 46 : 40;
    auto knobArea = b.withTrimmedBottom (labelH);
    const int size = juce::jmin (knobArea.getWidth(), knobArea.getHeight());
    slider.setBounds (knobArea.withSizeKeepingCentre (size, size));
}

void Knob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float labelH = isBig ? 46.0f : 40.0f;
    auto text = b.removeFromBottom (labelH);

    g.setColour (colours::text);
    g.setFont (laf.boldFont (isBig ? 15.0f : 12.5f).withExtraKerningFactor (0.14f));
    g.drawText (label, text.removeFromTop (labelH * 0.5f), juce::Justification::centredBottom, false);

    g.setColour (colours::textDim);
    g.setFont (laf.mediumFont (isBig ? 13.0f : 11.5f));
    g.drawText (slider.getTextFromValue (slider.getValue()), text, juce::Justification::centred, false);
}

//==============================================================================
ModeSelector::ModeSelector (juce::RangedAudioParameter& param, LookAndFeel& lnf)
    : laf (lnf),
      attachment (param, [this] (float v) { selected = juce::jlimit (0, 3, juce::roundToInt (v)); repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> ModeSelector::segmentBounds (int index) const
{
    const float w = (float) getWidth() / (float) names.size();
    return { w * (float) index, 0.0f, w, (float) getHeight() };
}

int ModeSelector::segmentAt (juce::Point<float> p) const
{
    for (int i = 0; i < names.size(); ++i)
        if (segmentBounds (i).contains (p))
            return i;
    return -1;
}

void ModeSelector::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const float corner = 4.0f;

    g.setColour (colours::panel);
    g.fillRoundedRectangle (b, corner);

    for (int i = 0; i < names.size(); ++i)
    {
        auto seg = segmentBounds (i).reduced (3.0f);

        if (i == selected)
        {
            g.setColour (colours::red);
            g.fillRoundedRectangle (seg, corner - 1.0f);
        }
        else if (i == hovered)
        {
            g.setColour (colours::track);
            g.fillRoundedRectangle (seg, corner - 1.0f);
        }

        g.setColour (i == selected ? juce::Colours::white : (i == hovered ? colours::text : colours::textDim));
        g.setFont (laf.boldFont (11.5f).withExtraKerningFactor (0.12f));
        g.drawText (names[i], seg, juce::Justification::centred, false);
    }

    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (b, corner, 1.0f);
}

void ModeSelector::mouseDown (const juce::MouseEvent& e)
{
    const int i = segmentAt (e.position);
    if (i >= 0 && i != selected)
        attachment.setValueAsCompleteGesture ((float) i);
}

void ModeSelector::mouseMove (const juce::MouseEvent& e)
{
    const int i = segmentAt (e.position);
    if (i != hovered) { hovered = i; repaint(); }
}

void ModeSelector::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

//==============================================================================
NajiDistortionEditor::NajiDistortionEditor (NajiDistortionProcessor& p)
    : AudioProcessorEditor (p),
      modeSelector (*p.apvts.getParameter (ParamIDs::type), laf),
      drive  (p.apvts, ParamIDs::drive,  "DRIVE",  laf, true),
      tone   (p.apvts, ParamIDs::tone,   "TONE",   laf, false),
      crush  (p.apvts, ParamIDs::crush,  "CRUSH",  laf, false),
      mix    (p.apvts, ParamIDs::mix,    "MIX",    laf, false),
      output (p.apvts, ParamIDs::output, "OUTPUT", laf, false)
{
    setLookAndFeel (&laf);

    mascot = juce::Drawable::createFromImageData (BinaryData::naji_mascot_svg, BinaryData::naji_mascot_svgSize);

    for (auto* c : std::initializer_list<juce::Component*> { &modeSelector, &drive, &tone, &crush, &mix, &output })
        addAndMakeVisible (c);

    setSize (editorW, editorH);
}

NajiDistortionEditor::~NajiDistortionEditor()
{
    setLookAndFeel (nullptr);
}

void NajiDistortionEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Header
    {
        const auto titleFont = laf.boldFont (28.0f).withExtraKerningFactor (0.08f);
        const juce::String first = "NAJI ", second = "DISTORTION";
        const float x = 28.0f, y = 18.0f, h = 36.0f;
        const float w1 = textWidth (titleFont, first);

        g.setFont (titleFont);
        g.setColour (colours::text);
        g.drawText (first, juce::Rectangle<float> (x, y, w1 + 4.0f, h), juce::Justification::centredLeft, false);
        g.setColour (colours::red);
        g.drawText (second, juce::Rectangle<float> (x + w1, y, 400.0f, h), juce::Justification::centredLeft, false);

        g.setColour (colours::divider);
        g.fillRect (0, 72, getWidth(), 1);
    }

    // Mascot panel
    {
        const auto panel = mascotPanel.toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (panel, 8.0f);

        if (mascot != nullptr)
        {
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clip;
            clip.addRoundedRectangle (panel.reduced (1.0f), 7.0f);
            g.reduceClipRegion (clip);
            mascot->drawWithin (g, panel.withTrimmedTop (18.0f).withTrimmedLeft (6.0f).withTrimmedRight (6.0f),
                                juce::RectanglePlacement::xMid | juce::RectanglePlacement::yBottom, 1.0f);
        }

        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (panel.reduced (0.5f), 8.0f, 1.0f);
    }

    // Footer
    {
        g.setColour (colours::divider);
        g.fillRect (0, getHeight() - 34, getWidth(), 1);

        g.setFont (laf.mediumFont (11.0f).withExtraKerningFactor (0.1f));
        g.setColour (colours::textFaint);
        auto footer = getLocalBounds().removeFromBottom (33).reduced (28, 0);
        g.drawText ("v" JucePlugin_VersionString, footer, juce::Justification::centredLeft, false);
        g.drawText ("OVERSAMPLED", footer, juce::Justification::centredRight, false);
    }
}

void NajiDistortionEditor::resized()
{
    modeSelector.setBounds (getWidth() - 28 - 316, 22, 316, 30);

    auto body = getLocalBounds().withTrimmedTop (73).withTrimmedBottom (35).reduced (24, 18);

    mascotPanel = body.removeFromLeft (196);
    body.removeFromLeft (16);

    auto driveArea = body.removeFromLeft (232);
    drive.setBounds (driveArea.withSizeKeepingCentre (200, 252));

    body.removeFromLeft (8);
    auto top = body.removeFromTop (body.getHeight() / 2);
    auto bottom = body;

    auto place = [] (juce::Rectangle<int> area, Knob& a, Knob& b)
    {
        auto left = area.removeFromLeft (area.getWidth() / 2);
        a.setBounds (left.withSizeKeepingCentre (112, 140));
        b.setBounds (area.withSizeKeepingCentre (112, 140));
    };
    place (top, tone, crush);
    place (bottom, mix, output);
}
