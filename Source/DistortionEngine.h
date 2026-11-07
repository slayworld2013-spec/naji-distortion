#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

/*
    NAJI DISTORTION - DSP engine

    Signal flow (wet path):

      in -> subsonic HPF -> 4x oversample -> DRIVE gain -> waveshaper (mode)
         -> downsample -> DC blocker -> CRUSH (bit depth + sample rate)
         -> TONE (low-pass + presence shelf) -> level compensation

      out = OUTPUT * ( dry_delayed * (1 - MIX) + wet * MIX )

    Modes:
      Soft    - asymmetric tanh saturation -> soft clipping (warm, even harmonics)
      Hard    - hard clipper with a tiny quadratic knee (tight, aggressive)
      Fuzz    - biased asymmetric transistor-style clipper into a second gain stage
      Digital - sine wavefolder blended with a hard clipper (metallic, ridiculous)

    All shaping runs at 8x (44.1/48 kHz) or 4x (88.2/96 kHz) the host rate to keep aliasing down. Every
    control is smoothed per sample, and switching mode crossfades the two
    shapers over 30 ms, so automation never clicks. The dry signal is delayed
    by the oversampling latency so MIX never comb-filters.
*/

namespace naji
{

enum class Mode : int { Soft = 0, Hard, Fuzz, Digital };
static constexpr int numModes = 4;

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
        // Positive half: soft exponential knee to +1.
        // Negative half: clips earlier and harder (to -0.55).
        return u >= 0.0f ? 1.0f - std::exp (-u)
                         : -0.55f * (1.0f - std::exp (u / 0.55f));
    }

    inline float fuzz (float x) noexcept
    {
        constexpr float bias = 0.35f;
        constexpr float atBias = 0.29531191f;       // fuzzStage(bias) = 1 - exp(-0.35)
        const float y = fuzzStage (x + bias) - atBias;  // silence stays silent
        return std::tanh (2.6f * y);
    }

    inline float digital (float x) noexcept
    {
        // Fold depth is bounded so extreme drive gives dense folding instead of noise.
        const float folded = std::sin (9.0f * std::tanh (x * (1.0f / 9.0f)));
        return 0.8f * folded + 0.2f * hardClip (x);
    }

    inline float process (Mode m, float x) noexcept
    {
        switch (m)
        {
            case Mode::Soft:    return soft (x);
            case Mode::Hard:    return hardClip (x);
            case Mode::Fuzz:    return fuzz (x);
            case Mode::Digital: return digital (x);
        }
        return x;
    }
}

//==============================================================================
class DistortionEngine
{
public:
    static constexpr int maxChannels = 2;

    // 8x below 60 kHz, 4x at 88.2/96 kHz, 2x above: aliasing stays low and CPU stays flat.
    static int stagesFor (double sr) noexcept    { return sr < 60000.0 ? 3 : (sr < 130000.0 ? 2 : 1); }

    struct Settings
    {
        float drive  = 0.35f;   // 0..1
        float tone   = 0.5f;    // 0..1
        float crush  = 0.0f;    // 0..1
        float mix    = 1.0f;    // 0..1
        float outDb  = 0.0f;    // dB
        int   mode   = 0;       // naji::Mode
    };

    // Maps the DRIVE knob (0..1) to input gain in dB.
    static float driveToDb (float d) noexcept   { return 48.0f * std::pow (juce::jlimit (0.0f, 1.0f, d), 1.2f); }

    void prepare (double newSampleRate, int maxBlockSize, int numChannels)
    {
        sampleRate = newSampleRate;
        numCh = juce::jlimit (1, maxChannels, numChannels);
        maxBlock = juce::jmax (1, maxBlockSize);

        osStages = stagesFor (sampleRate);
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) numCh, (size_t) osStages,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
            true,   // max quality filters
            true);  // integer latency, so the dry path can be aligned exactly
        oversampler->initProcessing ((size_t) maxBlock);

        latency = (int) std::lround (oversampler->getLatencyInSamples());
        for (auto& d : dryDelay)
            d.assign ((size_t) juce::jmax (1, latency), 0.0f);
        dryBuffer.setSize (maxChannels, maxBlock, false, true, true);

        const double osRate = sampleRate * (1 << osStages);
        driveGain.reset (osRate, 0.05);
        modeFadeStep = (float) (1.0 / (0.03 * osRate));

        toneSm.reset (sampleRate, 0.05);
        crushSm.reset (sampleRate, 0.05);
        mixSm.reset (sampleRate, 0.03);
        outGain.reset (sampleRate, 0.05);
        makeup.reset (sampleRate, 0.05);

        // One-pole coefficients
        hpfCoeff = onePoleG (25.0);
        shelfCoeff = onePoleG (2500.0);
        dcR = (float) (1.0 - juce::MathConstants<double>::twoPi * 8.0 / sampleRate);

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

        // Jump straight to the current targets (no ramp after a reset).
        driveGain.setCurrentAndTargetValue (driveGain.getTargetValue());
        toneSm.setCurrentAndTargetValue (toneSm.getTargetValue());
        crushSm.setCurrentAndTargetValue (crushSm.getTargetValue());
        mixSm.setCurrentAndTargetValue (mixSm.getTargetValue());
        outGain.setCurrentAndTargetValue (outGain.getTargetValue());
        makeup.setCurrentAndTargetValue (makeup.getTargetValue());
        prevMode = curMode;
        modeFade = 1.0f;
        updateToneCoeffs (toneSm.getCurrentValue());
    }

    int getLatency() const noexcept { return latency; }

    void setSettings (const Settings& s, bool snap = false)
    {
        const float gainDb = driveToDb (s.drive);
        const auto  m = (Mode) juce::jlimit (0, numModes - 1, s.mode);

        driveGain.setTargetValue (juce::Decibels::decibelsToGain (gainDb));
        toneSm.setTargetValue (s.tone);
        crushSm.setTargetValue (s.crush);
        mixSm.setTargetValue (s.mix);
        outGain.setTargetValue (juce::Decibels::decibelsToGain (s.outDb, -100.0f));
        makeup.setTargetValue (juce::Decibels::decibelsToGain (makeupDb (m, s.drive)));

        if (m != curMode)
        {
            prevMode = curMode;
            curMode = m;
            modeFade = snap ? 1.0f : 0.0f;
        }

        if (snap)
        {
            driveGain.setCurrentAndTargetValue (driveGain.getTargetValue());
            toneSm.setCurrentAndTargetValue (s.tone);
            crushSm.setCurrentAndTargetValue (s.crush);
            mixSm.setCurrentAndTargetValue (s.mix);
            outGain.setCurrentAndTargetValue (outGain.getTargetValue());
            makeup.setCurrentAndTargetValue (makeup.getTargetValue());
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
        float hpf = 0.0f;                    // subsonic HPF state
        float dcX = 0.0f, dcY = 0.0f;        // DC blocker
        float holdPhase = 0.0f, held = 0.0f; // sample-rate reducer
        float shelf = 0.0f;                  // presence shelf state
        float ic1 = 0.0f, ic2 = 0.0f;        // SVF low-pass state
    };

    // Per-mode level compensation, measured on a guitar-level signal (-20 dBFS RMS).
    // DRIVE 0 -> 100 rises about +6 dB in every mode, so turning it up
    // feels like "more" without blowing up the mix, and switching modes keeps a similar level.
    static float makeupDb (Mode m, float drive01) noexcept
    {
        static constexpr float table[numModes][11] = {
            { 0.0f, -2.4f, -5.4f, -8.6f, -11.3f, -13.3f, -14.5f, -14.9f, -14.9f, -14.5f, -14.0f }, // Soft
            { -0.3f, -2.7f, -6.0f, -9.6f, -12.6f, -14.4f, -15.2f, -15.3f, -15.0f, -14.6f, -14.1f }, // Hard
            { -5.2f, -7.4f, -9.9f, -12.1f, -13.8f, -14.7f, -15.1f, -15.0f, -14.7f, -14.3f, -13.8f }, // Fuzz
            { -0.2f, -2.6f, -5.8f, -9.2f, -12.1f, -13.5f, -12.9f, -11.0f, -9.6f, -9.6f, -9.7f }, // Digital
        };
        const float x = juce::jlimit (0.0f, 1.0f, drive01) * 10.0f;
        const int i = juce::jmin (9, (int) x);
        const float f = x - (float) i;
        const auto& row = table[(int) m];
        return row[i] + (row[i + 1] - row[i]) * f;
    }

    float onePoleG (double hz) const
    {
        const double g = std::tan (juce::MathConstants<double>::pi * hz / sampleRate);
        return (float) (g / (1.0 + g));
    }

    void updateToneCoeffs (float t)
    {
        // Low-pass sweeps 600 Hz -> 18 kHz over the first 3/4 of the knob,
        // then a presence shelf adds up to +7 dB of bite at the top.
        const double lpHz = juce::jmin (600.0 * std::pow (30.0, juce::jmin (1.0, t / 0.75)),
                                        sampleRate * 0.45);
        const double g = std::tan (juce::MathConstants<double>::pi * lpHz / sampleRate);
        constexpr double k = 1.41421356; // Q = 0.707
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
        auto up = oversampler->processSamplesUp (block);   // 2x / 4x / 8x
        const int upN = (int) up.getNumSamples();
        float* upCh[maxChannels] = { up.getChannelPointer (0),
                                     chans > 1 ? up.getChannelPointer (1) : nullptr };

        for (int i = 0; i < upN; ++i)
        {
            const float g = driveGain.getNextValue();

            if (modeFade >= 1.0f)
            {
                for (int c = 0; c < chans; ++c)
                    upCh[c][i] = shapers::process (curMode, upCh[c][i] * g);
            }
            else
            {
                modeFade = juce::jmin (1.0f, modeFade + modeFadeStep);
                for (int c = 0; c < chans; ++c)
                {
                    const float x = upCh[c][i] * g;
                    const float a = shapers::process (prevMode, x);
                    const float b = shapers::process (curMode, x);
                    upCh[c][i] = a + (b - a) * modeFade;
                }
            }
        }

        oversampler->processSamplesDown (block);

        // 3) Base-rate post processing: DC block, crush, tone, makeup, mix, output.
        for (int i = 0; i < n; ++i)
        {
            const float t = toneSm.getNextValue();
            if (toneSm.isSmoothing() || (i & 15) == 0)
                if (std::abs (t - lastTone) > 1.0e-6f) { updateToneCoeffs (t); lastTone = t; }

            const float c01  = crushSm.getNextValue();
            const float mk   = makeup.getNextValue();
            const float mix  = mixSm.getNextValue();
            const float outG = outGain.getNextValue();

            // Crush mapping: 16 bits -> 2 bits, 1x -> 32x sample-rate reduction.
            const bool crushing = c01 > 1.0e-4f;
            float steps = 0.0f, holdLen = 1.0f, crushWet = 0.0f;
            if (crushing)
            {
                const float bits = 16.0f - 14.0f * std::pow (c01, 0.6f);
                steps = std::exp2 (bits - 1.0f);
                holdLen = 1.0f + 31.0f * c01 * c01;
                crushWet = juce::jmin (1.0f, c01 * 12.0f);   // fade in, no step at 0
            }

            for (int c = 0; c < chans; ++c)
            {
                auto& s = ch[(size_t) c];
                float* w = buffer.getWritePointer (c, start);
                float y = w[i];

                // DC blocker (fuzz bias + folding can leave DC)
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
                        s.held = std::round (y * steps) / steps;   // mid-tread: silence stays silent
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

                // 12 dB/oct low-pass (TPT state-variable filter)
                {
                    const float v3 = y - s.ic2;
                    const float v1 = svfA1 * s.ic1 + svfA2 * v3;
                    const float v2 = s.ic2 + svfA2 * s.ic1 + svfA3 * v3;
                    s.ic1 = 2.0f * v1 - s.ic1;
                    s.ic2 = 2.0f * v2 - s.ic2;
                    y = v2;
                }

                const float dry = dryBuffer.getSample (c, i);
                w[i] = outG * (dry + (y * mk - dry) * mix);
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

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> driveGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> makeup { 1.0f };
    juce::SmoothedValue<float> toneSm { 0.5f }, crushSm { 0.0f }, mixSm { 1.0f };

    Mode curMode = Mode::Soft, prevMode = Mode::Soft;
    float modeFade = 1.0f, modeFadeStep = 0.001f;

    float hpfCoeff = 0.0f, shelfCoeff = 0.0f, dcR = 0.999f;
    float svfA1 = 0.0f, svfA2 = 0.0f, svfA3 = 0.0f, shelfAmt = 0.0f, lastTone = -1.0f;
};

} // namespace naji
