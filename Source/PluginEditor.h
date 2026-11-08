#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/PhotoStage.h"

/*
    Layout (1120 x 700):

      top     branding | preset bar (browse, prev, name/dropdown, next, save, load)
      left    effect chain: slot A / slot B + categorised effect tiles with icons
      centre  photo composition (all five photos) + effect chain readout
              + live waveform
      right   EFFECT tab (6 effect-specific controls) | LAB tab (global processing)
      bottom  DRIVE, drive type, UNHINGED, MIX, OUTPUT, input/output meters

    One 60 Hz timer drives every animation on the message thread. Audio data
    reaches the UI only through atomics and a lock-free scope buffer, so the
    UI never blocks or slows the audio thread.
*/
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
    naji::ui::EditorState state;

    naji::ui::PresetBar presetBar;
    naji::ui::EffectSelector selector;
    naji::ui::PhotoStage stage;
    naji::ui::WaveformView wave;
    naji::ui::RightPanel right;
    naji::ui::Knob drive, mix, output;
    naji::ui::Segmented driveType;
    naji::ui::UnhingedSwitch unhinged;
    naji::ui::Meters meters;
    naji::ui::PresetBrowser browser;

    juce::Rectangle<int> bottomPanel;
    double lastTime = 0.0;
    float stageDt = 0.0f;
    int frame = 0, lastA = -1, lastB = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NajiDistortionEditor)
};
