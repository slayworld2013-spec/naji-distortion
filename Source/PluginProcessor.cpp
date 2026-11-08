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

    juce::StringArray slotChoices()
    {
        juce::StringArray s { "Off" };
        for (int i = 1; i <= naji::fx::numEffects; ++i)
            s.add (juce::String (naji::fx::def (i).name).toLowerCase().replace ("_", " "));
        for (auto& x : s) x = x.substring (0, 1).toUpperCase() + x.substring (1);
        return s;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout NajiDistortionProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 100.0f, 0.1f), def,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percentText)
                                           .withValueFromStringFunction (parseNumber));
    };

    // Main
    auto main = std::make_unique<AudioProcessorParameterGroup> ("main", "Main", "|");
    main->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::slotA, 1 }, "Slot A", slotChoices(), naji::fx::Glitch));
    main->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::slotB, 1 }, "Slot B", slotChoices(), naji::fx::Off));
    main->addChild (pct (ParamIDs::drive, "Drive", 35.0f));
    main->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::type, 1 }, "Drive Type",
                                                            StringArray { "Soft", "Hard", "Tube", "Fold" }, 0));
    main->addChild (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::unhinged, 1 }, "Unhinged", false));
    main->addChild (pct (ParamIDs::mix, "Mix", 100.0f));
    main->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::output, 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText).withValueFromStringFunction (parseNumber)));
    layout.add (std::move (main));

    // LAB
    auto lab = std::make_unique<AudioProcessorParameterGroup> ("lab", "Lab", "|");
    lab->addChild (pct (ParamIDs::tone, "Tone", 50.0f));
    lab->addChild (pct (ParamIDs::crush, "Crush", 0.0f));
    lab->addChild (pct (ParamIDs::downsample, "Downsample", 0.0f));
    lab->addChild (pct (ParamIDs::alias, "Alias", 0.0f));
    lab->addChild (pct (ParamIDs::comb, "Comb", 0.0f));
    lab->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::combFreq, 1 }, "Comb Freq", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return naji::fx::fmt::hz (naji::lab::combHz (v * 0.01f)); })
                                       .withValueFromStringFunction (parseNumber)));
    lab->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::width, 1 }, "Width", NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percentText).withValueFromStringFunction (parseNumber)));
    lab->addChild (pct (ParamIDs::gate, "Gate", 0.0f));
    lab->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::gateRate, 1 }, "Gate Rate",
                                                           StringArray (naji::lab::gateNames, naji::lab::numGateRates), 3));
    layout.add (std::move (lab));

    // One group per effect
    for (int e = 1; e <= naji::fx::numEffects; ++e)
    {
        const auto& d = naji::fx::def (e);
        auto group = std::make_unique<AudioProcessorParameterGroup> (d.id, d.name, "|");
        for (int p = 0; p < naji::fx::numParams; ++p)
        {
            const auto fmtFn = d.p[p].format;
            const String name = String (d.name).toLowerCase().substring (0, 1).toUpperCase()
                              + String (d.name).toLowerCase().substring (1) + " " + d.p[p].name;
            group->addChild (std::make_unique<AudioParameterFloat> (
                ParameterID { naji::fx::paramId (e, p), 1 }, name,
                NormalisableRange<float> (0.0f, 100.0f, 0.1f), d.p[p].def * 100.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction ([fmtFn] (float v, int) { return fmtFn (v * 0.01f); })
                                               .withValueFromStringFunction (parseNumber)));
        }
        layout.add (std::move (group));
    }
    return layout;
}

NajiDistortionProcessor::NajiDistortionProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "NAJI_DISTORTION", createLayout()),
      presets (*this)
{
    auto get = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
    pSlotA = get (ParamIDs::slotA);       pSlotB = get (ParamIDs::slotB);
    pDrive = get (ParamIDs::drive);       pType = get (ParamIDs::type);         pUnhinged = get (ParamIDs::unhinged);
    pTone = get (ParamIDs::tone);         pCrush = get (ParamIDs::crush);       pDown = get (ParamIDs::downsample);
    pAlias = get (ParamIDs::alias);       pComb = get (ParamIDs::comb);         pCombFreq = get (ParamIDs::combFreq);
    pWidth = get (ParamIDs::width);       pGate = get (ParamIDs::gate);         pGateRate = get (ParamIDs::gateRate);
    pMix = get (ParamIDs::mix);           pOutput = get (ParamIDs::output);
    for (int e = 1; e <= naji::fx::numEffects; ++e)
        for (int p = 0; p < naji::fx::numParams; ++p)
            fxParams[e][p] = get (naji::fx::paramId (e, p));

    presets.loadFactory (0);   // open with the first factory preset
}

bool NajiDistortionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

naji::EngineParams NajiDistortionProcessor::readParams() const
{
    naji::EngineParams s;
    s.slot[0] = juce::roundToInt (pSlotA->load());
    s.slot[1] = juce::roundToInt (pSlotB->load());
    for (int e = 1; e <= naji::fx::numEffects; ++e)
        for (int p = 0; p < naji::fx::numParams; ++p)
            s.fxv[e][p] = fxParams[e][p]->load() * 0.01f;
    s.drive = pDrive->load() * 0.01f;
    s.driveType = juce::roundToInt (pType->load());
    s.unhinged = pUnhinged->load() > 0.5f;
    s.tone = pTone->load() * 0.01f;
    s.crush = pCrush->load() * 0.01f;
    s.downsample = pDown->load() * 0.01f;
    s.alias = pAlias->load() * 0.01f;
    s.comb = pComb->load() * 0.01f;
    s.combFreq = pCombFreq->load() * 0.01f;
    s.width = pWidth->load() * 0.01f;
    s.gate = pGate->load() * 0.01f;
    s.gateRate = juce::roundToInt (pGateRate->load());
    s.mix = pMix->load() * 0.01f;
    s.outDb = pOutput->load();
    return s;
}

void NajiDistortionProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    engine.setParams (readParams(), true);
    inputCopy.setSize (2, samplesPerBlock, false, true, true);
    setLatencySamples (engine.getLatency());
}

void NajiDistortionProcessor::updateMeters (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out)
{
    const int n = out.getNumSamples();
    const int chans = juce::jmin (2, out.getNumChannels());
    for (int c = 0; c < chans; ++c)
    {
        const float pi = in.getMagnitude (juce::jmin (c, in.getNumChannels() - 1), 0, n);
        const float po = out.getMagnitude (c, 0, n);
        if (pi > inPeak[c].load (std::memory_order_relaxed))  inPeak[c].store (pi, std::memory_order_relaxed);
        if (po > outPeak[c].load (std::memory_order_relaxed)) outPeak[c].store (po, std::memory_order_relaxed);
    }

    // Scope: one peak pair every 64 samples
    const float* inL = in.getReadPointer (0);
    const float* outL = out.getReadPointer (0);
    const float* outR = chans > 1 ? out.getReadPointer (1) : outL;
    for (int i = 0; i < n; ++i)
    {
        scopeIn = juce::jmax (scopeIn, std::abs (inL[i]));
        scopeOut = juce::jmax (scopeOut, std::abs (0.5f * (outL[i] + outR[i])));
        if (++scopeCount >= ScopeFeed::decimation)
        {
            scope.push (scopeIn, scopeOut);
            scopeIn = scopeOut = 0.0f;
            scopeCount = 0;
        }
    }
}

void NajiDistortionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, n);

    naji::dsp::Transport t;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) t.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) t.ppq = *ppq;
            t.playing = pos->getIsPlaying();
        }

    const bool canCopy = inputCopy.getNumSamples() >= n;
    if (canCopy)
        for (int c = 0; c < juce::jmin (2, buffer.getNumChannels()); ++c)
            inputCopy.copyFrom (c, 0, buffer, c, 0, n);

    engine.setParams (readParams());
    engine.process (buffer, t);

    autoGainDb.store (engine.getAutoGainDb(), std::memory_order_relaxed);
    activeSlot[0].store (engine.getSlotEffect (0), std::memory_order_relaxed);
    activeSlot[1].store (engine.getSlotEffect (1), std::memory_order_relaxed);
    if (canCopy)
    {
        juce::AudioBuffer<float> inView (inputCopy.getArrayOfWritePointers(), juce::jmin (2, buffer.getNumChannels()), n);
        updateMeters (inView, buffer);
    }
}

void NajiDistortionProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    engine.processBypassed (buffer);
}

int NajiDistortionProcessor::getNumPrograms()                   { return (int) naji::factoryPresets().size(); }
int NajiDistortionProcessor::getCurrentProgram()                { return juce::jmax (0, presets.getCurrentFactoryIndex()); }
const juce::String NajiDistortionProcessor::getProgramName (int i)
{
    const auto& f = naji::factoryPresets();
    return juce::isPositiveAndBelow (i, (int) f.size()) ? juce::String (f[(size_t) i].name) : juce::String();
}
void NajiDistortionProcessor::setCurrentProgram (int i)
{
    presets.loadFactory (i);
}

void NajiDistortionProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
    {
        presets.writeTo (*xml);
        copyXmlToBinary (*xml, destData);
    }
}

void NajiDistortionProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presets.readFrom (*xml);
        }
}

juce::AudioProcessorEditor* NajiDistortionProcessor::createEditor()
{
    return new NajiDistortionEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NajiDistortionProcessor();
}
