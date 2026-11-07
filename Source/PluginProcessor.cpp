#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String percentText (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }

    juce::String dbText (float v, int)
    {
        if (std::abs (v) < 0.05f) v = 0.0f;
        return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
    }

    float parseNumber (const juce::String& s) { return s.retainCharacters ("-+.0123456789").getFloatValue(); }
}

juce::AudioProcessorValueTreeState::ParameterLayout NajiDistortionProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 100.0f, 0.1f), def,
            AudioParameterFloatAttributes().withLabel ("%")
                                           .withStringFromValueFunction (percentText)
                                           .withValueFromStringFunction (parseNumber));
    };

    layout.add (pct (ParamIDs::drive, "Drive", 35.0f));
    layout.add (pct (ParamIDs::tone,  "Tone",  50.0f));
    layout.add (pct (ParamIDs::crush, "Crush", 0.0f));
    layout.add (pct (ParamIDs::mix,   "Mix",   100.0f));
    layout.add (pct (ParamIDs::alias, "Alias", 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
                                       .withStringFromValueFunction (dbText)
                                       .withValueFromStringFunction (parseNumber)));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::type, 1 }, "Type",
        StringArray { "Soft", "Hard", "Fuzz", "Digital", "Octave", "Melt", "Glitch", "Nuke" }, 0));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ParamIDs::unhinged, 1 }, "Unhinged", false));

    return layout;
}

NajiDistortionProcessor::NajiDistortionProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "NAJI_DISTORTION", createLayout())
{
    drive  = apvts.getRawParameterValue (ParamIDs::drive);
    tone   = apvts.getRawParameterValue (ParamIDs::tone);
    crush  = apvts.getRawParameterValue (ParamIDs::crush);
    mix    = apvts.getRawParameterValue (ParamIDs::mix);
    output = apvts.getRawParameterValue (ParamIDs::output);
    type   = apvts.getRawParameterValue (ParamIDs::type);
    alias  = apvts.getRawParameterValue (ParamIDs::alias);
    unhinged = apvts.getRawParameterValue (ParamIDs::unhinged);
}

bool NajiDistortionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

naji::DistortionEngine::Settings NajiDistortionProcessor::readSettings() const
{
    naji::DistortionEngine::Settings s;
    s.drive = drive->load() * 0.01f;
    s.tone  = tone->load()  * 0.01f;
    s.crush = crush->load() * 0.01f;
    s.mix   = mix->load()   * 0.01f;
    s.outDb = output->load();
    s.mode  = juce::roundToInt (type->load());
    s.alias = alias->load() * 0.01f;
    s.unhinged = unhinged->load() > 0.5f;
    return s;
}

void NajiDistortionProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    engine.setSettings (readSettings(), true);
    setLatencySamples (engine.getLatency());
}

void NajiDistortionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    engine.setSettings (readSettings());
    engine.process (buffer);

    autoGainDb.store (engine.getAutoGainDb(), std::memory_order_relaxed);

    float peak = 0.0f;
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        peak = juce::jmax (peak, buffer.getMagnitude (c, 0, buffer.getNumSamples()));
    const float prev = outputLevel.load (std::memory_order_relaxed);
    outputLevel.store (peak > prev ? peak : prev * 0.85f + peak * 0.15f, std::memory_order_relaxed);
}

void NajiDistortionProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    engine.processBypassed (buffer);
    outputLevel.store (0.0f, std::memory_order_relaxed);
}

void NajiDistortionProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void NajiDistortionProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* NajiDistortionProcessor::createEditor()
{
    return new NajiDistortionEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NajiDistortionProcessor();
}
