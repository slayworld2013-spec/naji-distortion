/*
    NAJI DISTORTION - offline test host

    Loads the compiled .vst3 exactly like a DAW does, drives it through VST3
    parameter automation and checks:
      - every control audibly does its job (measured, not assumed)
      - no NaN/Inf, sane levels at all modes/sample rates
      - no clicks when any parameter or the mode jumps
      - MIX = 0 and host bypass are sample-exact (latency compensated)
      - state save / restore
      - CPU load
    It also renders demo WAVs and PNG snapshots of the editor.

    Usage: NajiTestHost <path/to/NAJI DISTORTION.vst3> <output dir>
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

static int failures = 0, checks = 0;

static void check (bool ok, const String& what)
{
    ++checks;
    if (! ok) { ++failures; std::cout << "  FAIL: " << what << "\n"; }
}

//==============================================================================
// Test signals
static AudioBuffer<float> sine (double sr, double seconds, double hz, float amp)
{
    AudioBuffer<float> b (2, (int) (sr * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = amp * (float) std::sin (MathConstants<double>::twoPi * hz * i / sr);
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return b;
}

// Karplus-Strong "guitar" riff: palm-muted chugs and power chords, roughly like a DI guitar.
static AudioBuffer<float> riff (double sr, double seconds)
{
    const int n = (int) (sr * seconds);
    AudioBuffer<float> b (2, n);
    b.clear();
    Random rng (1234);

    struct Note { double t, len; std::vector<double> hz; float vel; float damp; };
    std::vector<Note> notes;
    const double E2 = 82.41, B2 = 123.47, E3 = 164.81, G2 = 98.0, D3 = 146.83, A2 = 110.0;
    const double beat = 60.0 / 140.0 / 2.0;  // 8ths at 140 bpm
    double t = 0.0;
    while (t < seconds - 1.0)
    {
        for (int k = 0; k < 6; ++k) { notes.push_back ({ t, beat * 0.9, { E2 }, 0.8f, 0.55f }); t += beat; }
        notes.push_back ({ t, beat * 1.9, { G2, D3 }, 0.9f, 0.995f }); t += beat * 2;
        notes.push_back ({ t, beat * 1.9, { A2, E3 }, 0.9f, 0.995f }); t += beat * 2;
        notes.push_back ({ t, beat * 3.8, { E2, B2, E3 }, 1.0f, 0.997f }); t += beat * 4;
    }

    for (auto& note : notes)
        for (auto hz : note.hz)
        {
            const int period = (int) std::round (sr / hz);
            std::vector<float> line ((size_t) period);
            float lpState = 0.0f;
            for (auto& s : line) { lpState += 0.35f * ((rng.nextFloat() * 2.0f - 1.0f) - lpState); s = lpState * 2.5f; }
            const int start = (int) (note.t * sr), len = (int) (note.len * sr);
            const float damp = note.damp > 0.9f ? note.damp : 0.5f; // muted notes lose highs fast
            const float decay = note.damp > 0.9f ? 0.9985f : 0.985f;
            float prev = 0.0f;
            for (int i = 0; i < len && start + i < n; ++i)
            {
                const size_t idx = (size_t) (i % period);
                const float cur = line[idx];
                const float nv = decay * (damp * cur + (1.0f - damp) * prev);
                prev = cur;
                line[idx] = nv;
                const float env = i > len - 200 ? (float) (len - i) / 200.0f : 1.0f;
                const float v = cur * note.vel * env * 0.22f;
                b.addSample (0, start + i, v);
                b.addSample (1, start + i, v * 0.98f);
            }
        }

    b.applyGain (0.5f / jmax (0.001f, b.getMagnitude (0, n)));  // peak -6 dBFS
    return b;
}

static float rmsDb (const AudioBuffer<float>& b, int start = 0, int len = -1)
{
    if (len < 0) len = b.getNumSamples() - start;
    double s = 0.0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = start; i < start + len; ++i)
            s += (double) b.getSample (c, i) * b.getSample (c, i);
    return Decibels::gainToDecibels ((float) std::sqrt (s / (len * b.getNumChannels())), -200.0f);
}

static bool allFinite (const AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

static float maxStep (const AudioBuffer<float>& b, int start, int end)
{
    float m = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = jmax (1, start); i < jmin (end, b.getNumSamples()); ++i)
            m = jmax (m, std::abs (b.getSample (c, i) - b.getSample (c, i - 1)));
    return m;
}

// Spectral centroid in Hz (channel 0), used to prove TONE changes brightness.
static double centroid (const AudioBuffer<float>& b, double sr)
{
    constexpr int order = 14, size = 1 << order;
    dsp::FFT fft (order);
    std::vector<float> data (size * 2, 0.0f);
    double num = 0.0, den = 0.0;
    for (int start = 0; start + size <= b.getNumSamples(); start += size)
    {
        std::fill (data.begin(), data.end(), 0.0f);
        for (int i = 0; i < size; ++i)
            data[(size_t) i] = b.getSample (0, start + i)
                               * (0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (size - 1)));
        fft.performFrequencyOnlyForwardTransform (data.data());
        for (int k = 1; k < size / 2; ++k)
        {
            const double f = k * sr / size;
            num += f * data[(size_t) k];
            den += data[(size_t) k];
        }
    }
    return den > 0 ? num / den : 0.0;
}

// Aliasing: energy away from harmonics of f0, relative to total (dB).
static double aliasRatioDb (const AudioBuffer<float>& b, double sr, double f0)
{
    constexpr int order = 15, size = 1 << order;
    dsp::FFT fft (order);
    std::vector<float> data (size * 2, 0.0f);
    const int start = b.getNumSamples() - size;
    for (int i = 0; i < size; ++i)
    {
        const double w = 0.35875 - 0.48829 * std::cos (MathConstants<double>::twoPi * i / (size - 1))
                       + 0.14128 * std::cos (2 * MathConstants<double>::twoPi * i / (size - 1))
                       - 0.01168 * std::cos (3 * MathConstants<double>::twoPi * i / (size - 1));
        data[(size_t) i] = (float) (b.getSample (0, start + i) * w);
    }
    fft.performFrequencyOnlyForwardTransform (data.data());
    double harm = 0.0, other = 0.0;
    for (int k = 2; k < size / 2; ++k)
    {
        const double f = k * sr / size;
        const double e = (double) data[(size_t) k] * data[(size_t) k];
        const double nearest = std::round (f / f0) * f0;
        if (std::abs (f - nearest) < 6.0 * sr / size) harm += e; else other += e;
    }
    return 10.0 * std::log10 (other / (harm + 1e-30) + 1e-30);
}

//==============================================================================
struct Plugin
{
    std::unique_ptr<AudioPluginInstance> inst;
    std::map<String, AudioProcessorParameter*> params;
    double sr = 48000.0;
    int block = 512;

    void prepare (double newSr, int newBlock)
    {
        sr = newSr; block = newBlock;
        inst->releaseResources();
        inst->setRateAndBufferSizeDetails (sr, block);
        inst->prepareToPlay (sr, block);
    }

    // Plain values: percent knobs 0..100, output in dB, type 0..7, unhinged 0/1
    void set (const String& name, float v)
    {
        auto* p = params.at (name);
        float norm = v / 100.0f;
        if (name == "Output")   norm = (v + 24.0f) / 36.0f;
        if (name == "Type")     norm = v / 7.0f;
        if (name == "Unhinged") norm = v;
        p->setValue (jlimit (0.0f, 1.0f, norm));
    }

    void setAll (float drive, float tone, float crush, float mix, float out, int type,
                 float alias = 0.0f, bool unhinged = false)
    {
        set ("Drive", drive); set ("Tone", tone); set ("Crush", crush);
        set ("Mix", mix); set ("Output", out); set ("Type", (float) type);
        set ("Alias", alias); set ("Unhinged", unhinged ? 1.0f : 0.0f);
    }

    AudioBuffer<float> render (const AudioBuffer<float>& in, std::function<void (int)> perBlock = {},
                               int fixedBlock = -1)
    {
        AudioBuffer<float> out (in);
        MidiBuffer midi;
        Random rng (42);
        int pos = 0, blockIndex = 0;
        while (pos < out.getNumSamples())
        {
            int n = fixedBlock > 0 ? fixedBlock : block;
            if (fixedBlock == 0) n = 1 + rng.nextInt (block);      // random block sizes
            n = jmin (n, out.getNumSamples() - pos);
            if (perBlock) perBlock (blockIndex);
            AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
            inst->processBlock (view, midi);
            pos += n; ++blockIndex;
        }
        return out;
    }

    // Run some silence so smoothed parameters settle after a change.
    void settle (double seconds = 0.3) { AudioBuffer<float> s (2, (int) (sr * seconds)); s.clear(); render (s); }

    // Render twice and keep the second pass, so the auto gain has locked on.
    AudioBuffer<float> renderWarm (const AudioBuffer<float>& in) { render (in); return render (in); }
};

static void writeWav (const AudioBuffer<float>& b, double sr, const File& f)
{
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (f.createOutputStream().release());
    if (auto* w = wav.createWriterFor (os.get(), sr, 2, 24, {}, 0))
    {
        os.release();
        std::unique_ptr<AudioFormatWriter> writer (w);
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

static void snapshotEditor (const File& f, std::function<void (NajiDistortionProcessor&)> setup)
{
    NajiDistortionProcessor proc;
    if (setup) setup (proc);
    std::unique_ptr<AudioProcessorEditor> ed (proc.createEditor());
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    f.deleteFile();
    PNGImageFormat png;
    if (auto os = f.createOutputStream())
        png.writeImageToStream (img, *os);
}

//==============================================================================
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI juceInit;

    // "NajiTestHost calibrate": prints the steady-state auto-gain (dB) per mode / drive
    // on the reference riff, used for the static tables in DistortionEngine::makeupDb.
    if (argc >= 2 && String (argv[1]) == "calibrate")
    {
        NajiDistortionProcessor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        const auto in = riff (48000.0, 6.0);
        auto set = [&] (const char* id, float plain)
        {
            auto* prm = proc.apvts.getParameter (id);
            prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        };
        for (int u = 0; u < 2; ++u)
        {
            std::cout << "        { // " << (u ? "unhinged" : "normal") << "\n";
            for (int m = 0; m < 8; ++m)
            {
                std::cout << "            {";
                for (int d = 0; d <= 100; d += 10)
                {
                    set (ParamIDs::drive, (float) d); set (ParamIDs::type, (float) m);
                    set (ParamIDs::unhinged, (float) u); set (ParamIDs::tone, 50); set (ParamIDs::crush, 0);
                    set (ParamIDs::alias, 0); set (ParamIDs::mix, 100); set (ParamIDs::output, 0);
                    MidiBuffer midi;
                    for (int pass = 0; pass < 3; ++pass)
                    {
                        AudioBuffer<float> buf (in);
                        for (int pos = 0; pos < buf.getNumSamples(); pos += 512)
                        {
                            AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, jmin (512, buf.getNumSamples() - pos));
                            proc.processBlock (view, midi);
                        }
                    }
                    std::cout << " " << String (proc.autoGainDb.load(), 1) << "f" << (d < 100 ? "," : "");
                }
                std::cout << " },\n";
            }
            std::cout << "        },\n";
        }
        return 0;
    }

    // "NajiTestHost probe drive tone crush alias mode unhinged": RMS + auto gain for one setting.
    if (argc >= 8 && String (argv[1]) == "probe")
    {
        NajiDistortionProcessor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        const auto in = riff (48000.0, 6.0);
        auto set = [&] (const char* id, float plain)
        {
            auto* prm = proc.apvts.getParameter (id);
            prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        };
        set (ParamIDs::drive, String (argv[2]).getFloatValue()); set (ParamIDs::tone, String (argv[3]).getFloatValue());
        set (ParamIDs::crush, String (argv[4]).getFloatValue()); set (ParamIDs::alias, String (argv[5]).getFloatValue());
        set (ParamIDs::type, String (argv[6]).getFloatValue()); set (ParamIDs::unhinged, String (argv[7]).getFloatValue());
        MidiBuffer midi;
        for (int pass = 0; pass < 3; ++pass)
        {
            AudioBuffer<float> buf (in);
            for (int pos = 0; pos < buf.getNumSamples(); pos += 512)
            {
                AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, jmin (512, buf.getNumSamples() - pos));
                proc.processBlock (view, midi);
            }
            std::cout << "pass " << pass << " rms " << rmsDb (buf) << " in " << rmsDb (in) << " autogain " << proc.autoGainDb.load() << "\n";
            if (pass == 2 && argc >= 9) writeWav (buf, 48000.0, File (String::fromUTF8 (argv[8])));
        }
        return 0;
    }

    if (argc < 3) { std::cout << "usage: NajiTestHost <plugin.vst3> <outdir>\n"; return 2; }
    const File pluginFile { File::getCurrentWorkingDirectory().getChildFile (String::fromUTF8 (argv[1])) };
    const File outDir { File::getCurrentWorkingDirectory().getChildFile (String::fromUTF8 (argv[2])) };
    outDir.createDirectory();

    // ---- Load the real VST3 ------------------------------------------------
    AudioPluginFormatManager fm;
    fm.addFormat (new VST3PluginFormat());
    OwnedArray<PluginDescription> types;
    VST3PluginFormat vst3;
    vst3.findAllTypesForFile (types, pluginFile.getFullPathName());
    if (types.isEmpty()) { std::cout << "Could not scan plugin\n"; return 1; }

    const auto& desc = *types[0];
    std::cout << "Loaded: " << desc.name << " | " << desc.manufacturerName << " | " << desc.category
              << " | v" << desc.version << " | ins " << desc.numInputChannels << " outs " << desc.numOutputChannels << "\n";

    String err;
    Plugin p;
    p.inst = fm.createPluginInstance (desc, 48000.0, 512, err);
    if (p.inst == nullptr) { std::cout << "Instantiate failed: " << err << "\n"; return 1; }

    auto layout = p.inst->getBusesLayout();
    layout.inputBuses.getReference (0) = AudioChannelSet::stereo();
    layout.outputBuses.getReference (0) = AudioChannelSet::stereo();
    check (p.inst->setBusesLayout (layout), "stereo layout accepted");

    std::cout << "Parameters:";
    for (auto* prm : p.inst->getParameters())
    {
        p.params[prm->getName (64)] = prm;
        std::cout << " [" << prm->getName (64) << " = " << prm->getCurrentValueAsText() << "]";
    }
    std::cout << "\n";
    for (auto n : { "Drive", "Tone", "Crush", "Mix", "Output", "Type", "Alias", "Unhinged" })
        check (p.params.count (n) == 1, String ("parameter exists: ") + n);
    if (failures) return 1;

    p.prepare (48000.0, 512);
    const int latency = p.inst->getLatencySamples();
    std::cout << "Reported latency: " << latency << " samples (" << String (latency / 48.0, 2) << " ms @48k)\n";
    check (latency > 0 && latency < 256, "latency reported to host");

    const auto riff48 = riff (48000.0, 6.0);
    const char* modeNames[] = { "Soft", "Hard", "Fuzz", "Digital", "Octave", "Melt", "Glitch", "Nuke" };
    constexpr int numModes = 8;
    const float inRms = rmsDb (riff48);
    constexpr float levelTol = 2.0f;   // auto gain must hold the wet level within +-2 dB of the input

    // ---- 1. Level / sanity table --------------------------------------------
    std::cout << "\n[1] Output RMS (dBFS) on guitar riff, input RMS " << String (rmsDb (riff48), 1) << " dBFS, tone 50, mix 100\n";
    std::cout << "    mode      drive:   0     35     70    100   | crush 50 @ drive 70 | crush 100 @ drive 70\n";
    for (int m = 0; m < numModes; ++m)
    {
        String line = "    " + String (modeNames[m]).paddedRight (' ', 9) + "      ";
        for (float d : { 0.0f, 35.0f, 70.0f, 100.0f })
        {
            p.setAll (d, 50, 0, 100, 0, m); p.settle();
            auto out = p.renderWarm (riff48);
            check (allFinite (out), "finite output");
            check (out.getMagnitude (0, out.getNumSamples()) < 1.0f, "peak below 0 dBFS");
            check (std::abs (rmsDb (out) - inRms) < levelTol, String ("auto gain holds level: ") + modeNames[m] + " drive " + String (d));
            line << String (rmsDb (out), 1).paddedLeft (' ', 6) << " ";
        }
        for (float c : { 50.0f, 100.0f })
        {
            p.setAll (70, 50, c, 100, 0, m); p.settle();
            auto out = p.renderWarm (riff48);
            check (allFinite (out), "finite output (crush)");
            check (std::abs (rmsDb (out) - inRms) < levelTol, String ("auto gain holds level with crush: ") + modeNames[m]);
            line << "  |   " << String (rmsDb (out), 1).paddedLeft (' ', 6) << "        ";
        }
        std::cout << line << "\n";
    }

    std::cout << "\n[1b] Drive sweep 0..100 step 10 (RMS dBFS, tone 50; input is " << String (inRms, 1) << ")\n";
    for (int u = 0; u < 2; ++u)
        for (int m = 0; m < numModes; ++m)
        {
            String line = "    " + (String (modeNames[m]) + (u ? "+UNH" : "")).paddedRight (' ', 12);
            for (int d = 0; d <= 100; d += 10)
            {
                p.setAll ((float) d, 50, 0, 100, 0, m, 0, u == 1); p.settle();
                const float r = rmsDb (p.renderWarm (riff48));
                check (std::abs (r - inRms) < levelTol, "drive sweep level held");
                line << String (r, 1).paddedLeft (' ', 7);
            }
            std::cout << line << "\n";
        }

    std::cout << "\n[1c] Worst case: drive 100, UNHINGED, tone 100, crush 100, alias 100 (RMS dBFS / peak dBFS)\n";
    for (int m = 0; m < numModes; ++m)
    {
        p.setAll (100, 100, 100, 100, 0, m, 100, true); p.settle();
        auto out = p.renderWarm (riff48);
        const float r = rmsDb (out), pk = Decibels::gainToDecibels (out.getMagnitude (0, out.getNumSamples()));
        std::cout << "    " << String (modeNames[m]).paddedRight (' ', 8) << String (r, 1).paddedLeft (' ', 7)
                  << " / " << String (pk, 1) << "\n";
        check (allFinite (out), "worst case finite");
        check (std::abs (r - inRms) < levelTol + 1.0f, "worst case level held");
        check (pk < 0.0f, "worst case peak below 0 dBFS");
    }

    // ---- 2. Every control measurably works ----------------------------------
    std::cout << "\n[2] Control checks\n";
    {
        // DRIVE: more drive -> more harmonics -> brighter (on a pure sine)
        auto s = sine (48000.0, 2.0, 220.0, 0.3f);
        double prev = 0.0;
        String line = "    DRIVE  centroid (Hz) at 0/35/70/100:";
        for (float d : { 0.0f, 35.0f, 70.0f, 100.0f })
        {
            p.setAll (d, 100, 0, 100, 0, 1); p.settle();
            const double c = centroid (p.render (s), 48000.0);
            line << " " << String (c, 0);
            check (c > prev, "drive increases harmonic content");
            prev = c;
        }
        std::cout << line << "\n";

        // TONE: brighter as it goes up
        prev = 0.0; line = "    TONE   centroid (Hz) at 0/25/50/75/100:";
        for (float t : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
        {
            p.setAll (70, t, 0, 100, 0, 0); p.settle();
            const double c = centroid (p.render (riff48), 48000.0);
            line << " " << String (c, 0);
            check (c > prev, "tone increases brightness");
            prev = c;
        }
        std::cout << line << "\n";

        // CRUSH: changes the signal, more crush = more difference
        p.setAll (35, 50, 0, 100, 0, 0); p.settle();
        auto ref = p.render (riff48);
        prev = -999.0; line = "    CRUSH  difference vs crush 0 (dB) at 10/50/100:";
        for (float c : { 10.0f, 50.0f, 100.0f })
        {
            p.setAll (35, 50, c, 100, 0, 0); p.settle();
            auto out = p.render (riff48);
            out.addFrom (0, 0, ref, 0, 0, ref.getNumSamples(), -1.0f);
            out.addFrom (1, 0, ref, 1, 0, ref.getNumSamples(), -1.0f);
            const double d = rmsDb (out);
            line << " " << String (d, 1);
            check (d > prev, "crush increases digital distortion");
            prev = d;
        }
        std::cout << line << "\n";

        // MIX = 0 must be a perfect, latency-aligned dry signal
        p.setAll (100, 50, 100, 0, 0, 3); p.settle();
        auto out = p.render (riff48);
        float maxErr = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = latency; i < riff48.getNumSamples(); ++i)
                maxErr = jmax (maxErr, std::abs (out.getSample (c, i) - riff48.getSample (c, i - latency)));
        std::cout << "    MIX    0% vs dry input (latency aligned) max error: " << Decibels::gainToDecibels (maxErr, -200.0f) << " dB\n";
        check (maxErr < 1.0e-5f, "mix 0 is sample-exact dry");

        // MIX = 50 lands between dry and wet
        p.setAll (70, 50, 0, 50, 0, 1); p.settle();
        const float half = rmsDb (p.render (riff48));
        std::cout << "    MIX    50% RMS " << String (half, 1) << " dBFS\n";
        check (std::isfinite (half), "mix 50 works");

        // OUTPUT: exact dB
        p.setAll (50, 50, 0, 100, 0, 0); p.settle();
        const float at0 = rmsDb (p.renderWarm (riff48));
        for (float o : { -24.0f, -6.0f, 6.0f, 12.0f })
        {
            p.setAll (50, 50, 0, 100, o, 0); p.settle();
            const float delta = rmsDb (p.renderWarm (riff48)) - at0;
            std::cout << "    OUTPUT " << String (o, 0) << " dB -> measured " << String (delta, 2) << " dB\n";
            check (std::abs (delta - o) < 0.05f, "output gain accurate");
        }

        // TYPE: all modes sound different from each other
        std::vector<AudioBuffer<float>> outs;
        for (int m = 0; m < numModes; ++m) { p.setAll (60, 50, 0, 100, 0, m); p.settle(); outs.push_back (p.render (riff48)); }
        for (int a = 0; a < numModes; ++a)
            for (int b = a + 1; b < numModes; ++b)
            {
                auto d = outs[(size_t) a];
                d.addFrom (0, 0, outs[(size_t) b], 0, 0, d.getNumSamples(), -1.0f);
                check (rmsDb (d) > rmsDb (outs[(size_t) a]) - 20.0f,
                       String ("modes differ: ") + modeNames[a] + " vs " + modeNames[b]);
            }
        std::cout << "    TYPE   all 8 modes produce distinct output\n";

        // UNHINGED: more harmonics than the same setting without it
        {
            auto sn = sine (48000.0, 2.0, 220.0, 0.3f);
            p.setAll (60, 100, 0, 100, 0, 0); p.settle();
            const double off = centroid (p.render (sn), 48000.0);
            p.setAll (60, 100, 0, 100, 0, 0, 0, true); p.settle();
            const double on = centroid (p.render (sn), 48000.0);
            std::cout << "    UNHINGED centroid (Hz) off/on: " << String (off, 0) << " " << String (on, 0) << "\n";
            check (on > off * 1.2, "unhinged adds distortion");
        }

        // ALIAS: energy off the harmonic series rises with the knob
        {
            auto sn = sine (48000.0, 1.5, 4987.0, 0.5f);
            String l = "    ALIAS  inharmonic energy (dB) at 0/25/50/100:";
            double prevA = -999.0, a0 = 0.0, a100 = 0.0;
            for (float a : { 0.0f, 25.0f, 50.0f, 100.0f })
            {
                p.setAll (60, 100, 0, 100, 0, 1, a); p.settle();
                const double r = aliasRatioDb (p.render (sn), 48000.0, 4987.0);
                l << " " << String (r, 1);
                check (r > prevA, "alias increases aliasing");
                if (a == 0.0f) a0 = r;
                if (a == 100.0f) a100 = r;
                prevA = r;
            }
            std::cout << l << "\n";
            check (a100 - a0 > 20.0, "alias at 100 is at least 20 dB more aliased");
        }
    }

    // ---- 3. Click test: jump parameters at block boundaries ------------------
    std::cout << "\n[3] Click test (110 Hz sine, abrupt automation jumps; ratio = worst step during change / worst steady step, <= 1.25 passes)\n";
    {
        struct Jump { String name; std::function<void()> before, after; };
        auto base = [&p] (float d, float t, float c, float mx, float o, int ty) { return [=, &p] { p.setAll (d, t, c, mx, o, ty); }; };
        std::vector<Jump> jumps = {
            { "Drive 0 -> 100",     base (0, 50, 0, 100, 0, 1),   base (100, 50, 0, 100, 0, 1) },
            { "Drive 100 -> 0",     base (100, 50, 0, 100, 0, 0), base (0, 50, 0, 100, 0, 0) },
            { "Tone 0 -> 100",      base (60, 0, 0, 100, 0, 0),   base (60, 100, 0, 100, 0, 0) },
            { "Crush 0 -> 100",     base (40, 50, 0, 100, 0, 0),  base (40, 50, 100, 100, 0, 0) },
            { "Mix 0 -> 100",       base (80, 50, 0, 0, 0, 1),    base (80, 50, 0, 100, 0, 1) },
            { "Output -24 -> +12",  base (40, 50, 0, 100, -24, 0),base (40, 50, 0, 100, 12, 0) },
            { "Type Soft -> Hard",  base (60, 50, 0, 100, 0, 0),  base (60, 50, 0, 100, 0, 1) },
            { "Type Hard -> Fuzz",  base (60, 50, 0, 100, 0, 1),  base (60, 50, 0, 100, 0, 2) },
            { "Type Fuzz -> Digital", base (60, 50, 0, 100, 0, 2), base (60, 50, 0, 100, 0, 3) },
            { "Type Digital -> Soft", base (60, 50, 0, 100, 0, 3), base (60, 50, 0, 100, 0, 0) },
            { "Type Soft -> Octave",  base (60, 50, 0, 100, 0, 0), base (60, 50, 0, 100, 0, 4) },
            { "Type Octave -> Melt",  base (60, 50, 0, 100, 0, 4), base (60, 50, 0, 100, 0, 5) },
            { "Type Melt -> Glitch",  base (60, 50, 0, 100, 0, 5), base (60, 50, 0, 100, 0, 6) },
            { "Type Glitch -> Nuke",  base (60, 50, 0, 100, 0, 6), base (60, 50, 0, 100, 0, 7) },
            { "Type Nuke -> Soft",    base (60, 50, 0, 100, 0, 7), base (60, 50, 0, 100, 0, 0) },
            { "Alias 0 -> 100",       [&p] { p.setAll (60, 50, 0, 100, 0, 1, 0); },  [&p] { p.setAll (60, 50, 0, 100, 0, 1, 100); } },
            { "Unhinged off -> on",   [&p] { p.setAll (60, 50, 0, 100, 0, 0, 0, false); }, [&p] { p.setAll (60, 50, 0, 100, 0, 0, 0, true); } },
            { "Unhinged on -> off",   [&p] { p.setAll (60, 50, 0, 100, 0, 0, 0, true); },  [&p] { p.setAll (60, 50, 0, 100, 0, 0, 0, false); } },
        };

        const double sr = 48000.0;
        auto sig = sine (sr, 2.0, 110.0, 0.25f);
        const int bs = 256, changeBlock = (int) (sr * 1.0) / bs;
        const int changeAt = changeBlock * bs;

        for (auto& j : jumps)
        {
            j.before(); p.settle (0.5);
            auto out = p.render (sig, [&] (int bi) { if (bi == changeBlock) j.after(); }, bs);
            const float steadyBefore = maxStep (out, changeAt - (int) (0.4 * sr), changeAt);
            const float steadyAfter  = maxStep (out, out.getNumSamples() - (int) (0.4 * sr), out.getNumSamples());
            const float during       = maxStep (out, changeAt, changeAt + (int) (0.2 * sr));
            const float ratio = during / jmax (1.0e-9f, jmax (steadyBefore, steadyAfter));
            std::cout << "    " << j.name.paddedRight (' ', 22) << " ratio " << String (ratio, 3)
                      << (ratio <= 1.25f ? "  ok" : "  CLICK") << "\n";
            check (ratio <= 1.25f, "no click: " + j.name);
        }
    }

    // ---- 4. Host bypass is latency compensated -------------------------------
    std::cout << "\n[4] Bypass\n";
    if (auto* bp = p.inst->getBypassParameter())
    {
        p.setAll (100, 50, 0, 100, 0, 2);
        bp->setValue (1.0f); p.settle();
        auto out = p.render (riff48);
        float maxErr = 0.0f;
        for (int i = latency; i < riff48.getNumSamples(); ++i)
            maxErr = jmax (maxErr, std::abs (out.getSample (0, i) - riff48.getSample (0, i - latency)));
        std::cout << "    bypassed vs dry (latency aligned) max error: " << Decibels::gainToDecibels (maxErr, -200.0f) << " dB\n";
        check (maxErr < 1.0e-5f, "bypass is clean and aligned");
        bp->setValue (0.0f); p.settle();
    }
    else check (false, "bypass parameter present");

    // ---- 5. Sample rates and block sizes ------------------------------------
    std::cout << "\n[5] Sample rates / block sizes\n";
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        p.prepare (sr, 512);
        auto in = riff (sr, 3.0);
        bool ok = true;
        for (int m = 0; m < numModes; ++m)
            for (int u = 0; u < 2; ++u)
            {
                p.setAll (100, 100, 60, 100, 0, m, u ? 80.0f : 0.0f, u == 1); p.settle();
                auto out = p.render (in);
                ok = ok && allFinite (out) && out.getMagnitude (0, out.getNumSamples()) < 1.0f;
            }
        // tiny, odd and random block sizes must give identical output
        p.setAll (70, 60, 30, 80, -2, 7, 40, true); p.settle();
        p.inst->reset();
        auto a = p.render (in, {}, 512);
        p.inst->reset();
        auto b = p.render (in, {}, 0);
        p.inst->reset();
        auto c = p.render (in, {}, 1);
        float diff = 0.0f;
        for (int i = 0; i < a.getNumSamples(); ++i)
            diff = jmax (diff, std::abs (a.getSample (0, i) - b.getSample (0, i)), std::abs (a.getSample (0, i) - c.getSample (0, i)));
        std::cout << "    " << String (sr, 0).paddedLeft (' ', 6) << " Hz  latency " << p.inst->getLatencySamples()
                  << "  all modes finite: " << (ok ? "yes" : "NO")
                  << "  block-size independence max diff: " << Decibels::gainToDecibels (diff, -200.0f) << " dB\n";
        check (ok, "sample rate " + String (sr));
        check (diff < 1.0e-4f, "block size independence at " + String (sr));
    }

    // ---- 6. Aliasing ---------------------------------------------------------
    std::cout << "\n[6] Aliasing with ALIAS at 0 (4987 Hz sine, energy off the harmonic series vs on it; lower is cleaner)\n";
    p.prepare (48000.0, 512);
    {
        auto s = sine (48000.0, 1.5, 4987.0, 0.5f);
        for (int m = 0; m < numModes; ++m)
        {
            p.setAll (70, 100, 0, 100, 0, m); p.settle();
            std::cout << "    " << String (modeNames[m]).paddedRight (' ', 8) << " drive 70: "
                      << String (aliasRatioDb (p.render (s), 48000.0, 4987.0), 1) << " dB\n";
        }
    }

    // ---- 7. State save / restore ---------------------------------------------
    std::cout << "\n[7] State\n";
    {
        p.setAll (77, 23, 41, 66, -5, 2, 12, true); p.settle (0.05);
        MemoryBlock mb;
        p.inst->getStateInformation (mb);
        p.setAll (0, 0, 0, 0, 0, 0); p.settle (0.05);
        p.inst->setStateInformation (mb.getData(), (int) mb.getSize());
        p.settle (0.05);
        const String got = p.params["Drive"]->getCurrentValueAsText() + " " + p.params["Tone"]->getCurrentValueAsText()
                         + " " + p.params["Crush"]->getCurrentValueAsText() + " " + p.params["Mix"]->getCurrentValueAsText()
                         + " " + p.params["Output"]->getCurrentValueAsText() + " " + p.params["Type"]->getCurrentValueAsText()
                         + " " + p.params["Alias"]->getCurrentValueAsText() + " " + p.params["Unhinged"]->getCurrentValueAsText();
        std::cout << "    restored: " << got << "\n";
        check (got == "77% 23% 41% 66% -5.0 dB Fuzz 12% On", "state restored");
    }

    // ---- 8. CPU --------------------------------------------------------------
    std::cout << "\n[8] CPU (stereo, 512-sample blocks, % of one core in real time)\n";
    for (double sr : { 48000.0, 96000.0 })
    {
        p.prepare (sr, 512);
        auto in = riff (sr, 10.0);
        String line = "    " + String (sr / 1000.0, 0) + " kHz:";
        for (int m = 0; m < numModes; ++m)
        {
            p.setAll (100, 50, 50, 100, 0, m, 50, true); p.settle();
            const auto t0 = Time::getHighResolutionTicks();
            p.render (in);
            const double secs = Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - t0);
            line << "  " << modeNames[m] << " " << String (100.0 * secs / 10.0, 2) << "%";
        }
        line << "   (worst case: unhinged + alias 50)";
        std::cout << line << "\n";
    }

    // ---- 9. Demo renders -----------------------------------------------------
    std::cout << "\n[9] Rendering demo WAVs\n";
    {
        p.prepare (48000.0, 512);
        auto in = riff (48000.0, 8.0);
        writeWav (in, 48000.0, outDir.getChildFile ("00_clean_input.wav"));
        struct Demo { const char* file; float d, t, c, mx, o; int m; float a; bool u; };
        for (auto& d : { Demo { "01_soft_warm.wav",        20, 55, 0,  100, 0, 0, 0, false },
                         Demo { "02_soft_driven.wav",      65, 50, 0,  100, 0, 0, 0, false },
                         Demo { "03_hard_aggressive.wav",  75, 60, 0,  100, 0, 1, 0, false },
                         Demo { "04_fuzz_maxed.wav",       100, 45, 0, 100, 0, 2, 0, false },
                         Demo { "05_digital_folded.wav",   70, 55, 0,  100, 0, 3, 0, false },
                         Demo { "06_ridiculous.wav",       100, 70, 70, 100, 0, 3, 0, false },
                         Demo { "07_hard_crush_mix50.wav", 90, 50, 45, 50,  0, 1, 0, false },
                         Demo { "08_octave.wav",           70, 60, 0,  100, 0, 4, 0, false },
                         Demo { "09_melt.wav",             85, 55, 0,  100, 0, 5, 0, false },
                         Demo { "10_glitch.wav",           70, 60, 0,  100, 0, 6, 0, false },
                         Demo { "11_nuke_unhinged.wav",    100, 65, 30, 100, 0, 7, 0, true },
                         Demo { "12_alias_hard.wav",       70, 70, 0,  100, 0, 1, 60, false },
                         Demo { "13_everything_maxed.wav", 100, 100, 100, 100, 0, 7, 100, true } })
        {
            p.setAll (d.d, d.t, d.c, d.mx, d.o, d.m, d.a, d.u); p.settle();
            p.render (in);   // let the auto gain lock on
            writeWav (p.render (in), 48000.0, outDir.getChildFile (d.file));
            std::cout << "    " << d.file << "\n";
        }
    }

    // ---- 10. Editor snapshots -------------------------------------------------
    std::cout << "\n[10] Editor snapshots\n";
    snapshotEditor (outDir.getChildFile ("editor_default.png"), {});
    snapshotEditor (outDir.getChildFile ("editor_pushed.png"), [] (NajiDistortionProcessor& proc)
    {
        auto set = [&] (const char* id, float plain)
        {
            auto* prm = proc.apvts.getParameter (id);
            prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        };
        set (ParamIDs::drive, 88); set (ParamIDs::tone, 62); set (ParamIDs::crush, 30);
        set (ParamIDs::mix, 85); set (ParamIDs::output, -4.5f); set (ParamIDs::type, 2);
        set (ParamIDs::alias, 35);
    });
    snapshotEditor (outDir.getChildFile ("editor_unhinged.png"), [] (NajiDistortionProcessor& proc)
    {
        auto set = [&] (const char* id, float plain)
        {
            auto* prm = proc.apvts.getParameter (id);
            prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        };
        set (ParamIDs::drive, 100); set (ParamIDs::tone, 70); set (ParamIDs::crush, 20);
        set (ParamIDs::alias, 60); set (ParamIDs::type, 7); set (ParamIDs::unhinged, 1);
        proc.outputLevel = 0.8f;
    });
    std::cout << "    editor_default.png, editor_pushed.png, editor_unhinged.png\n";

    p.inst->releaseResources();
    p.inst.reset();

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
