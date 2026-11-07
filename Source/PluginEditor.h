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

    // 8-way segmented selector (2 rows of 4) bound to the "type" parameter.
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
        juce::StringArray names { "SOFT", "HARD", "FUZZ", "DIGITAL", "OCTAVE", "MELT", "GLITCH", "NUKE" };
        int selected = 0, hovered = -1;
        juce::ParameterAttachment attachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeSelector)
    };

    // Big red UNHINGED switch.
    class UnhingedButton final : public juce::ToggleButton
    {
    public:
        UnhingedButton (juce::AudioProcessorValueTreeState& state, LookAndFeel& lnf);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        LookAndFeel& laf;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnhingedButton)
    };

    // The photo panel: shows a different photo for whichever control you touch,
    // and distorts the picture (shake, zoom, pixelate, RGB split, glitch bands,
    // posterise, red tint, strobe) according to the settings and the audio level.
    class PhotoPanel final : public juce::Component, private juce::Timer
    {
    public:
        PhotoPanel (NajiDistortionProcessor& p, LookAndFeel& lnf);

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int numPhotos = 5;

    private:
        void timerCallback() override;
        void pollParameters();
        void renderFrame();
        void showPhoto (int index);

        NajiDistortionProcessor& proc;
        LookAndFeel& laf;

        std::array<juce::Image, numPhotos> originals;
        std::array<std::vector<uint32_t>, numPhotos> scaled;   // 0xRRGGBB at render size
        int rw = 0, rh = 0;
        juce::Image frame;
        bool dirty = true;

        int current = 2, previous = 2;
        float transition = 1.0f;     // 0..1, photo switch glitch
        float kick = 0.0f;           // jolt on every knob move, decays
        float unhingedVis = 0.0f;
        uint32_t frameCounter = 0;

        struct Snapshot { float drive, tone, crush, alias, mix, output, type, unhinged; };
        Snapshot last {};
        bool havePoll = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhotoPanel)
    };
}

class NajiDistortionEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NajiDistortionEditor (NajiDistortionProcessor&);
    ~NajiDistortionEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    NajiDistortionProcessor& proc;
    naji::ui::LookAndFeel laf;

    naji::ui::ModeSelector modeSelector;
    naji::ui::PhotoPanel photo;
    naji::ui::Knob drive, tone, crush, alias, mix, output;
    naji::ui::UnhingedButton unhinged;

    float shownAutoGain = 0.0f;
    juce::Rectangle<int> footerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NajiDistortionEditor)
};
