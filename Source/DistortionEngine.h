#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

/*
    NAJI DISTORTION - DSP engine

    Signal flow (wet path):

      in -> subsonic HPF -> oversample -> DRIVE gain -> waveshaper (mode)
         -> [UNHINGED second stage] -> downsample
         -> blend with ALIAS path (same shaper at base rate, no oversampling,
            unfiltered fractional decimation + spectral mirroring)
         -> DC blocker -> CRUSH (bit depth + sample rate) -> TONE
         -> AUTO GAIN (static estimate + adaptive loudness match to the input)
         -> peak limiter

      out = OUTPUT * ( dry_delayed * (1 - MIX) + wet * MIX )

    Modes:
      Soft    - asymmetric tanh saturation -> soft clipping (warm, even harmonics)
      Hard    - hard clipper with a tiny quadratic knee (tight, aggressive)
      Fuzz    - biased asymmetric transistor-style clipper into a second gain stage
      Digital - sine wavefolder blended with a hard clipper (metallic)
      Octave  - full-wave rectifier fuzz (octave-up, ring-mod-ish)
      Melt    - chaotic feedback shaper: the output feeds back through a sine,
                so loud input breaks into unstable, screaming tones
      Glitch  - quantise + Gray-code bit scrambling (broken-DAC destruction)
      Nuke    - fuzz -> fold -> rectify -> bit scramble, all stacked

    AUTO GAIN: the wet level is continuously matched to the input level, so
    raising DRIVE, UNHINGED, TONE, CRUSH or ALIAS never makes it louder; it
    only adds distortion. A peak limiter catches anything that jumps out.
    OUTPUT is the only control that changes the level on purpose.
*/

namespace naji
{

enum class Mode : int { Soft = 0, Hard, Fuzz, Digital, Octave, Melt, Glitch, Nuke };
static constexpr int numModes = 8;

//==============================================================================
namespace shapers
{
    inline float hardClip (float x) noexcept
    {
        constexpr float k = 0.08f;                  // knee half-width
        const float ax = std::abs (x);
        if (ax <= 1.0f - k) return x;
        if (ax >= 1.0f + k) return std::copysign (1.0f, x);
        const float d = ax - (1.0f - k);
        return std::copysign (ax - d * d / (4.0f * k), x);
    }

    inline float soft (float x) noexcept
    {
        // Small DC offset before tanh = asymmetric transfer = even harmonics.
        constexpr float a  = 0.12f;
        constexpr float ta = 0.11942729853438588f;  // tanh(0.12)
        return std::tanh (x + a) - ta;
    }

    inline float fuzzStage (float u) noexcept
    {
        return u >= 0.0f ? 1.0f - std::exp (-u)
                         : -0.55f * (1.0f - std::exp (u / 0.55f));
    }

    inline float fuzz (float x) noexcept
    {
        constexpr float bias = 0.35f;
        constexpr float atBias = 0.29531191f;       // fuzzStage(bias)
        const float y = fuzzStage (x + bias) - atBias;  // silence stays silent
        return std::tanh (2.6f * y);
    }

    inline float digital (float x) noexcept
    {
        const float folded = std::sin (9.0f * std::tanh (x * (1.0f / 9.0f)));
        return 0.8f * folded + 0.2f * hardClip (x);
    }

    inline float octave (float x) noexcept
    {
        // Rectified (octave up) fuzz with a little of the fundamental left in.
        const float r = std::tanh (1.6f * std::abs (x));
        return 0.8f * r + 0.35f * std::tanh (x);
    }

    inline float melt (float x, float& prev) noexcept
    {
        // Feedback through a sine. The feedback amount follows the input, so
        // silence stays silent, but loud input goes chaotic.
        const float fb = juce::jmin (1.8f, 3.0f * std::abs (x));
        const float y = std::tanh (x + fb * std::sin (3.3f * prev));
        prev = y;
        return y;
    }

    inline float glitch (float x) noexcept
    {
        // 11-bit quantiser whose magnitude is Gray-coded: neighbouring levels
        // land far apart, so the waveform shatters into digital junk.
        const float c = hardClip (x);
        const int m = (int) (std::abs (c) * 2047.0f);
        const int gm = m ^ (m >> 1) ^ ((m >> 4) & 0x55);
        const float y = std::copysign ((float) juce::jmin (2047, gm) * (1.0f / 2047.0f), c);
        return 0.75f * y + 0.25f * c;
    }

    // Cheap approximations, used by the NUKE chain (it stacks a lot of stages).
    inline float fastTanh (float x) noexcept
    {
        if (x > 3.0f) return 1.0f;
        if (x < -3.0f) return -1.0f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    inline float fastSin (float x) noexcept
    {
        // Wrap to [-pi, pi], then a 7th-order odd polynomial (max error ~1e-3).
        constexpr float twoPi = 6.283185307f, invTwoPi = 0.159154943f;
        x -= twoPi * std::floor (x * invTwoPi + 0.5f);
        const float x2 = x * x;
        return x * (1.0f + x2 * (-0.16605f + x2 * (0.00761f + x2 * -0.000142f)));
    }

    inline float nuke (float x, float& prev) noexcept
    {
        // fuzz -> fold -> rectify -> bit scramble, with a chaotic feedback layer.
        const float u = x * 2.0f + 0.35f;
        const float fz = fastTanh (2.6f * ((u >= 0.0f ? u / (1.0f + u) : 0.55f * u / (0.55f - u)) - 0.2593f));
        const float b = 0.8f * fastSin (9.0f * fastTanh (fz * (14.0f / 9.0f))) + 0.2f * hardClip (fz * 14.0f);
        const float c = 0.8f * fastTanh (4.8f * std::abs (b)) + 0.35f * fastTanh (3.0f * b);
        const float d = glitch (c * 1.5f + 0.5f * b);
        const float fb = juce::jmin (1.8f, 6.0f * std::abs (b));
        const float m = fastTanh (2.0f * b + fb * fastSin (3.3f * prev));
        prev = m;
        return hardClip (0.55f * d + 0.45f * m);
    }

    inline float process (Mode m, float x, float& state) noexcept
    {
        switch (m)
        {
            case Mode::Soft:    return soft (x);
            case Mode::Hard:    return hardClip (x);
            case Mode::Fuzz:    return fuzz (x);
            case Mode::Digital: return digital (x);
            case Mode::Octave:  return octave (x);
            case Mode::Melt:    return melt (x, state);
            case Mode::Glitch:  return glitch (x);
            case Mode::Nuke:    return nuke (x, state);
        }
        return x;
    }

    // Shaper plus the optional UNHINGED second stage (u = 0..1, smoothed).
    // The first stage's DC is removed before the second stage (otherwise rectifying
    // modes collapse into DC), and a slammed hard clip keeps the energy up.
    // states: [0, n) shaper state, [n, 2n) second-stage state, [2n, 3n) inter-stage DC filter.
    inline float shape (Mode m, float x, float* states, float u, float hpCoeff) noexcept
    {
        const float s = process (m, x, states[(int) m]);
        if (u <= 0.0f) return s;
        float& lp = states[2 * numModes + (int) m];
        lp += hpCoeff * (s - lp);
        const float h = s - lp;
        const float s2 = process (m, 6.0f * h + 2.5f * h * h * h, states[numModes + (int) m]);
        const float wild = 0.6f * s2 + 0.4f * hardClip (4.0f * h);
        return s + (wild - s) * u;
    }
}

//==============================================================================
class DistortionEngine
{
public:
    static constexpr int maxChannels = 2;

    // 8x below 60 kHz, 4x at 88.2/96 kHz, 2x above.
    static int stagesFor (double sr) noexcept    { return sr < 60000.0 ? 3 : (sr < 130000.0 ? 2 : 1); }

    struct Settings
    {
        float drive    = 0.35f;   // 0..1
        float tone     = 0.5f;    // 0..1
        float crush    = 0.0f;    // 0..1
        float alias    = 0.0f;    // 0..1
        float mix      = 1.0f;    // 0..1
        float outDb    = 0.0f;    // dB
        int   mode     = 0;       // naji::Mode
        bool  unhinged = false;
    };

    // Maps the DRIVE knob (0..1) to input gain in dB. UNHINGED adds up to +30 dB.
    static float driveToDb (float d, bool unhinged) noexcept
    {
        d = juce::jlimit (0.0f, 1.0f, d);
        return 48.0f * std::pow (d, 1.2f) + (unhinged ? 30.0f * std::pow (d, 0.7f) : 0.0f);
    }

    void prepare (double newSampleRate, int maxBlockSize, int numChannels)
    {
        sampleRate = newSampleRate;
        numCh = juce::jlimit (1, maxChannels, numChannels);
        maxBlock = juce::jmax (1, maxBlockSize);

        osStages = stagesFor (sampleRate);
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) numCh, (size_t) osStages,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
            true, true);
        oversampler->initProcessing ((size_t) maxBlock);

        latency = (int) std::lround (oversampler->getLatencyInSamples());
        for (auto& d : dryDelay)
            d.assign ((size_t) juce::jmax (1, latency), 0.0f);
        dryBuffer.setSize (maxChannels, maxBlock, false, true, true);

        const double osRate = sampleRate * (1 << osStages);
        driveGain.reset (osRate, 0.05);
        unhingedOS.reset (osRate, 0.05);
        modeFadeStep = (float) (1.0 / (0.03 * osRate));
        modeFadeStepBase = (float) (1.0 / (0.03 * sampleRate));
        interHpOS   = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 20.0 / osRate));
        interHpBase = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 20.0 / sampleRate));

        driveGainBase.reset (sampleRate, 0.05);
        unhingedBase.reset (sampleRate, 0.05);
        toneSm.reset (sampleRate, 0.05);
        crushSm.reset (sampleRate, 0.05);
        aliasSm.reset (sampleRate, 0.05);
        mixSm.reset (sampleRate, 0.03);
        outGain.reset (sampleRate, 0.05);
        makeup.reset (sampleRate, 0.05);

        hpfCoeff = onePoleG (25.0);
        shelfCoeff = onePoleG (2500.0);
        dcR = (float) (1.0 - juce::MathConstants<double>::twoPi * 8.0 / sampleRate);

        // Auto gain: loudness detectors (~200 ms) feeding a gain integrator (~350 ms), well damped.
        detCoeff  = (float) (1.0 - std::exp (-1.0 / (0.20 * sampleRate)));
        glideCoeff = (float) (1.0 - std::exp (-1.0 / (0.35 * sampleRate)));
        limRelease = (float) std::exp (-1.0 / (0.08 * sampleRate));

        reset();
    }

    void reset()
    {
        if (oversampler != nullptr)
            oversampler->reset();

        for (auto& d : dryDelay)
            std::fill (d.begin(), d.end(), 0.0f);
        delayPos = 0;

        for (auto& s : ch)
            s = ChannelState{};

        driveGain.setCurrentAndTargetValue (driveGain.getTargetValue());
        driveGainBase.setCurrentAndTargetValue (driveGainBase.getTargetValue());
        unhingedOS.setCurrentAndTargetValue (unhingedOS.getTargetValue());
        unhingedBase.setCurrentAndTargetValue (unhingedBase.getTargetValue());
        toneSm.setCurrentAndTargetValue (toneSm.getTargetValue());
        crushSm.setCurrentAndTargetValue (crushSm.getTargetValue());
        aliasSm.setCurrentAndTargetValue (aliasSm.getTargetValue());
        mixSm.setCurrentAndTargetValue (mixSm.getTargetValue());
        outGain.setCurrentAndTargetValue (outGain.getTargetValue());
        makeup.setCurrentAndTargetValue (makeup.getTargetValue());
        prevMode = curMode;
        modeFade = modeFadeBase = 1.0f;
        inPow = wetPow = 0.0f;
        adaptDb = 0.0f;
        limEnv = 0.0f;
        parity = 0;
        lastTone = -1.0f;
        updateToneCoeffs (toneSm.getCurrentValue());
    }

    int getLatency() const noexcept { return latency; }

    // Total wet-path level compensation in dB (static estimate + adaptive part).
    float getAutoGainDb() const noexcept
    {
        return juce::Decibels::gainToDecibels (makeup.getCurrentValue()) + adaptDb;
    }

    void setSettings (const Settings& s, bool snap = false)
    {
        const auto m = (Mode) juce::jlimit (0, numModes - 1, s.mode);
        const float g = juce::Decibels::decibelsToGain (driveToDb (s.drive, s.unhinged));

        driveGain.setTargetValue (g);
        driveGainBase.setTargetValue (g);
        unhingedOS.setTargetValue (s.unhinged ? 1.0f : 0.0f);
        unhingedBase.setTargetValue (s.unhinged ? 1.0f : 0.0f);
        toneSm.setTargetValue (s.tone);
        crushSm.setTargetValue (s.crush);
        aliasSm.setTargetValue (s.alias);
        mixSm.setTargetValue (s.mix);
        outGain.setTargetValue (juce::Decibels::decibelsToGain (s.outDb, -100.0f));
        makeup.setTargetValue (juce::Decibels::decibelsToGain (makeupDb (m, s.drive, s.unhinged)));

        if (m != curMode)
        {
            prevMode = curMode;
            curMode = m;
            modeFade = modeFadeBase = snap ? 1.0f : 0.0f;
        }

        if (snap)
        {
            for (auto* sm : { &driveGain, &driveGainBase, &outGain, &makeup })
                sm->setCurrentAndTargetValue (sm->getTargetValue());
            for (auto* sm : { &unhingedOS, &unhingedBase, &toneSm, &crushSm, &aliasSm, &mixSm })
                sm->setCurrentAndTargetValue (sm->getTargetValue());
            updateToneCoeffs (s.tone);
        }
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int n = buffer.getNumSamples();
        for (int start = 0; start < n; start += maxBlock)
            processChunk (buffer, start, juce::jmin (maxBlock, n - start));
    }

    // Host bypass: pass audio through with the same latency, so nothing jumps.
    void processBypassed (juce::AudioBuffer<float>& buffer)
    {
        const int chans = juce::jmin (numCh, buffer.getNumChannels());
        const int n = buffer.getNumSamples();
        int pos = delayPos;
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < chans; ++c)
            {
                float* d = buffer.getWritePointer (c);
                if (latency == 0) continue;
                const float x = d[i];
                d[i] = dryDelay[(size_t) c][(size_t) pos];
                dryDelay[(size_t) c][(size_t) pos] = x;
            }
            if (latency > 0) pos = (pos + 1) % latency;
        }
        delayPos = pos;
    }

private:
    struct ChannelState
    {
        float hpf = 0.0f;                    // subsonic HPF state (oversampled path)
        float hpfA = 0.0f;                   // subsonic HPF state (alias path)
        float dcX = 0.0f, dcY = 0.0f;        // DC blocker
        float holdPhase = 0.0f, held = 0.0f; // CRUSH sample-rate reducer
        float aPhase = 0.0f, aHeld = 0.0f;   // ALIAS fractional decimator
        float shelf = 0.0f;                  // presence shelf state
        float ic1 = 0.0f, ic2 = 0.0f;        // SVF low-pass state
        float stOS[3 * numModes] {};         // stateful shapers (oversampled path)
        float stBase[3 * numModes] {};       // stateful shapers (alias path)
    };

    // Static level compensation, measured on a guitar-level signal (-20 dBFS RMS) with
    // "NajiTestHost calibrate". The adaptive stage corrects whatever is left, so the
    // wet level always tracks the input level.
    static float makeupDb (Mode m, float drive01, bool unhinged) noexcept
    {
        static constexpr float table[2][numModes][11] = {
        { // normal
            { 1.8f, -1.2f, -4.8f, -8.4f, -11.5f, -13.6f, -14.8f, -15.3f, -15.5f, -15.6f, -15.7f },
            { 1.5f, -1.5f, -5.4f, -9.6f, -12.8f, -14.5f, -15.2f, -15.5f, -15.6f, -15.6f, -15.7f },
            { -3.4f, -6.1f, -9.1f, -11.6f, -13.3f, -14.3f, -14.9f, -15.2f, -15.3f, -15.3f, -15.3f },
            { 1.6f, -1.4f, -5.2f, -9.1f, -12.3f, -13.3f, -10.9f, -7.5f, -10.1f, -11.4f, -11.0f },
            { 4.6f, 2.0f, -1.0f, -3.6f, -5.2f, -6.2f, -6.6f, -6.8f, -6.8f, -6.8f, -6.8f },
            { -8.2f, -10.0f, -11.5f, -12.4f, -13.5f, -14.6f, -15.2f, -15.5f, -15.7f, -15.7f, -15.7f },
            { 0.7f, -2.4f, -6.3f, -9.8f, -11.3f, -11.8f, -11.9f, -12.0f, -12.0f, -12.0f, -12.0f },
            { -5.4f, -5.7f, -7.5f, -9.2f, -10.2f, -10.6f, -10.8f, -10.9f, -10.9f, -10.9f, -10.9f },
        },
        { // unhinged
            { -10.7f, -14.5f, -15.4f, -15.6f, -15.6f, -15.7f, -15.7f, -15.7f, -15.7f, -15.7f, -15.8f },
            { -11.7f, -14.8f, -15.5f, -15.6f, -15.6f, -15.7f, -15.7f, -15.7f, -15.8f, -15.8f, -15.8f },
            { -13.9f, -15.1f, -15.4f, -15.4f, -15.5f, -15.5f, -15.5f, -15.5f, -15.6f, -15.6f, -15.6f },
            { -11.4f, -12.4f, -9.6f, -8.0f, -4.9f, -4.8f, -6.3f, -7.8f, -8.3f, -8.4f, -8.5f },
            { -4.9f, -7.9f, -9.4f, -10.6f, -11.3f, -11.3f, -11.4f, -11.4f, -11.4f, -11.4f, -11.4f },
            { -14.7f, -15.5f, -15.7f, -16.0f, -15.9f, -15.8f, -15.8f, -15.8f, -15.7f, -15.7f, -15.7f },
            { -10.9f, -13.0f, -13.5f, -13.6f, -13.7f, -13.7f, -13.7f, -13.7f, -13.8f, -13.8f, -13.8f },
            { -9.6f, -11.3f, -12.7f, -13.1f, -13.2f, -13.2f, -13.2f, -13.2f, -13.2f, -13.2f, -13.2f },
        },
        };
        const float x = juce::jlimit (0.0f, 1.0f, drive01) * 10.0f;
        const int i = juce::jmin (9, (int) x);
        const float f = x - (float) i;
        const auto& row = table[unhinged ? 1 : 0][(int) m];
        return row[i] + (row[i + 1] - row[i]) * f;
    }

    float onePoleG (double hz) const
    {
        const double g = std::tan (juce::MathConstants<double>::pi * hz / sampleRate);
        return (float) (g / (1.0 + g));
    }

    void updateToneCoeffs (float t)
    {
        const double lpHz = juce::jmin (600.0 * std::pow (30.0, juce::jmin (1.0, t / 0.75)),
                                        sampleRate * 0.45);
        const double g = std::tan (juce::MathConstants<double>::pi * lpHz / sampleRate);
        constexpr double k = 1.41421356;
        const double a1 = 1.0 / (1.0 + g * (g + k));
        svfA1 = (float) a1;
        svfA2 = (float) (g * a1);
        svfA3 = (float) (g * g * a1);

        const float shelfDb = t > 0.5f ? 7.0f * (t - 0.5f) / 0.5f : 0.0f;
        shelfAmt = juce::Decibels::decibelsToGain (shelfDb) - 1.0f;
    }

    void processChunk (juce::AudioBuffer<float>& buffer, int start, int n)
    {
        const int chans = juce::jmin (numCh, buffer.getNumChannels());
        if (chans == 0 || n == 0) return;

        // 1) Latency-aligned dry copy + subsonic high-pass on the wet path.
        {
            int pos = delayPos;
            for (int i = 0; i < n; ++i)
            {
                for (int c = 0; c < chans; ++c)
                {
                    float* w = buffer.getWritePointer (c, start);
                    auto& dl = dryDelay[(size_t) c];
                    const float x = w[i];
                    if (latency > 0)
                    {
                        dryBuffer.setSample (c, i, dl[(size_t) pos]);
                        dl[(size_t) pos] = x;
                    }
                    else
                    {
                        dryBuffer.setSample (c, i, x);
                    }

                    auto& s = ch[(size_t) c];
                    const float v = (x - s.hpf) * hpfCoeff;
                    const float lp = v + s.hpf;
                    s.hpf = lp + v;
                    w[i] = x - lp;
                }
                if (latency > 0) pos = (pos + 1) % latency;
            }
            delayPos = pos;
        }

        // 2) Oversampled drive + waveshaping.
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) chans,
                                            (size_t) start, (size_t) n);
        auto up = oversampler->processSamplesUp (block);
        const int upN = (int) up.getNumSamples();
        float* upCh[maxChannels] = { up.getChannelPointer (0),
                                     chans > 1 ? up.getChannelPointer (1) : nullptr };

        for (int i = 0; i < upN; ++i)
        {
            const float g = driveGain.getNextValue();
            const float u = unhingedOS.getNextValue();

            if (modeFade >= 1.0f)
            {
                for (int c = 0; c < chans; ++c)
                    upCh[c][i] = shapers::shape (curMode, upCh[c][i] * g, ch[(size_t) c].stOS, u, interHpOS);
            }
            else
            {
                modeFade = juce::jmin (1.0f, modeFade + modeFadeStep);
                for (int c = 0; c < chans; ++c)
                {
                    const float x = upCh[c][i] * g;
                    const float a = shapers::shape (prevMode, x, ch[(size_t) c].stOS, u, interHpOS);
                    const float b = shapers::shape (curMode, x, ch[(size_t) c].stOS, u, interHpOS);
                    upCh[c][i] = a + (b - a) * modeFade;
                }
            }
        }

        oversampler->processSamplesDown (block);

        // 3) Base-rate: alias path, DC block, crush, tone, auto gain, limiter, mix, output.
        for (int i = 0; i < n; ++i)
        {
            const float t = toneSm.getNextValue();
            if (toneSm.isSmoothing() || (i & 15) == 0)
                if (std::abs (t - lastTone) > 1.0e-6f) { updateToneCoeffs (t); lastTone = t; }

            const float c01  = crushSm.getNextValue();
            const float a01  = aliasSm.getNextValue();
            const float gB   = driveGainBase.getNextValue();
            const float uB   = unhingedBase.getNextValue();
            const float mk   = makeup.getNextValue();
            const float mix  = mixSm.getNextValue();
            const float outG = outGain.getNextValue();

            const bool fadingBase = modeFadeBase < 1.0f;
            if (fadingBase) modeFadeBase = juce::jmin (1.0f, modeFadeBase + modeFadeStepBase);

            // Crush: 16 bits -> 2 bits, 1x -> 32x sample-rate reduction.
            const bool crushing = c01 > 1.0e-4f;
            float steps = 0.0f, holdLen = 1.0f, crushWet = 0.0f;
            if (crushing)
            {
                const float bits = 16.0f - 14.0f * std::pow (c01, 0.6f);
                steps = std::exp2 (bits - 1.0f);
                holdLen = 1.0f + 30.73f * c01 * c01;   // non-integer, so it never locks to a pattern
                crushWet = juce::jmin (1.0f, c01 * 12.0f);
            }

            // Alias: blend toward a non-oversampled shaper, decimate at a
            // fractional rate with no filter, and mirror the spectrum at the top.
            const bool aliasing = a01 > 1.0e-4f;
            const float aBlend = juce::jmin (1.0f, a01 * 2.5f);
            const float aHold  = 1.0f + 47.0f * a01 * a01 * 1.0137f;  // non-integer on purpose
            const float aFlip  = juce::jlimit (0.0f, 0.35f, (a01 - 0.45f) * 0.7f);
            const float flipGain = (parity & 1) ? 1.0f - 2.0f * aFlip : 1.0f;
            parity ^= 1;

            float ys[maxChannels] {};
            float inP = 0.0f, wetP = 0.0f;

            for (int c = 0; c < chans; ++c)
            {
                auto& s = ch[(size_t) c];
                const float* w = buffer.getReadPointer (c, start);
                float y = w[i];
                const float dry = dryBuffer.getSample (c, i);
                inP += dry * dry;

                if (aliasing)
                {
                    const float v = (dry - s.hpfA) * hpfCoeff;
                    const float lp = v + s.hpfA;
                    s.hpfA = lp + v;
                    const float xa = (dry - lp) * gB;

                    float ya = shapers::shape (curMode, xa, s.stBase, uB, interHpBase);
                    if (fadingBase)
                    {
                        const float yp = shapers::shape (prevMode, xa, s.stBase, uB, interHpBase);
                        ya = yp + (ya - yp) * modeFadeBase;
                    }

                    s.aPhase += 1.0f;
                    if (s.aPhase >= aHold) { s.aPhase -= aHold; s.aHeld = ya; }
                    ya = s.aHeld * flipGain;

                    y += (ya - y) * aBlend;
                }
                else
                {
                    s.aPhase = 0.0f;
                }

                // DC blocker
                const float dc = y - s.dcX + dcR * s.dcY;
                s.dcX = y;
                s.dcY = dc;
                y = dc;

                if (crushing)
                {
                    s.holdPhase += 1.0f;
                    if (s.holdPhase >= holdLen)
                    {
                        s.holdPhase -= holdLen;
                        s.held = std::round (y * steps) / steps;
                    }
                    y += (s.held - y) * crushWet;
                }
                else
                {
                    s.holdPhase = 0.0f;
                    s.held = y;
                }

                // Presence shelf
                {
                    const float v = (y - s.shelf) * shelfCoeff;
                    const float lp = v + s.shelf;
                    s.shelf = lp + v;
                    y += shelfAmt * (y - lp);
                }

                // 12 dB/oct low-pass (TPT SVF)
                {
                    const float v3 = y - s.ic2;
                    const float v1 = svfA1 * s.ic1 + svfA2 * v3;
                    const float v2 = s.ic2 + svfA2 * s.ic1 + svfA3 * v3;
                    s.ic1 = 2.0f * v1 - s.ic1;
                    s.ic2 = 2.0f * v2 - s.ic2;
                    y = v2;
                }

                ys[c] = y * mk;
            }

            // Auto gain (stereo linked): the static estimate is already in ys; the
            // adaptive part keeps the final wet loudness (measured after the limiter)
            // equal to the input loudness.
            const float ag = juce::Decibels::decibelsToGain (adaptDb);

            // Peak limiter on the wet signal (instant attack, 80 ms release).
            float peak = 0.0f;
            for (int c = 0; c < chans; ++c)
                peak = juce::jmax (peak, std::abs (ys[c] * ag));
            limEnv = juce::jmax (peak, limEnv * limRelease);
            const float lg = limEnv > limCeiling ? limCeiling / limEnv : 1.0f;

            for (int c = 0; c < chans; ++c)
            {
                const float w = ys[c] * ag * lg;
                wetP += w * w;
            }
            inPow  += (inP  - inPow)  * detCoeff;
            wetPow += (wetP - wetPow) * detCoeff;
            if (inPow > 1.0e-8f * (float) chans)
            {
                // Error relative to the gain already applied, so this converges
                // even when the limiter is working.
                const float err = 10.0f * std::log10 ((inPow + 1.0e-12f) / (wetPow + 1.0e-12f));
                adaptDb = juce::jlimit (-36.0f, 18.0f, adaptDb + err * glideCoeff);
            }

            for (int c = 0; c < chans; ++c)
            {
                const float wet = ys[c] * ag * lg;
                const float dry = dryBuffer.getSample (c, i);
                buffer.getWritePointer (c, start)[i] = outG * (dry + (wet - dry) * mix);
            }
        }
    }

    //==========================================================================
    double sampleRate = 44100.0;
    int numCh = 2, maxBlock = 512, latency = 0, osStages = 3;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::array<std::vector<float>, maxChannels> dryDelay;
    int delayPos = 0;
    juce::AudioBuffer<float> dryBuffer;

    std::array<ChannelState, maxChannels> ch {};

    using Mult = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;
    Mult driveGain { 1.0f }, driveGainBase { 1.0f }, outGain { 1.0f }, makeup { 1.0f };
    juce::SmoothedValue<float> unhingedOS { 0.0f }, unhingedBase { 0.0f };
    juce::SmoothedValue<float> toneSm { 0.5f }, crushSm { 0.0f }, aliasSm { 0.0f }, mixSm { 1.0f };

    Mode curMode = Mode::Soft, prevMode = Mode::Soft;
    float modeFade = 1.0f, modeFadeStep = 0.001f, modeFadeBase = 1.0f, modeFadeStepBase = 0.01f;
    float interHpOS = 0.0001f, interHpBase = 0.001f;

    float hpfCoeff = 0.0f, shelfCoeff = 0.0f, dcR = 0.999f;
    float svfA1 = 0.0f, svfA2 = 0.0f, svfA3 = 0.0f, shelfAmt = 0.0f, lastTone = -1.0f;

    float detCoeff = 0.0f, glideCoeff = 0.0f, inPow = 0.0f, wetPow = 0.0f, adaptDb = 0.0f;
    float limEnv = 0.0f, limRelease = 0.999f;
    static constexpr float limCeiling = 0.891f;   // -1 dBFS
    int parity = 0;
};

} // namespace naji
