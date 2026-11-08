#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DistortionEngine.h"
#include "Presets.h"

namespace ParamIDs
{
    inline constexpr const char* slotA      = "slotA";
    inline constexpr const char* slotB      = "slotB";
    inline constexpr const char* drive      = "drive";
    inline constexpr const char* type       = "type";        // drive character
    inline constexpr const char* unhinged   = "unhinged";
    inline constexpr const char* tone       = "tone";
    inline constexpr const char* crush      = "crush";
    inline constexpr const char* downsample = "downsample";
    inline constexpr const char* alias      = "alias";
    inline constexpr const char* comb       = "comb";
    inline constexpr const char* combFreq   = "combFreq";
    inline constexpr const char* width      = "width";
    inline constexpr const char* gate       = "gate";
    inline constexpr const char* gateRate   = "gateRate";
    inline constexpr const char* mix        = "mix";
    inline constexpr const char* output     = "output";
}

// Lock-free audio -> UI scope feed (single producer, single consumer).
struct ScopeFeed
{
    static constexpr int size = 2048, decimation = 64;
    std::array<float, size> in {}, out {};
    std::atomic<int> written { 0 };

    // audio thread
    void push (float inPeak, float outPeak) noexcept
    {
        const int w = written.load (std::memory_order_relaxed);
        in[(size_t) (w & (size - 1))] = inPeak;
        out[(size_t) (w & (size - 1))] = outPeak;
        written.store (w + 1, std::memory_order_release);
    }
};

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
    double getTailLengthSeconds() const override        { return 2.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    naji::PresetManager presets;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Read by the editor (written on the audio thread).
    std::atomic<float> autoGainDb { 0.0f };
    std::atomic<float> inPeak[2] {}, outPeak[2] {};
    ScopeFeed scope;
    std::atomic<int> activeSlot[2] { {}, {} };

    naji::EngineParams readParams() const;

private:
    void updateMeters (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out);

    naji::DistortionEngine engine;
    juce::AudioBuffer<float> inputCopy;

    std::atomic<float>* fxParams[naji::fx::numEffects + 1][naji::fx::numParams] {};
    std::atomic<float>* pSlotA = nullptr, *pSlotB = nullptr, *pDrive = nullptr, *pType = nullptr, *pUnhinged = nullptr,
                       *pTone = nullptr, *pCrush = nullptr, *pDown = nullptr, *pAlias = nullptr, *pComb = nullptr,
                       *pCombFreq = nullptr, *pWidth = nullptr, *pGate = nullptr, *pGateRate = nullptr,
                       *pMix = nullptr, *pOutput = nullptr;

    float scopeIn = 0.0f, scopeOut = 0.0f;
    int scopeCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NajiDistortionProcessor)
};
