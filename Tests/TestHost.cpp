/*
    NAJI DISTORTION v3 - offline test host

    Loads the compiled .vst3 exactly like a DAW does and checks:
      - all parameters exist; 18 factory programs with the exact names
      - every program recalls every parameter exactly
      - every effect does what it claims (measured: octave peaks, SSB shift
        direction, ring sidebands, formant bands, robot harmonics, pitch bend,
        melt spread, spectral freeze sustain, reverse/glitch/granular time
        scrambling, vinyl crackle + ageing, destroy/fuzz harmonics)
      - all 14 effects (and all 18 presets) are measurably different
      - effect mix = 0 is transparent; plugin MIX 0 and bypass are sample exact
      - auto gain keeps every effect and preset near the input level
      - no clicks when switching effects or jumping parameters
      - 44.1 / 48 / 88.2 / 96 / 192 kHz, odd block sizes, block-size independence
      - state save/restore, user preset save/load, CPU
    It also renders a demo WAV per preset and PNG snapshots of the editor.

    Usage: NajiTestHost <path/to/NAJI DISTORTION.vst3> <output dir>
           NajiTestHost calibrate          (drive auto-gain tables)
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <set>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;
namespace fx = naji::fx;

static int failures = 0, checks = 0;

static void check (bool ok, const String& what)
{
    ++checks;
    if (! ok) { ++failures; std::cout << "  FAIL: " << what << "\n"; }
}

//==============================================================================
// Signals
static AudioBuffer<float> sine (double sr, double seconds, double hz, float amp)
{
    AudioBuffer<float> b (2, (int) (sr * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = amp * (float) std::sin (MathConstants<double>::twoPi * hz * i / sr);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}

static AudioBuffer<float> noise (double sr, double seconds, float amp)
{
    AudioBuffer<float> b (2, (int) (sr * seconds));
    Random r (7);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = amp * (r.nextFloat() * 2.0f - 1.0f);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}

// Repeating rising log chirp 200 Hz -> 3.2 kHz over `period` seconds
static AudioBuffer<float> chirp (double sr, double seconds, double period, float amp)
{
    AudioBuffer<float> b (2, (int) (sr * seconds));
    double ph = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double tt = std::fmod (i / sr, period) / period;
        const double f = 200.0 * std::pow (16.0, tt);
        ph += MathConstants<double>::twoPi * f / sr;
        const float v = amp * (float) std::sin (ph);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}

// Karplus-Strong guitar riff (same reference as earlier versions).
static AudioBuffer<float> riff (double sr, double seconds)
{
    const int n = (int) (sr * seconds);
    AudioBuffer<float> b (2, n);
    b.clear();
    Random rng (1234);
    struct Note { double t, len; std::vector<double> hz; float vel; float damp; };
    std::vector<Note> notes;
    const double E2 = 82.41, B2 = 123.47, E3 = 164.81, G2 = 98.0, D3 = 146.83, A2 = 110.0;
    const double beat = 60.0 / 140.0 / 2.0;
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
            const float damp = note.damp > 0.9f ? note.damp : 0.5f;
            const float decay = note.damp > 0.9f ? 0.9985f : 0.985f;
            float prev = 0.0f;
            for (int i = 0; i < len && start + i < n; ++i)
            {
                const size_t idx = (size_t) (i % period);
                const float cur = line[idx];
                line[idx] = decay * (damp * cur + (1.0f - damp) * prev);
                prev = cur;
                const float env = i > len - 200 ? (float) (len - i) / 200.0f : 1.0f;
                const float v = cur * note.vel * env * 0.22f;
                b.addSample (0, start + i, v);
                b.addSample (1, start + i, v * 0.98f);
            }
        }
    b.applyGain (0.5f / jmax (0.001f, b.getMagnitude (0, n)));
    return b;
}

//==============================================================================
// Measurements
static float rmsDb (const AudioBuffer<float>& b, int start = 0, int len = -1)
{
    if (len < 0) len = b.getNumSamples() - start;
    double s = 0.0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = start; i < start + len; ++i)
            s += (double) b.getSample (c, i) * b.getSample (c, i);
    return Decibels::gainToDecibels ((float) std::sqrt (s / jmax (1, len * b.getNumChannels())), -200.0f);
}

static bool allFinite (const AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

static float peakDb (const AudioBuffer<float>& b) { return Decibels::gainToDecibels (b.getMagnitude (0, b.getNumSamples()), -200.0f); }

static float maxStep (const AudioBuffer<float>& b, int start, int end)
{
    float m = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = jmax (1, start); i < jmin (end, b.getNumSamples()); ++i)
            m = jmax (m, std::abs (b.getSample (c, i) - b.getSample (c, i - 1)));
    return m;
}

// 4th-order Butterworth high-pass (channel 0), used to find clicks.
static AudioBuffer<float> highPassed (const AudioBuffer<float>& b, double sr, double hz)
{
    AudioBuffer<float> out (1, b.getNumSamples());
    out.copyFrom (0, 0, b, 0, 0, b.getNumSamples());
    for (double q : { 0.5412, 1.3066 })
    {
        IIRFilter f;
        f.setCoefficients (IIRCoefficients::makeHighPass (sr, hz, q));
        f.processSamples (out.getWritePointer (0), out.getNumSamples());
    }
    return out;
}

static float maxAbs (const AudioBuffer<float>& b, int start, int end)
{
    float m = 0.0f;
    for (int i = jmax (0, start); i < jmin (end, b.getNumSamples()); ++i) m = jmax (m, std::abs (b.getSample (0, i)));
    return m;
}

// Averaged power spectrum of channel 0 (from `start`), 1 Hz-ish resolution at order 15.
static std::vector<double> spectrum (const AudioBuffer<float>& b, int start, int order = 15)
{
    const int size = 1 << order;
    dsp::FFT fft (order);
    std::vector<float> data ((size_t) size * 2);
    std::vector<double> acc ((size_t) size / 2, 0.0);
    for (int s = start; s + size <= b.getNumSamples(); s += size / 2)
    {
        std::fill (data.begin(), data.end(), 0.0f);
        for (int i = 0; i < size; ++i)
            data[(size_t) i] = b.getSample (0, s + i) * (0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (size - 1)));
        fft.performFrequencyOnlyForwardTransform (data.data());
        for (int k = 0; k < size / 2; ++k) acc[(size_t) k] += (double) data[(size_t) k] * data[(size_t) k];
    }
    return acc;
}

static double bandEnergy (const std::vector<double>& sp, double sr, double lo, double hi)
{
    const double binHz = sr / (double) (sp.size() * 2);
    double e = 1.0e-30;
    for (size_t k = (size_t) std::max (1.0, lo / binHz); k < sp.size() && (double) k * binHz <= hi; ++k) e += sp[k];
    return e;
}

static double peakHz (const std::vector<double>& sp, double sr, double lo, double hi)
{
    const double binHz = sr / (double) (sp.size() * 2);
    size_t best = 1;
    for (size_t k = (size_t) std::max (1.0, lo / binHz); k < sp.size() && (double) k * binHz <= hi; ++k)
        if (sp[k] > sp[best] || best < (size_t) (lo / binHz)) best = k;
    return (double) best * binHz;
}

static double dbRatio (double a, double b) { return 10.0 * std::log10 (a / b); }

static double centroid (const std::vector<double>& sp, double sr)
{
    const double binHz = sr / (double) (sp.size() * 2);
    double num = 0.0, den = 1e-30;
    for (size_t k = 1; k < sp.size(); ++k) { num += (double) k * binHz * sp[k]; den += sp[k]; }
    return num / den;
}

// Fraction of 25 ms steps where the zero-crossing rate goes down (a rising
// chirp only goes up, so time-scrambling effects push this way up).
static double zcrDecreasingFraction (const AudioBuffer<float>& b, int start)
{
    const int win = 1200;
    std::vector<int> z;
    for (int s = start; s + win <= b.getNumSamples(); s += win)
    {
        int c = 0;
        for (int i = s + 1; i < s + win; ++i)
            if ((b.getSample (0, i - 1) < 0.0f) != (b.getSample (0, i) < 0.0f)) ++c;
        z.push_back (c);
    }
    int dec = 0, tot = 0;
    for (size_t i = 1; i < z.size(); ++i)
    {
        if (z[i] < 4 && z[i - 1] < 4) continue;
        ++tot;
        if (z[i] + 2 < z[i - 1]) ++dec;
    }
    return tot > 0 ? (double) dec / tot : 0.0;
}

static double zcrIncreasingFraction (const AudioBuffer<float>& b, int start)
{
    const int win = 1200;
    std::vector<int> z;
    for (int s = start; s + win <= b.getNumSamples(); s += win)
    {
        int c = 0;
        for (int i = s + 1; i < s + win; ++i)
            if ((b.getSample (0, i - 1) < 0.0f) != (b.getSample (0, i) < 0.0f)) ++c;
        z.push_back (c);
    }
    int inc = 0, tot = 0;
    for (size_t i = 1; i < z.size(); ++i)
    {
        if (z[i] < 4 && z[i - 1] < 4) continue;
        ++tot;
        if (z[i] > z[i - 1] + 2) ++inc;
    }
    return tot > 0 ? (double) inc / tot : 0.0;
}

// Time-frequency fingerprint: 24 log bands per 2048-sample frame, in dB.
static std::vector<float> fingerprint (const AudioBuffer<float>& b, double sr, int start)
{
    constexpr int order = 11, size = 1 << order;
    dsp::FFT fft (order);
    std::vector<float> data ((size_t) size * 2), fp;
    for (int s = start; s + size <= b.getNumSamples(); s += size)
    {
        std::fill (data.begin(), data.end(), 0.0f);
        for (int i = 0; i < size; ++i)
            data[(size_t) i] = 0.5f * (b.getSample (0, s + i) + b.getSample (1, s + i))
                             * (0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (size - 1)));
        fft.performFrequencyOnlyForwardTransform (data.data());
        for (int band = 0; band < 24; ++band)
        {
            const double lo = 60.0 * std::pow (2.0, band * 8.0 / 24.0), hi = lo * std::pow (2.0, 8.0 / 24.0);
            double e = 1e-12;
            for (int k = (int) (lo * size / sr); k <= (int) (hi * size / sr) && k < size / 2; ++k) e += (double) data[(size_t) k] * data[(size_t) k];
            fp.push_back ((float) jmax (-100.0, 10.0 * std::log10 (e)));
        }
    }
    return fp;
}

static float fpDistance (const std::vector<float>& a, const std::vector<float>& b)
{
    double d = 0.0;
    const size_t n = std::min (a.size(), b.size());
    for (size_t i = 0; i < n; ++i) d += std::abs (a[i] - b[i]);
    return (float) (d / (double) jmax ((size_t) 1, n));
}

//==============================================================================
// Hosted plugin wrapper. Parameters are addressed by our string IDs; the
// mapping to the hosted VST3 parameter goes through the parameter name.
struct Plugin
{
    std::unique_ptr<AudioPluginInstance> inst;
    std::map<String, AudioProcessorParameter*> hosted;     // by name
    NajiDistortionProcessor reference;                     // in-process copy, for ranges/names/defaults
    std::map<String, RangedAudioParameter*> local;         // by ID
    double sr = 48000.0;
    int block = 512;

    void index()
    {
        for (auto* p : inst->getParameters()) hosted[p->getName (256)] = p;
        for (auto* p : reference.getParameters())
            if (auto* rp = dynamic_cast<RangedAudioParameter*> (p)) local[rp->getParameterID()] = rp;
    }

    AudioProcessorParameter* h (const String& id)
    {
        auto it = hosted.find (local.at (id)->getName (256));
        return it != hosted.end() ? it->second : nullptr;
    }

    void prepare (double newSr, int newBlock)
    {
        sr = newSr; block = newBlock;
        inst->releaseResources();
        inst->setRateAndBufferSizeDetails (sr, block);
        inst->prepareToPlay (sr, block);
    }

    void set (const String& id, float plain) { h (id)->setValue (local.at (id)->convertTo0to1 (plain)); }

    // All parameters to their defaults, then the given plain values.
    void apply (std::initializer_list<std::pair<const char*, float>> values)
    {
        for (auto& [id, p] : local) h (id)->setValue (p->getDefaultValue());
        // neutral base for effect tests
        set ("slotA", 0); set ("slotB", 0); set ("drive", 20); set ("tone", 50);
        for (auto& [id, v] : values) set (id, v);
    }

    AudioBuffer<float> render (const AudioBuffer<float>& in, std::function<void (int)> perBlock = {}, int fixedBlock = -1)
    {
        AudioBuffer<float> out (in);
        MidiBuffer midi;
        Random rng (42);
        int pos = 0, blockIndex = 0;
        while (pos < out.getNumSamples())
        {
            int n = fixedBlock > 0 ? fixedBlock : block;
            if (fixedBlock == 0) n = 1 + rng.nextInt (block);
            n = jmin (n, out.getNumSamples() - pos);
            if (perBlock) perBlock (blockIndex);
            AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
            inst->processBlock (view, midi);
            pos += n; ++blockIndex;
        }
        return out;
    }

    void settle (double seconds = 0.3) { AudioBuffer<float> s (2, (int) (sr * seconds)); s.clear(); render (s); }
    AudioBuffer<float> renderWarm (const AudioBuffer<float>& in) { render (in); return render (in); }
};

static void writeWav (const AudioBuffer<float>& b, double sr, const File& f)
{
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return;
    if (auto writer = wav.createWriterFor (os, AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24)))
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

//==============================================================================
static int calibrate()
{
    NajiDistortionProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    const auto in = riff (48000.0, 6.0);
    proc.presets.applyValues ({ { "slotA", 0.0f }, { "slotB", 0.0f } });
    auto set = [&] (const char* id, float plain) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (plain)); };
    for (int u = 0; u < 2; ++u)
    {
        std::cout << "        { // " << (u ? "unhinged" : "normal") << "\n";
        for (int m = 0; m < 4; ++m)
        {
            std::cout << "            {";
            for (int d = 0; d <= 100; d += 10)
            {
                set ("drive", (float) d); set ("type", (float) m); set ("unhinged", (float) u);
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

//==============================================================================
static void editorSnapshots (const File& outDir)
    {
    auto snap = [&] (const String& file, int preset, std::function<void (NajiDistortionProcessor&, AudioProcessorEditor&)> setup)
    {
        NajiDistortionProcessor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        if (preset >= 0) proc.setCurrentProgram (preset);
        // feed some audio so meters / scope have data
        auto in = riff (48000.0, 1.0);
        MidiBuffer midi;
        for (int pos = 0; pos < in.getNumSamples(); pos += 512)
        {
            AudioBuffer<float> view (in.getArrayOfWritePointers(), 2, pos, jmin (512, in.getNumSamples() - pos));
            proc.processBlock (view, midi);
        }
        std::unique_ptr<AudioProcessorEditor> ed (proc.createEditor());
        if (setup) setup (proc, *ed);
        const auto t0 = Time::getHighResolutionTicks();
        auto more = riff (48000.0, 2.0);
        int feed = 0;
        for (int k = 0; k < 30; ++k)
        {
            for (int j = 0; j < 2 && feed + 512 <= more.getNumSamples(); ++j, feed += 512)
            {
                AudioBuffer<float> view (more.getArrayOfWritePointers(), 2, feed, 512);
                proc.processBlock (view, midi);
            }
            MessageManager::getInstance()->runDispatchLoopUntil (17);
        }
        const double animSecs = Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - t0);
        const auto t1 = Time::getHighResolutionTicks();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        const double paintMs = 1000.0 * Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - t1);

        // Per-frame cost of the animated centre display at 1x (what a normal screen redraws each frame)
        double stageMs = 0.0;
        std::function<Component* (Component&)> findStage = [&] (Component& c) -> Component*
        {
            for (auto* child : c.getChildren())
            {
                if (dynamic_cast<naji::ui::PhotoStage*> (child) != nullptr) return child;
                if (auto* r = findStage (*child)) return r;
            }
            return nullptr;
        };
        if (auto* st = findStage (*ed))
        {
            const auto t2 = Time::getHighResolutionTicks();
            for (int k = 0; k < (getenv ("NAJI_PROFILE") ? 3000 : 20); ++k) st->createComponentSnapshot (st->getLocalBounds(), true, 1.0f);
            stageMs = 1000.0 * Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - t2) / 20.0;
        }

        File out = outDir.getChildFile (file);
        out.deleteFile();
        PNGImageFormat png;
        if (auto os = out.createOutputStream()) png.writeImageToStream (img, *os);
        std::cout << "    " << file << "  " << ed->getWidth() << "x" << ed->getHeight() << "  full repaint at 2x: "
                  << String (paintMs, 1) << " ms, centre stage frame at 1x: " << String (stageMs, 1) << " ms  (0.5 s of animation took " << String (animSecs, 2) << " s)\n";
        check (ed->getWidth() >= 900 && ed->getHeight() >= 600, "editor size");
    };
    snap ("editor_default.png", 0, {});
    snap ("editor_formant.png", 3, {});
    snap ("editor_anime_gamer.png", 13, {});
    snap ("editor_lab.png", 8, [] (NajiDistortionProcessor&, AudioProcessorEditor& ed)
    {
        std::function<void (Component&)> find = [&] (Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (auto* rp = dynamic_cast<naji::ui::RightPanel*> (child)) rp->setTab (1);
                find (*child);
            }
        };
        find (ed);
    });
    snap ("editor_browser.png", 9, [] (NajiDistortionProcessor&, AudioProcessorEditor& ed)
    {
        std::function<void (Component&)> find = [&] (Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (child->getComponentID() == "browseButton")
                    if (auto* b = dynamic_cast<Button*> (child)) b->triggerClick();
                find (*child);
            }
        };
        find (ed);
    });
}


//==============================================================================
int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI juceInit;
    std::cout << std::unitbuf;

    if (argc >= 2 && String (argv[1]) == "calibrate") return calibrate();
    // "switchprobe a b": slot A effect a -> b on a 110 Hz sine, print HF (>6 kHz) peak per 5 ms around the change
    if (argc >= 4 && String (argv[1]) == "switchprobe")
    {
        NajiDistortionProcessor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 256);
        proc.prepareToPlay (48000.0, 256);
        proc.presets.applyValues ({ { "slotA", String (argv[2]).getFloatValue() }, { "slotB", 0.0f }, { "drive", 40.0f } });
        auto sig = sine (48000.0, 4.0, 110.0, 0.25f);
        MidiBuffer midi;
        for (int pos = 0; pos < sig.getNumSamples(); pos += 256)
        {
            if (pos == 256 * 375) proc.apvts.getParameter ("slotA")->setValueNotifyingHost (proc.apvts.getParameter ("slotA")->convertTo0to1 (String (argv[3]).getFloatValue()));
            AudioBuffer<float> view (sig.getArrayOfWritePointers(), 2, pos, 256);
            proc.processBlock (view, midi);
        }
        auto hf = highPassed (sig, 48000.0, 6000.0);
        const int at = 256 * 375;
        for (int w = (getenv ("NAJI_W0") ? atoi (getenv ("NAJI_W0")) : -4); w < (getenv ("NAJI_W1") ? atoi (getenv ("NAJI_W1")) : 30); ++w)
            std::cout << String (w * 5).paddedLeft (' ', 4) << " ms  HF " << String (Decibels::gainToDecibels (maxAbs (hf, at + w * 240, at + (w + 1) * 240), -120.0f), 1)
                      << "  level " << String (Decibels::gainToDecibels (sig.getMagnitude (0, at + w * 240, 240), -120.0f), 1) << "\n";
        return 0;
    }
    // "fxprobe <effect id> p0 p1 p2 p3 p4 p5 <sine Hz>": run one engine directly, print the strongest peaks.
    if (argc >= 10 && String (argv[1]) == "fxprobe")
    {
        const double rate = 48000.0;
        auto eng = naji::dsp::createEffect (String (argv[2]).getIntValue());
        eng->prepare (rate, 512);
        auto in = sine (rate, 3.0, String (argv[9]).getDoubleValue(), 0.3f);
        std::vector<float> pv[6];
        naji::dsp::ParamBlock pb;
        for (int k = 0; k < 6; ++k) { pv[k].assign (512, String (argv[3 + k]).getFloatValue() * 0.01f); pb.v[k] = pv[k].data(); }
        naji::dsp::Transport tr;
        for (int pos = 0; pos < in.getNumSamples(); pos += 512)
        {
            float* ch[2] = { in.getWritePointer (0, pos), in.getWritePointer (1, pos) };
            eng->process (ch, 2, jmin (512, in.getNumSamples() - pos), pb, tr);
        }
        auto sp = spectrum (in, 24000);
        std::vector<std::pair<double, int>> peaks;
        for (int k = 2; k < (int) sp.size() - 2; ++k)
            if (sp[(size_t) k] > sp[(size_t) k - 1] && sp[(size_t) k] >= sp[(size_t) k + 1]) peaks.push_back ({ sp[(size_t) k], k });
        std::sort (peaks.rbegin(), peaks.rend());
        double tot = 0; for (auto v : sp) tot += v;
        for (size_t i = 0; i < std::min<size_t> (8, peaks.size()); ++i)
            std::cout << String (peaks[i].second * rate / 32768.0, 1) << " Hz  " << String (10.0 * std::log10 (peaks[i].first / tot), 1) << " dB\n";
        return 0;
    }
    if (argc >= 3 && String (argv[1]) == "snapshots")
    {
        const File dir { File::getCurrentWorkingDirectory().getChildFile (String::fromUTF8 (argv[2])) };
        dir.createDirectory();
        editorSnapshots (dir);
        return failures == 0 ? 0 : 1;
    }
    if (argc < 3) { std::cout << "usage: NajiTestHost <plugin.vst3> <outdir>\n"; return 2; }

    const File pluginFile { File::getCurrentWorkingDirectory().getChildFile (String::fromUTF8 (argv[1])) };
    const File outDir { File::getCurrentWorkingDirectory().getChildFile (String::fromUTF8 (argv[2])) };
    outDir.createDirectory();

    AudioPluginFormatManager fm;
    fm.addFormat (new VST3PluginFormat());
    OwnedArray<PluginDescription> types;
    VST3PluginFormat vst3;
    vst3.findAllTypesForFile (types, pluginFile.getFullPathName());
    if (types.isEmpty()) { std::cout << "Could not scan plugin\n"; return 1; }
    const auto& desc = *types[0];
    std::cout << "Loaded: " << desc.name << " | " << desc.manufacturerName << " | " << desc.category << " | v" << desc.version << "\n";

    String err;
    Plugin p;
    p.inst = fm.createPluginInstance (desc, 48000.0, 512, err);
    if (p.inst == nullptr) { std::cout << "Instantiate failed: " << err << "\n"; return 1; }
    auto layout = p.inst->getBusesLayout();
    layout.inputBuses.getReference (0) = AudioChannelSet::stereo();
    layout.outputBuses.getReference (0) = AudioChannelSet::stereo();
    check (p.inst->setBusesLayout (layout), "stereo layout accepted");
    p.index();

    // ---- 0. Parameters & programs ---------------------------------------------
    std::cout << "\n[0] Parameters and programs\n";
    {
        int missing = 0;
        for (auto& [id, lp] : p.local) if (p.h (id) == nullptr) { ++missing; std::cout << "    missing: " << id << "\n"; }
        std::cout << "    " << p.local.size() << " parameters, " << missing << " missing in the VST3\n";
        check (missing == 0 && p.local.size() == 16 + 14 * 6, "all 100 parameters exposed");

        const char* expected[] = { "Payday 2", "Dollar Store Cappucino Assassino", "Gummy808s", "Jacob", "Nineslay", "Raheem", "Recqn",
                                   "Liam", "Housprty", "Raheem. Yshaouzi", "Danzie", "Yusuf", "Fuquad", "Anime Gamer",
                                   "Dicks Sporting Goods", "Fortnite", "Pirate Shop Token", "Boolymon" };
        check (p.inst->getNumPrograms() == 18, "18 factory programs");
        bool namesOk = true;
        for (int i = 0; i < 18; ++i) namesOk = namesOk && p.inst->getProgramName (i) == expected[i];
        check (namesOk, "program names exact");
        std::cout << "    programs: " << p.inst->getNumPrograms() << ", names " << (namesOk ? "exact" : "WRONG") << "\n";
    }
    if (failures) return 1;

    p.prepare (48000.0, 512);
    const int latency = p.inst->getLatencySamples();
    std::cout << "    latency " << latency << " samples\n";
    const double sr = 48000.0;
    const auto riff48 = riff (sr, 6.0);
    const float inRms = rmsDb (riff48);

    // ---- 1. Effect-specific measurements ---------------------------------------
    std::cout << "\n[1] Effect DSP checks (drive 0, slot A only)\n";
    auto fxOnly = [&] (int effect, std::initializer_list<std::pair<const char*, float>> extra)
    {
        p.apply ({ { "drive", 0 }, { "tone", 75 } });
        p.set ("slotA", (float) effect);
        for (auto& [id, v] : extra) p.set (id, v);
        p.settle (0.3);
    };
    {
        // OCTAVE
        auto s220 = sine (sr, 3.0, 220.0, 0.3f);
        fxOnly (fx::Octave, { { "octave_down", 100 }, { "octave_up", 0 }, { "octave_sub", 0 }, { "octave_harm", 0 }, { "octave_blend", 100 } });
        auto sp = spectrum (p.render (s220), 24000);
        const double down110 = dbRatio (bandEnergy (sp, sr, 100, 120), bandEnergy (sp, sr, 210, 230));
        fxOnly (fx::Octave, { { "octave_down", 0 }, { "octave_up", 100 }, { "octave_sub", 0 }, { "octave_harm", 0 }, { "octave_blend", 100 } });
        sp = spectrum (p.render (s220), 24000);
        const double up440 = dbRatio (bandEnergy (sp, sr, 430, 450), bandEnergy (sp, sr, 210, 230));
        fxOnly (fx::Octave, { { "octave_down", 0 }, { "octave_up", 0 }, { "octave_sub", 100 }, { "octave_harm", 0 }, { "octave_blend", 100 } });
        sp = spectrum (p.render (s220), 24000);
        const double sub110 = dbRatio (bandEnergy (sp, sr, 100, 120), bandEnergy (sp, sr, 210, 230));
        std::cout << "    OCTAVE   down: 110 Hz vs 220 Hz " << String (down110, 1) << " dB | up: 440 vs 220 " << String (up440, 1)
                  << " dB | sub: 110 vs 220 " << String (sub110, 1) << " dB\n";
        check (down110 > 10.0, "octave down produces 110 Hz from 220 Hz");
        check (up440 > 10.0, "octave up produces 440 Hz from 220 Hz");
        check (sub110 > 6.0, "analog sub produces 110 Hz");

        // FREQ SHIFT (+300 Hz and -300 Hz)
        auto s1k = sine (sr, 3.0, 1000.0, 0.3f);
        const float vUp = (float) (50.0 + 50.0 * std::cbrt (300.0 / 2000.0)), vDn = 100.0f - vUp;
        fxOnly (fx::FreqShift, { { "fshift_shift", vUp }, { "fshift_fb", 0 }, { "fshift_mod", 0 }, { "fshift_stereo", 0 }, { "fshift_mix", 100 } });
        sp = spectrum (p.render (s1k), 24000);
        const double pkUp = peakHz (sp, sr, 200, 3000), ssbUp = dbRatio (bandEnergy (sp, sr, 1290, 1310), bandEnergy (sp, sr, 690, 710));
        fxOnly (fx::FreqShift, { { "fshift_shift", vDn }, { "fshift_fb", 0 }, { "fshift_mod", 0 }, { "fshift_stereo", 0 }, { "fshift_mix", 100 } });
        sp = spectrum (p.render (s1k), 24000);
        const double pkDn = peakHz (sp, sr, 200, 3000);
        std::cout << "    FSHIFT   +300 Hz -> peak " << String (pkUp, 1) << " Hz (upper vs lower sideband " << String (ssbUp, 1)
                  << " dB) | -300 Hz -> peak " << String (pkDn, 1) << " Hz\n";
        check (std::abs (pkUp - 1300.0) < 5.0, "frequency shift +300 Hz moves 1 kHz to 1.3 kHz");
        check (std::abs (pkDn - 700.0) < 5.0, "frequency shift -300 Hz moves 1 kHz to 700 Hz");
        check (ssbUp > 25.0, "frequency shift is single-sideband");

        // RING
        const float vRing = (float) (100.0 * std::log (300.0) / std::log (4000.0));
        fxOnly (fx::Ring, { { "ring_freq", vRing }, { "ring_shape", 0 }, { "ring_track", 0 }, { "ring_fm", 0 }, { "ring_spread", 0 }, { "ring_mix", 100 } });
        sp = spectrum (p.render (s1k), 24000);
        const double side = dbRatio (bandEnergy (sp, sr, 690, 710) + bandEnergy (sp, sr, 1290, 1310), bandEnergy (sp, sr, 990, 1010));
        std::cout << "    RING     300 Hz carrier on 1 kHz: sidebands vs carrier-input " << String (side, 1) << " dB\n";
        check (side > 20.0, "ring modulation creates 700 + 1300 Hz and removes 1 kHz");

        // WARP bend +12
        fxOnly (fx::Warp, { { "warp_bend", 100 }, { "warp_sweep", 0 }, { "warp_fm", 0 }, { "warp_mix", 100 } });
        sp = spectrum (p.render (s1k), 24000);
        const double warpPk = peakHz (sp, sr, 200, 5000);
        std::cout << "    WARP     bend +12 st: 1 kHz -> " << String (warpPk, 1) << " Hz\n";
        check (std::abs (warpPk - 2000.0) < 15.0, "warp bend +12 doubles the pitch");

        // MELT spread
        fxOnly (fx::Melt, { { "melt_wobble", 100 }, { "melt_speed", 50 }, { "melt_drift", 60 }, { "melt_smear", 0 }, { "melt_sag", 0 }, { "melt_mix", 100 } });
        sp = spectrum (p.render (s1k), 24000);
        const double total = bandEnergy (sp, sr, 20, 20000), core = bandEnergy (sp, sr, 985, 1015);
        const double spread = 1.0 - core / total;
        std::cout << "    MELT     energy moved off 1 kHz by pitch wobble: " << String (spread * 100.0, 1) << "%\n";
        check (spread > 0.25, "melt modulates pitch");

        // FORMANT: vowel A vs I on noise
        auto nz = noise (sr, 3.0, 0.2f);
        fxOnly (fx::Formant, { { "formant_vowel", 0 }, { "formant_shift", 50 }, { "formant_res", 60 }, { "formant_mod", 0 }, { "formant_move", 0 }, { "formant_amount", 100 } });
        auto spA = spectrum (p.render (nz), 24000);
        fxOnly (fx::Formant, { { "formant_vowel", 50 }, { "formant_shift", 50 }, { "formant_res", 60 }, { "formant_mod", 0 }, { "formant_move", 0 }, { "formant_amount", 100 } });
        auto spI = spectrum (p.render (nz), 24000);
        fxOnly (fx::Formant, { { "formant_vowel", 0 }, { "formant_shift", 100 }, { "formant_res", 60 }, { "formant_mod", 0 }, { "formant_move", 0 }, { "formant_amount", 100 } });
        auto spAshift = spectrum (p.render (nz), 24000);
        const double aF1 = dbRatio (bandEnergy (spA, sr, 700, 900), bandEnergy (spI, sr, 700, 900));
        const double iF2 = dbRatio (bandEnergy (spI, sr, 2000, 2300), bandEnergy (spA, sr, 2000, 2300));
        const double pkA = peakHz (spA, sr, 300, 1300), pkAs = peakHz (spAshift, sr, 300, 3000);
        std::cout << "    FORMANT  'A' F1 band +" << String (aF1, 1) << " dB vs 'I'; 'I' F2 band +" << String (iF2, 1)
                  << " dB vs 'A'; shift +12: F1 peak " << String (pkA, 0) << " -> " << String (pkAs, 0) << " Hz\n";
        check (aF1 > 6.0 && iF2 > 6.0, "formant vowels put resonances in different places");
        check (pkAs > pkA * 1.6, "formant shift moves the formants up");

        // ROBOT on noise: harmonics of 100 Hz
        const float vRobot = (float) (100.0 * std::log (100.0 / 40.0) / std::log (15.0));
        fxOnly (fx::Robot, { { "robot_pitch", vRobot }, { "robot_robot", 100 }, { "robot_metal", 0 }, { "robot_ring", 0 }, { "robot_mix", 100 } });
        sp = spectrum (p.render (nz), 24000);
        double on = 0.0, off = 0.0;
        for (int k = 3; k <= 20; ++k) { on += bandEnergy (sp, sr, k * 100.0 - 4, k * 100.0 + 4); off += bandEnergy (sp, sr, k * 100.0 + 46, k * 100.0 + 54); }
        std::cout << "    ROBOT    noise -> 100 Hz harmonic series: on-harmonic vs between " << String (dbRatio (on, off), 1) << " dB\n";
        check (dbRatio (on, off) > 10.0, "robot turns noise into a pitched buzz");

        // SPECTRAL freeze sustains after the input stops
        AudioBuffer<float> burst (2, (int) (sr * 4.0));
        burst.clear();
        for (int c = 0; c < 2; ++c) burst.copyFrom (c, 0, riff48, c, 0, (int) (sr * 2.0));
        fxOnly (fx::Spectral, { { "spec_freeze", 100 }, { "spec_shift", 50 }, { "spec_stretch", 50 }, { "spec_blur", 0 }, { "spec_thin", 0 }, { "spec_mix", 100 } });
        const float frozenTail = rmsDb (p.render (burst), (int) (sr * 2.6), (int) (sr * 1.0));
        fxOnly (fx::Spectral, { { "spec_freeze", 0 }, { "spec_mix", 100 } });
        const float liveTail = rmsDb (p.render (burst), (int) (sr * 2.6), (int) (sr * 1.0));
        std::cout << "    SPECTRAL tail 0.6-1.6 s after the input stops: freeze 100 " << String (frozenTail, 1) << " dBFS, freeze 0 "
                  << String (liveTail, 1) << " dBFS\n";
        check (frozenTail > -45.0 && liveTail < -80.0, "spectral freeze holds the sound");

        // REVERSE / GLITCH / GRANULAR scramble a rising chirp
        auto ch = chirp (sr, 6.0, 1.5, 0.3f);
        fxOnly (fx::Off, {});
        const double dryDec = zcrDecreasingFraction (p.render (ch), (int) sr);
        fxOnly (fx::Reverse, { { "rev_len", 40 }, { "rev_chance", 100 }, { "rev_fb", 0 }, { "rev_fade", 10 }, { "rev_speed", 50 }, { "rev_mix", 100 } });
        auto revOut = p.render (ch);
        const double revDec = zcrDecreasingFraction (revOut, (int) sr);
        const double revInc = zcrIncreasingFraction (revOut, (int) sr);
        fxOnly (fx::Glitch, { { "glitch_slice", 60 }, { "glitch_chaos", 100 }, { "glitch_repeat", 20 }, { "glitch_pitch", 0 }, { "glitch_chop", 0 }, { "glitch_mix", 100 } });
        const double glDec = zcrDecreasingFraction (p.render (ch), (int) sr);
        fxOnly (fx::Granular, { { "gran_size", 50 }, { "gran_density", 50 }, { "gran_pos", 50 }, { "gran_spray", 80 }, { "gran_pitch", 0 }, { "gran_mix", 100 } });
        const double grDec = zcrDecreasingFraction (p.render (ch), (int) sr);
        std::cout << "    TIME     rising chirp, share of steps where pitch goes DOWN: dry " << String (dryDec * 100, 0) << "%, reverse "
                  << String (revDec * 100, 0) << "% (up " << String (revInc * 100, 0) << "%), glitch " << String (glDec * 100, 0) << "%, granular " << String (grDec * 100, 0) << "%\n";
        check (dryDec < 0.05, "chirp test baseline");
        check (revDec > 0.3 && revDec > revInc * 2.0, "reverse plays audio backwards (pitch mostly falls)");
        check (glDec > 0.15, "glitch repeats / reverses slices");
        check (grDec > 0.15, "granular scrambles time");

        // VINYL crackle + age
        auto quiet = sine (sr, 3.0, 440.0, 0.1f);
        fxOnly (fx::Vinyl, { { "vinyl_wow", 0 }, { "vinyl_flutter", 0 }, { "vinyl_crackle", 100 }, { "vinyl_noise", 0 }, { "vinyl_age", 0 }, { "vinyl_mix", 100 } });
        auto vo = p.render (quiet);
        int clicks = 0;
        {
            float prev = 0.0f;
            for (int i = (int) sr; i < vo.getNumSamples(); ++i)
            {
                const float hp = vo.getSample (0, i) - prev; prev = vo.getSample (0, i);
                if (std::abs (hp) > 0.08f && std::abs (vo.getSample (0, i - 1) - vo.getSample (0, i - 2)) < 0.04f) ++clicks;
            }
        }
        fxOnly (fx::Vinyl, { { "vinyl_crackle", 0 }, { "vinyl_age", 0 }, { "vinyl_mix", 100 } });
        const double cYoung = centroid (spectrum (p.render (nz), 24000), sr);
        fxOnly (fx::Vinyl, { { "vinyl_crackle", 0 }, { "vinyl_age", 100 }, { "vinyl_mix", 100 } });
        const double cOld = centroid (spectrum (p.render (nz), 24000), sr);
        std::cout << "    VINYL    crackle clicks in 2 s: " << clicks << " | age centroid " << String (cYoung, 0) << " -> " << String (cOld, 0) << " Hz\n";
        check (clicks > 20, "vinyl crackle");
        check (cOld < cYoung * 0.6, "vinyl age band-limits");

        // DESTROY / FUZZ harmonics
        for (int e : { (int) fx::Destroy, (int) fx::Fuzz })
        {
            fxOnly (e, {});
            sp = spectrum (p.render (s220), 24000);
            const double thd = dbRatio (bandEnergy (sp, sr, 300, 20000), bandEnergy (sp, sr, 200, 240));
            std::cout << "    " << String (fx::def (e).name).paddedRight (' ', 8) << " harmonics vs fundamental " << String (thd, 1) << " dB\n";
            check (thd > -10.0, String (fx::def (e).name) + " generates strong harmonics");
        }
    }

    // ---- 2. Every effect is different -----------------------------------------
    std::cout << "\n[2] Distinctness: time-frequency fingerprint distance (mean |dB|) on the riff, effects at defaults\n";
    std::vector<std::vector<float>> fps;
    {
        p.apply ({}); p.settle();
        const auto dryFp = fingerprint (p.renderWarm (riff48), sr, (int) sr);
        float minPair = 1e9f; String minName;
        for (int e = 1; e <= fx::numEffects; ++e)
        {
            p.apply ({}); p.set ("slotA", (float) e); p.settle();
            auto out = p.renderWarm (riff48);
            fps.push_back (fingerprint (out, sr, (int) sr));
            const float dd = fpDistance (fps.back(), dryFp);
            std::cout << "    " << String (fx::def (e).name).paddedRight (' ', 11) << " vs dry " << String (dd, 1).paddedLeft (' ', 5)
                      << " dB   level " << String (rmsDb (out), 1) << " dBFS (input " << String (inRms, 1) << ")\n";
            check (dd > 2.0, String (fx::def (e).name) + " clearly changes the sound");
            check (std::abs (rmsDb (out) - inRms) < 3.0f, String (fx::def (e).name) + " level held by auto gain");
            check (allFinite (out) && peakDb (out) < 0.0f, String (fx::def (e).name) + " finite, below 0 dBFS");
        }
        for (int a = 0; a < fx::numEffects; ++a)
            for (int b = a + 1; b < fx::numEffects; ++b)
            {
                const float d = fpDistance (fps[(size_t) a], fps[(size_t) b]);
                if (d < minPair) { minPair = d; minName = String (fx::def (a + 1).name) + " / " + fx::def (b + 1).name; }
                check (d > 2.0f, "effects differ: " + String (fx::def (a + 1).name) + " vs " + fx::def (b + 1).name);
            }
        std::cout << "    closest pair: " << minName << " at " << String (minPair, 1) << " dB (all 91 pairs checked)\n";
    }

    // ---- 3. Effect mix 0 is transparent ----------------------------------------
    std::cout << "\n[3] Each effect's own Mix/Blend at 0 leaves the signal untouched\n";
    {
        p.apply ({}); p.settle(); p.inst->reset();
        const auto ref = p.render (riff48);
        float worst = -300.0f;
        for (int e = 1; e <= fx::numEffects; ++e)
        {
            p.apply ({}); p.set ("slotA", (float) e); p.set (fx::paramId (e, 5), 0.0f); p.settle(); p.inst->reset();
            auto out = p.render (riff48);
            for (int c = 0; c < 2; ++c) out.addFrom (c, 0, ref, c, 0, ref.getNumSamples(), -1.0f);
            const float d = peakDb (out);
            worst = jmax (worst, d);
            check (d < -60.0f, String (fx::def (e).name) + " mix 0 is transparent");
        }
        std::cout << "    worst difference: " << String (worst, 1) << " dBFS\n";
    }

    // ---- 4. Presets -------------------------------------------------------------
    std::cout << "\n[4] Factory presets: recall, level, distinctness\n";
    {
        const auto& fps2 = naji::factoryPresets();
        std::set<std::pair<int, int>> combos;
        std::vector<std::vector<float>> pf;
        // A host does not resend the program that is already selected, so start from another one.
        p.inst->setCurrentProgram (1); p.settle (0.05);
        for (int i = 0; i < (int) fps2.size(); ++i)
        {
            p.inst->setCurrentProgram (i);
            p.settle (0.05);
            MessageManager::getInstance()->runDispatchLoopUntil (20);
            // Expected normalised value of every parameter
            int wrong = 0;
            for (auto& [id, lp] : p.local)
            {
                float expect = lp->getDefaultValue();
                for (auto& [vid, v] : fps2[(size_t) i].values) if (id == vid) expect = lp->convertTo0to1 (v);
                if (std::abs (p.h (id)->getValue() - expect) > 2.0e-3f) { ++wrong; if (wrong < 4) std::cout << "      " << id << " = " << p.h (id)->getValue() << " expected " << expect << "\n"; }
            }
            check (wrong == 0, String ("preset recalls every parameter: ") + fps2[(size_t) i].name);

            int sa = 0, sb = 0;
            for (auto& [vid, v] : fps2[(size_t) i].values) { if (String (vid) == "slotA") sa = (int) v; if (String (vid) == "slotB") sb = (int) v; }
            combos.insert ({ sa, sb });

            auto out = p.renderWarm (riff48);
            pf.push_back (fingerprint (out, sr, (int) sr));
            const float r = rmsDb (out), pk = peakDb (out);
            std::cout << "    " << String (i + 1).paddedLeft (' ', 2) << " " << String (fps2[(size_t) i].name).paddedRight (' ', 34)
                      << fx::effectName (sa).paddedRight (' ', 11) << "-> " << fx::effectName (sb).paddedRight (' ', 11)
                      << " RMS " << String (r, 1) << "  peak " << String (pk, 1) << "  recall " << (wrong == 0 ? "ok" : "WRONG") << "\n";
            check (allFinite (out) && pk < 0.0f, "preset output finite and below 0 dBFS");
            check (std::abs (r - inRms) < 3.0f, "preset level held");
        }
        check (combos.size() == fps2.size(), "every preset uses a different effect combination");
        float minPair = 1e9f; String minName;
        for (size_t a = 0; a < pf.size(); ++a)
            for (size_t b = a + 1; b < pf.size(); ++b)
            {
                const float d = fpDistance (pf[a], pf[b]);
                if (d < minPair) { minPair = d; minName = String (fps2[a].name) + " / " + fps2[b].name; }
                check (d > 2.0f, "presets differ: " + String (fps2[a].name) + " vs " + fps2[b].name);
            }
        std::cout << "    " << combos.size() << " distinct slot combinations; closest presets: " << minName << " at " << String (minPair, 1) << " dB (153 pairs)\n";
    }

    // ---- 5. Click tests -----------------------------------------------------------
    std::cout << "\n[5] Click test (110 Hz sine; clicks are broadband, so the output is high-passed at 6 kHz;\n"
              << "    ratio = biggest HF spike in the 200 ms after the change / biggest steady HF spike (floor -60 dBFS), <= 1.5 passes)\n";
    {
        struct Jump { String name; std::function<void()> before, after; };
        std::vector<Jump> jumps;
        auto slotJump = [&] (int a, int b)
        {
            jumps.push_back ({ "Slot A " + fx::effectName (a) + " -> " + fx::effectName (b),
                               [&p, a] { p.apply ({ { "drive", 40 } }); p.set ("slotA", (float) a); },
                               [&p, b] { p.set ("slotA", (float) b); } });
        };
        int prev = 0;
        for (int e = 1; e <= fx::numEffects; ++e) { slotJump (prev, e); prev = e; }
        slotJump (prev, 0);
        jumps.push_back ({ "Slot B Off -> Formant", [&p] { p.apply ({ { "drive", 40 } }); }, [&p] { p.set ("slotB", (float) fx::Formant); } });
        jumps.push_back ({ "Drive 0 -> 100",   [&p] { p.apply ({ { "drive", 0 }, { "type", 1 } }); },  [&p] { p.set ("drive", 100); } });
        jumps.push_back ({ "Type Soft -> Fold", [&p] { p.apply ({ { "drive", 60 } }); },               [&p] { p.set ("type", 3); } });
        jumps.push_back ({ "Unhinged on",      [&p] { p.apply ({ { "drive", 60 } }); },                [&p] { p.set ("unhinged", 1); } });
        jumps.push_back ({ "Mix 0 -> 100",     [&p] { p.apply ({ { "drive", 80 }, { "mix", 0 } }); },  [&p] { p.set ("mix", 100); } });
        jumps.push_back ({ "Output -24 -> +12",[&p] { p.apply ({ { "output", -24 } }); },              [&p] { p.set ("output", 12); } });
        jumps.push_back ({ "Width 0 -> 200",   [&p] { p.apply ({ { "width", 0 } }); },                 [&p] { p.set ("width", 200); } });
        jumps.push_back ({ "Comb 0 -> 100",    [&p] { p.apply ({}); },                                 [&p] { p.set ("comb", 100); } });
        jumps.push_back ({ "Gate 0 -> 100",    [&p] { p.apply ({}); },                                 [&p] { p.set ("gate", 100); } });
        jumps.push_back ({ "Crush 0 -> 100",   [&p] { p.apply ({}); },                                 [&p] { p.set ("crush", 100); } });
        jumps.push_back ({ "Formant vowel A->U", [&p] { p.apply ({ { "slotA", 4 } }); },               [&p] { p.set ("formant_vowel", 100); } });

        auto sig = sine (sr, 2.0, 110.0, 0.25f);
        const int bs = 256, changeBlock = (int) sr / bs, changeAt = changeBlock * bs;
        for (auto& j : jumps)
        {
            j.before(); p.settle (0.2);
            p.render (sig, {}, bs);   // warm up on the same signal so auto gain has settled
            auto out = p.render (sig, [&] (int bi) { if (bi == changeBlock) j.after(); }, bs);
            auto hf = highPassed (out, sr, 6000.0);
            const float steadyBefore = maxAbs (hf, changeAt - (int) (0.4 * sr), changeAt);
            const float steadyAfter  = maxAbs (hf, out.getNumSamples() - (int) (0.4 * sr), out.getNumSamples());
            const float during       = maxAbs (hf, changeAt, changeAt + (int) (0.2 * sr));
            const float floorLevel   = 1.0e-3f;   // -60 dBFS: HF this far under a -12 dBFS tone is not audible
            const float ratio = jmax (during, floorLevel) / jmax (floorLevel, jmax (steadyBefore, steadyAfter));
            std::cout << "    " << j.name.paddedRight (' ', 32) << " ratio " << String (ratio, 3)
                      << "  (HF peak during " << String (Decibels::gainToDecibels (during, -120.0f), 1) << " dB, steady "
                      << String (Decibels::gainToDecibels (jmax (steadyBefore, steadyAfter), -120.0f), 1) << " dB)"
                      << (ratio <= 1.5f ? "  ok" : "  CLICK") << "\n";
            check (allFinite (out), "finite during " + j.name);
            check (ratio <= 1.5f, "no click: " + j.name);
        }
    }

    // ---- 6. Dry paths --------------------------------------------------------------
    std::cout << "\n[6] MIX 0 and host bypass\n";
    {
        p.inst->setCurrentProgram (12); p.set ("mix", 0); p.settle();
        auto out = p.render (riff48);
        float maxErr = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = latency; i < riff48.getNumSamples(); ++i)
                maxErr = jmax (maxErr, std::abs (out.getSample (c, i) - riff48.getSample (c, i - latency)));
        std::cout << "    MIX 0 vs dry (latency aligned): " << Decibels::gainToDecibels (maxErr, -200.0f) << " dB\n";
        check (maxErr < 1.0e-5f, "mix 0 is sample-exact dry");

        if (auto* bp = p.inst->getBypassParameter())
        {
            p.set ("mix", 100);
            bp->setValue (1.0f); p.settle();
            out = p.render (riff48);
            maxErr = 0.0f;
            for (int i = latency; i < riff48.getNumSamples(); ++i)
                maxErr = jmax (maxErr, std::abs (out.getSample (0, i) - riff48.getSample (0, i - latency)));
            std::cout << "    bypass vs dry: " << Decibels::gainToDecibels (maxErr, -200.0f) << " dB\n";
            check (maxErr < 1.0e-5f, "bypass is clean and aligned");
            bp->setValue (0.0f); p.settle();
        }
        else check (false, "bypass parameter present");
    }

    // ---- 7. Sample rates / block sizes ------------------------------------------------
    std::cout << "\n[7] Sample rates and block sizes\n";
    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        p.prepare (rate, 512);
        auto in = riff (rate, 3.0);
        bool ok = true;
        for (int e = 1; e <= fx::numEffects; ++e)
        {
            p.apply ({ { "drive", 60 } }); p.set ("slotA", (float) e); p.set ("slotB", (float) (e % fx::numEffects + 1)); p.settle (0.1);
            auto out = p.render (in);
            const bool good = allFinite (out) && peakDb (out) < 0.0f;
            if (! good) std::cout << "      problem with " << fx::def (e).name << " at " << rate << "\n";
            ok = ok && good;
        }
        float diff = 0.0f;
        for (int preset : { 0, 3, 11, 12 })
        {
            p.inst->setCurrentProgram (preset); p.settle (0.1);
            p.inst->reset(); auto a = p.render (in, {}, 512);
            p.inst->reset(); auto b = p.render (in, {}, 0);
            p.inst->reset(); auto c = p.render (in, {}, 37);
            for (int i = 0; i < a.getNumSamples(); ++i)
                diff = jmax (diff, std::abs (a.getSample (0, i) - b.getSample (0, i)), std::abs (a.getSample (0, i) - c.getSample (0, i)));
        }
        std::cout << "    " << String (rate, 0).paddedLeft (' ', 6) << " Hz  latency " << p.inst->getLatencySamples()
                  << "  all effects (both slots) finite and < 0 dBFS: " << (ok ? "yes" : "NO")
                  << "  block-size independence: " << Decibels::gainToDecibels (diff, -200.0f) << " dB\n";
        check (ok, "all effects work at " + String (rate));
        check (diff < 1.0e-4f, "block size independence at " + String (rate));
    }
    p.prepare (48000.0, 512);

    // ---- 8. State --------------------------------------------------------------------
    std::cout << "\n[8] State save / restore\n";
    {
        Random r (99);
        std::map<String, float> setTo;
        for (auto& [id, lp] : p.local)
        {
            float v = r.nextFloat();
            if (lp->isDiscrete()) { const int steps = jmax (2, lp->getNumSteps()); v = (float) roundToInt (v * (float) (steps - 1)) / (float) (steps - 1); }
            p.h (id)->setValue (v);
            setTo[id] = p.h (id)->getValue();
        }
        p.settle (0.05);
        MemoryBlock mb;
        p.inst->getStateInformation (mb);
        p.inst->setCurrentProgram (5); p.settle (0.05);
        p.inst->setStateInformation (mb.getData(), (int) mb.getSize());
        p.settle (0.05);
        MessageManager::getInstance()->runDispatchLoopUntil (20);
        int wrong = 0;
        for (auto& [id, v] : setTo) if (std::abs (p.h (id)->getValue() - v) > 1.0e-3f) ++wrong;
        std::cout << "    " << setTo.size() << " random values, " << wrong << " not restored\n";
        check (wrong == 0, "state restored exactly");
    }

    // ---- 9. User preset save / load (in-process preset manager) ----------------------
    std::cout << "\n[9] User preset save / load\n";
    {
        NajiDistortionProcessor proc;
        proc.presets.loadFactory (9);
        proc.apvts.getParameter ("gran_size")->setValueNotifyingHost (0.123f);
        proc.apvts.getParameter ("drive")->setValueNotifyingHost (0.777f);
        std::map<String, float> before;
        for (auto* prm : proc.getParameters()) if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm)) before[rp->getParameterID()] = rp->getValue();
        const File f = outDir.getChildFile ("test_user_preset.najipreset");
        const bool saved = proc.presets.saveToFile (f, "My Test Preset", "User");
        proc.presets.loadFactory (0);
        const bool loaded = proc.presets.loadFile (f);
        int wrong = 0;
        for (auto* prm : proc.getParameters())
            if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
                if (std::abs (rp->getValue() - before[rp->getParameterID()]) > 1.0e-3f) ++wrong;
        std::cout << "    saved " << (saved ? "yes" : "no") << ", loaded " << (loaded ? "yes" : "no") << ", name '"
                  << proc.presets.getCurrentName() << "', " << wrong << " parameters differ\n";
        check (saved && loaded && wrong == 0 && proc.presets.getCurrentName() == "My Test Preset", "user preset round trip");
        f.deleteFile();
    }

    // ---- 10. CPU -----------------------------------------------------------------------
    std::cout << "\n[10] CPU (% of one core, stereo, 512-sample blocks, effect in slot A, drive 50)\n";
    for (double rate : { 48000.0, 96000.0 })
    {
        p.prepare (rate, 512);
        auto in = riff (rate, 5.0);
        String line = "    " + String (rate / 1000.0, 0) + " kHz:";
        double worst = 0.0;
        for (int e = 1; e <= fx::numEffects; ++e)
        {
            p.apply ({ { "drive", 50 } }); p.set ("slotA", (float) e); p.settle (0.1);
            const auto t0 = Time::getHighResolutionTicks();
            p.render (in);
            const double pct = 100.0 * Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - t0) / 5.0;
            worst = jmax (worst, pct);
            line << " " << String (fx::def (e).name).substring (0, 5) << " " << String (pct, 1);
        }
        std::cout << line << "\n";
        double worstPreset = 0.0; String worstName;
        for (int i = 0; i < 18; ++i)
        {
            p.inst->setCurrentProgram (i); p.settle (0.1);
            const auto t0 = Time::getHighResolutionTicks();
            p.render (in);
            const double pct = 100.0 * Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - t0) / 5.0;
            if (pct > worstPreset) { worstPreset = pct; worstName = naji::factoryPresets()[(size_t) i].name; }
        }
        std::cout << "      heaviest preset: " << worstName << " " << String (worstPreset, 1) << "%\n";
        check (worstPreset < 60.0, "CPU reasonable");
    }
    p.prepare (48000.0, 512);

    // ---- 11. Demo renders ------------------------------------------------------------------
    std::cout << "\n[11] Rendering one demo WAV per preset\n";
    {
        auto in = riff (sr, 8.0);
        writeWav (in, sr, outDir.getChildFile ("00_clean_input.wav"));
        for (int i = 0; i < 18; ++i)
        {
            p.inst->setCurrentProgram (i); p.settle();
            p.render (in);
            const String name = String (i + 1).paddedLeft ('0', 2) + "_" + File::createLegalFileName (naji::factoryPresets()[(size_t) i].name).replaceCharacter (' ', '_') + ".wav";
            writeWav (p.render (in), sr, outDir.getChildFile (name));
        }
        std::cout << "    18 files written\n";
    }

    // ---- 12. Editor -------------------------------------------------------------------------
    std::cout << "\n[12] Editor\n";
    editorSnapshots (outDir);

    p.inst->releaseResources();
    p.inst.reset();

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
