#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DistortionEngine.h"

namespace ParamIDs
{
    inline constexpr const char* drive  = "drive";
    inline constexpr const char* tone   = "tone";
    inline constexpr const char* crush  = "crush";
    inline constexpr const char* mix    = "mix";
    inline constexpr const char* output = "output";
    inline constexpr const char* type   = "type";
    inline constexpr const char* alias  = "alias";
    inline constexpr const char* unhinged = "unhinged";
}

class NajiDistortionProcessor final : public juce::AudioProcessor
{
public:
    NajiDistortionProcessor();
    ~NajiDistortionProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Smoothed output level (0..1-ish), read by the editor to animate the photos.
    std::atomic<float> outputLevel { 0.0f };
    // Current auto-gain amount in dB (negative = turned down), shown in the footer.
    std::atomic<float> autoGainDb { 0.0f };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    naji::DistortionEngine::Settings readSettings() const;

    naji::DistortionEngine engine;

    std::atomic<float>* drive  = nullptr;
    std::atomic<float>* tone   = nullptr;
    std::atomic<float>* crush  = nullptr;
    std::atomic<float>* mix    = nullptr;
    std::atomic<float>* output = nullptr;
    std::atomic<float>* type   = nullptr;
    std::atomic<float>* alias  = nullptr;
    std::atomic<float>* unhinged = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NajiDistortionProcessor)
};
