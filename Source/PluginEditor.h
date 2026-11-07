#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "NajiLookAndFeel.h"

namespace naji::ui
{
    // A rotary knob with its name and live value underneath.
    class Knob final : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
              const juce::String& labelText, LookAndFeel& lnf, bool big);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        LookAndFeel& laf;
        juce::String label;
        bool isBig;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
    };

    // SOFT / HARD / FUZZ / DIGITAL segmented selector bound to the "type" parameter.
    class ModeSelector final : public juce::Component
    {
    public:
        ModeSelector (juce::RangedAudioParameter& param, LookAndFeel& lnf);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        int segmentAt (juce::Point<float>) const;
        juce::Rectangle<float> segmentBounds (int index) const;

        LookAndFeel& laf;
        juce::StringArray names { "SOFT", "HARD", "FUZZ", "DIGITAL" };
        int selected = 0, hovered = -1;
        juce::ParameterAttachment attachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeSelector)
    };
}

class NajiDistortionEditor final : public juce::AudioProcessorEditor
{
public:
    explicit NajiDistortionEditor (NajiDistortionProcessor&);
    ~NajiDistortionEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    naji::ui::LookAndFeel laf;
    std::unique_ptr<juce::Drawable> mascot;

    naji::ui::ModeSelector modeSelector;
    naji::ui::Knob drive, tone, crush, mix, output;

    juce::Rectangle<int> mascotPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NajiDistortionEditor)
};
