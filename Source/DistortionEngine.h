#pragma once

#include <juce_dsp/juce_dsp.h>
#include "dsp/Effects.h"
#include <array>
#include <cmath>
#include <memory>
#include <vector>

/*
    NAJI DISTORTION - main signal chain

      in -> subsonic HPF
         -> SLOT A effect (any of the 14 engines, or off)
         -> oversampled DRIVE (Soft / Hard / Tube / Fold) [+ UNHINGED 2nd stage]
         -> ALIAS blend (same shaper at base rate, unfiltered fractional
            decimation + spectral mirroring)
         -> DC blocker
         -> SLOT B effect
         -> LAB: CRUSH (bits) -> DOWNSAMPLE -> COMB -> TONE -> GATE (tempo
            synced) -> WIDTH (M/S + decorrelation)
         -> AUTO GAIN (calibrated static estimate + adaptive loudness match)
         -> peak limiter

      out = OUTPUT * ( dry_delayed * (1 - MIX) + wet * MIX )

    Switching an effect slot fades that slot's contribution out (20 ms),
    swaps the engine, and fades back in, so it never clicks.
*/

namespace naji
{
    enum class DriveType : int { Soft = 0, Hard, Tube, Fold };
    static constexpr int numDriveTypes = 4;

    namespace lab
    {
        inline constexpr float gateBeats[] = { 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f };
        inline constexpr const char* gateNames[] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
        constexpr int numGateRates = 6;
        inline float combHz (float v) { return fx::map::expo (v, 40.0f, 2000.0f); }
    }

    // Everything the engine needs, read from the parameters once per block.
    struct EngineParams
    {
        int   slot[2] = { fx::Glitch, fx::Off };
        float fxv[fx::numEffects + 1][fx::numParams] {};   // [effect id][param], 0..1
        float drive = 0.35f, tone = 0.5f, crush = 0.0f, downsample = 0.0f, alias = 0.0f;
        float comb = 0.0f, combFreq = 0.4f, width = 1.0f, gate = 0.0f;
        int   gateRate = 3;
        float mix = 1.0f, outDb = 0.0f;
        int   driveType = 0;
        bool  unhinged = false;

        EngineParams()
        {
            for (int e = 1; e <= fx::numEffects; ++e)
                for (int p = 0; p < fx::numParams; ++p)
                    fxv[e][p] = fx::def (e).p[p].def;
        }
    };

    //==========================================================================
    namespace shapers
    {
        inline float hardClip (float x) noexcept
        {
            constexpr float k = 0.08f;
            const float ax = std::abs (x);
            if (ax <= 1.0f - k) return x;
            if (ax >= 1.0f + k) return std::copysign (1.0f, x);
            const float d = ax - (1.0f - k);
            return std::copysign (ax - d * d / (4.0f * k), x);
        }
        inline float soft (float x) noexcept
        {
            constexpr float a = 0.12f, ta = 0.11942729853438588f;
            return std::tanh (x + a) - ta;
        }
        inline float tubeStage (float u) noexcept
        {
            return u >= 0.0f ? 1.0f - std::exp (-u) : -0.55f * (1.0f - std::exp (u / 0.55f));
        }
        inline float tube (float x) noexcept
        {
            constexpr float bias = 0.35f, atBias = 0.29531191f;
            return std::tanh (2.6f * (tubeStage (x + bias) - atBias));
        }
        inline float fold (float x) noexcept
        {
            const float folded = std::sin (9.0f * std::tanh (x * (1.0f / 9.0f)));
            return 0.8f * folded + 0.2f * hardClip (x);
        }
        inline float process (int m, float x) noexcept
        {
            switch (m)
            {
                case 0: return soft (x);
                case 1: return hardClip (x);
                case 2: return tube (x);
                default: return fold (x);
            }
        }
        // Shaper + optional UNHINGED second stage (u = 0..1, smoothed).
        inline float shape (int m, float x, float& lp, float u, float hpCoeff) noexcept
        {
            const float s = process (m, x);
            if (u <= 0.0f) return s;
            lp += hpCoeff * (s - lp);
            const float h = s - lp;
            const float s2 = process (m, 6.0f * h + 2.5f * h * h * h);
            const float wild = 0.6f * s2 + 0.4f * hardClip (4.0f * h);
            return s + (wild - s) * u;
        }
    }

    //==========================================================================
    // One effect slot: owns an instance of every engine (allocated in prepare,
    // never on the audio thread) and crossfades when the selection changes.
    class EffectSlot
    {
    public:
        void prepare (double sampleRate, int maxBlock)
        {
            sr = sampleRate;
            maxN = maxBlock;
            for (int id = 1; id <= fx::numEffects; ++id)
            {
                engines[(size_t) id] = dsp::createEffect (id);
                engines[(size_t) id]->prepare (sr, maxBlock);
            }
            for (auto& b : pbuf) b.assign ((size_t) maxBlock, 0.0f);
            dry.setSize (2, maxBlock);
            for (auto& s : smooth) s.reset (sr, 0.05);
            fadeStep = (float) (1.0 / (0.02 * sr));
            detA = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
            refRelease = (float) std::exp (-1.0 / (3.0 * sr));
            prerollLen = (int) (0.015 * sr);
            levAtt = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
            levRel = (float) (1.0 - std::exp (-1.0 / (0.5 * sr)));
            reset();
        }

        void reset()
        {
            inEnv = wetEnv = inRef = 0.0f;
            levelGain = 1.0f;
            preroll = 0;
            current = target;
            gain = current == fx::Off ? 0.0f : 1.0f;
            if (current != fx::Off) engines[(size_t) current]->reset();
            for (int p = 0; p < fx::numParams; ++p) smooth[(size_t) p].setCurrentAndTargetValue (targetParams[p]);
        }

        void setTarget (int id, const float* params)
        {
            target = juce::jlimit (0, fx::numEffects, id);
            for (int p = 0; p < fx::numParams; ++p) targetParams[p] = params[p];
        }

        int getCurrent() const noexcept { return current; }

        void process (float* const* ch, int numCh, int n, const dsp::Transport& t)
        {
            if (current == target)
                for (int p = 0; p < fx::numParams; ++p) smooth[(size_t) p].setTargetValue (targetParams[p]);

            int pos = 0;
            while (pos < n)
            {
                int len = n - pos;
                if (current != target)
                {
                    if (current == fx::Off || gain <= 0.0f) { switchNow(); continue; }
                    len = juce::jmin (len, (int) std::ceil (gain / fadeStep));
                    runSegment (ch, numCh, pos, len, t, -fadeStep);
                    if (gain <= 0.0f) switchNow();
                }
                else
                {
                    runSegment (ch, numCh, pos, len, t, gain < 1.0f ? fadeStep : 0.0f);
                }
                pos += len;
            }
        }

    private:
        void switchNow()
        {
            current = target;
            gain = 0.0f;
            inEnv = wetEnv = 0.0f;
            levelGain = 1.0f;
            preroll = prerollLen;   // let the new engine and the leveller settle before fading in
            if (current != fx::Off) engines[(size_t) current]->reset();
            for (int p = 0; p < fx::numParams; ++p) smooth[(size_t) p].setCurrentAndTargetValue (targetParams[p]);
        }

        void runSegment (float* const* ch, int numCh, int start, int len, const dsp::Transport& t, float step)
        {
            if (current == fx::Off || len <= 0) return;

            for (int c = 0; c < numCh; ++c) dry.copyFrom (c, 0, ch[c] + start, len);
            dsp::ParamBlock pb;
            for (int p = 0; p < fx::numParams; ++p)
            {
                auto& b = pbuf[(size_t) p];
                for (int i = 0; i < len; ++i) b[(size_t) i] = smooth[(size_t) p].getNextValue();
                pb.v[p] = b.data();
            }

            float* ptrs[2] = { ch[0] + start, numCh > 1 ? ch[1] + start : nullptr };
            dsp::Transport ts = t;
            ts.ppq = t.ppq + (double) start * juce::jlimit (20.0, 400.0, t.bpm) / (60.0 * sr);
            engines[(size_t) current]->process (ptrs, numCh, len, pb, ts);

            // Slot leveller: keeps each effect's output near its input level, so switching effects
            // does not jump in volume. Turns down fast (10 ms), brings quiet effects up slowly
            // (0.5 s, at most +12 dB), and holds while the input is silent so tails can sustain.
            for (int i = 0; i < len; ++i)
            {
                float inP = 0.0f, wetP = 0.0f;
                for (int c = 0; c < numCh; ++c)
                {
                    const float d = dry.getSample (c, i), w = ptrs[c][i];
                    inP += d * d;
                    wetP += w * w;
                }
                inEnv += (inP - inEnv) * detA;
                wetEnv += (wetP - wetEnv) * detA;
                // Reference: input level with a slow (3 s) release, so tails, freezes and
                // delayed material are allowed to outlast the input.
                inRef = juce::jmax (inEnv, inRef * refRelease);
                if (inRef > 1.0e-7f)
                {
                    float tgt = 1.0f;
                    if (wetEnv > inRef)       tgt = std::sqrt (inRef / wetEnv);                                   // louder than the input: down
                    else if (wetEnv < inEnv)  tgt = juce::jmin (4.0f, std::sqrt (inEnv / (wetEnv + inEnv * 1.0e-3f))); // quieter: up
                    levelGain += (tgt - levelGain) * (tgt < levelGain ? levAtt : levRel);
                }
                if (preroll > 0) --preroll;
                else gain = juce::jlimit (0.0f, 1.0f, gain + step);
                for (int c = 0; c < numCh; ++c)
                {
                    const float d = dry.getSample (c, i);
                    ptrs[c][i] = d + (ptrs[c][i] * levelGain - d) * gain;
                }
            }
        }

        double sr = 48000.0;
        int maxN = 512;
        std::array<std::unique_ptr<dsp::Effect>, fx::numEffects + 1> engines;
        std::array<std::vector<float>, fx::numParams> pbuf;
        std::array<juce::SmoothedValue<float>, fx::numParams> smooth;
        juce::AudioBuffer<float> dry;
        int current = fx::Off, target = fx::Off;
        float targetParams[fx::numParams] {};
        float gain = 0.0f, fadeStep = 0.001f;
        float inEnv = 0.0f, wetEnv = 0.0f, inRef = 0.0f, levelGain = 1.0f, detA = 0.001f, levAtt = 0.01f, levRel = 0.0001f, refRelease = 0.9999f;
        int preroll = 0, prerollLen = 720;
    };

    //==========================================================================
    class DistortionEngine
    {
    public:
        static constexpr int maxChannels = 2;

        static int stagesFor (double sr) noexcept { return sr < 60000.0 ? 3 : (sr < 130000.0 ? 2 : 1); }

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
                (size_t) numCh, (size_t) osStages, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
            oversampler->initProcessing ((size_t) maxBlock);

            latency = (int) std::lround (oversampler->getLatencyInSamples());
            for (auto& d : dryDelay)   d.assign ((size_t) juce::jmax (1, latency), 0.0f);
            for (auto& d : aliasDelay) d.assign ((size_t) juce::jmax (1, latency), 0.0f);
            dryBuffer.setSize (maxChannels, maxBlock, false, true, true);
            aliasBuffer.setSize (maxChannels, maxBlock, false, true, true);

            for (auto& s : slots) s.prepare (sampleRate, maxBlock);

            const double osRate = sampleRate * (1 << osStages);
            driveGain.reset (osRate, 0.05);
            unhingedOS.reset (osRate, 0.05);
            modeFadeStep = (float) (1.0 / (0.03 * osRate));
            modeFadeStepBase = (float) (1.0 / (0.03 * sampleRate));
            interHpOS   = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 20.0 / osRate));
            interHpBase = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 20.0 / sampleRate));

            for (auto* sm : { &driveGainBase, &outGain, &makeup }) sm->reset (sampleRate, 0.05);
            for (auto* sm : { &unhingedBase, &toneSm, &crushSm, &downSm, &aliasSm, &combSm, &combFreqSm, &widthSm, &gateSm })
                sm->reset (sampleRate, 0.05);
            mixSm.reset (sampleRate, 0.03);

            hpfCoeff = onePoleG (25.0);
            shelfCoeff = onePoleG (2500.0);
            dcR = (float) (1.0 - juce::MathConstants<double>::twoPi * 8.0 / sampleRate);

            detCoeff   = (float) (1.0 - std::exp (-1.0 / (0.30 * sampleRate)));
            glideCoeff = (float) (1.0 - std::exp (-1.0 / (0.60 * sampleRate)));
            detFast    = (float) (1.0 - std::exp (-1.0 / (0.05 * sampleRate)));
            refDecay   = (float) std::exp (-1.0 / (3.0 * sampleRate));
            attackCoeff = (float) (1.0 - std::exp (-1.0 / (0.12 * sampleRate)));
            limRelease = (float) std::exp (-1.0 / (0.08 * sampleRate));
            gateSmoothA = (float) (1.0 - std::exp (-1.0 / (0.0015 * sampleRate)));

            for (auto& d : combDl) d.prepare ((int) (sampleRate / 35.0) + 8);
            for (auto& o : combDamp) o.setHz (6500.0f, sampleRate);

            reset();
        }

        void reset()
        {
            if (oversampler != nullptr) oversampler->reset();
            for (auto& d : dryDelay) std::fill (d.begin(), d.end(), 0.0f);
            for (auto& d : aliasDelay) std::fill (d.begin(), d.end(), 0.0f);
            delayPos = 0;
            for (auto& s : ch) s = ChannelState{};
            for (auto& s : slots) s.reset();
            for (auto& d : combDl) d.reset();
            for (auto& o : combDamp) o.reset();
            for (auto& c : widthAp) for (auto& a : c) a.reset();

            for (auto* sm : { &driveGain, &driveGainBase, &outGain, &makeup })
                sm->setCurrentAndTargetValue (sm->getTargetValue());
            for (auto* sm : { &unhingedOS, &unhingedBase, &toneSm, &crushSm, &downSm, &aliasSm, &combSm, &combFreqSm,
                              &widthSm, &gateSm, &mixSm })
                sm->setCurrentAndTargetValue (sm->getTargetValue());
            prevMode = curMode;
            modeFade = modeFadeBase = 1.0f;
            inPow = wetPow = inPeakRef = 0.0f;
            adaptDb = 0.0f;
            limEnv = 0.0f;
            parity = 0;
            gateGain = 1.0f;
            clock.beat = 0.0;
            lastTone = -1.0f;
            updateToneCoeffs (toneSm.getCurrentValue());
        }

        int getLatency() const noexcept { return latency; }
        int getSlotEffect (int s) const noexcept { return slots[(size_t) s].getCurrent(); }

        float getAutoGainDb() const noexcept
        {
            return juce::Decibels::gainToDecibels (makeup.getCurrentValue()) + adaptDb;
        }

        void setParams (const EngineParams& p, bool snap = false)
        {
            const int m = juce::jlimit (0, numDriveTypes - 1, p.driveType);
            const float g = juce::Decibels::decibelsToGain (driveToDb (p.drive, p.unhinged));

            for (int s = 0; s < 2; ++s)
            {
                const int id = juce::jlimit (0, fx::numEffects, p.slot[s]);
                slots[(size_t) s].setTarget (id, p.fxv[id == 0 ? 1 : id]);
            }

            driveGain.setTargetValue (g);
            driveGainBase.setTargetValue (g);
            unhingedOS.setTargetValue (p.unhinged ? 1.0f : 0.0f);
            unhingedBase.setTargetValue (p.unhinged ? 1.0f : 0.0f);
            toneSm.setTargetValue (p.tone);
            crushSm.setTargetValue (p.crush);
            downSm.setTargetValue (p.downsample);
            aliasSm.setTargetValue (p.alias);
            combSm.setTargetValue (p.comb);
            combFreqSm.setTargetValue (p.combFreq);
            widthSm.setTargetValue (juce::jlimit (0.0f, 2.0f, p.width));
            gateSm.setTargetValue (p.gate);
            gateRate = juce::jlimit (0, lab::numGateRates - 1, p.gateRate);
            mixSm.setTargetValue (p.mix);
            outGain.setTargetValue (juce::Decibels::decibelsToGain (p.outDb, -100.0f));
            makeup.setTargetValue (juce::Decibels::decibelsToGain (makeupDb (m, p.drive, p.unhinged)));

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
                for (auto* sm : { &unhingedOS, &unhingedBase, &toneSm, &crushSm, &downSm, &aliasSm, &combSm, &combFreqSm,
                                  &widthSm, &gateSm, &mixSm })
                    sm->setCurrentAndTargetValue (sm->getTargetValue());
                for (auto& s : slots) s.reset();
                updateToneCoeffs (p.tone);
            }
        }

        void process (juce::AudioBuffer<float>& buffer, const dsp::Transport& t)
        {
            const int n = buffer.getNumSamples();
            dsp::Transport tc = t;
            for (int start = 0; start < n; start += maxBlock)
            {
                tc.ppq = t.ppq + (double) start * juce::jlimit (20.0, 400.0, t.bpm) / (60.0 * sampleRate);
                processChunk (buffer, start, juce::jmin (maxBlock, n - start), tc);
            }
        }

        void processBypassed (juce::AudioBuffer<float>& buffer)
        {
            const int chans = juce::jmin (numCh, buffer.getNumChannels());
            const int n = buffer.getNumSamples();
            int pos = delayPos;
            for (int i = 0; i < n; ++i)
            {
                for (int c = 0; c < chans; ++c)
                {
                    if (latency == 0) continue;
                    float* d = buffer.getWritePointer (c);
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
            float hpf = 0.0f, hpfA = 0.0f;
            float dcX = 0.0f, dcY = 0.0f;
            float holdPhase = 0.0f, held = 0.0f;   // DOWNSAMPLE
            float aPhase = 0.0f, aHeld = 0.0f;     // ALIAS decimator
            float shelf = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
            float interOS = 0.0f, interBase = 0.0f;
        };

        static float makeupDb (int m, float drive01, bool unhinged) noexcept
        {
            // Measured with "NajiTestHost calibrate" (guitar-level input, -20 dBFS RMS).
            static constexpr float table[2][numDriveTypes][11] = {
                { { 1.8f, -1.2f, -4.8f, -8.4f, -11.5f, -13.6f, -14.8f, -15.3f, -15.5f, -15.6f, -15.7f },
                  { 1.5f, -1.5f, -5.4f, -9.6f, -12.8f, -14.5f, -15.2f, -15.5f, -15.6f, -15.6f, -15.7f },
                  { -3.4f, -6.1f, -9.1f, -11.6f, -13.3f, -14.3f, -14.9f, -15.2f, -15.3f, -15.3f, -15.3f },
                  { 1.6f, -1.4f, -5.2f, -9.1f, -12.3f, -13.3f, -10.9f, -7.5f, -10.1f, -11.4f, -11.0f } },
                { { -10.7f, -14.5f, -15.4f, -15.6f, -15.6f, -15.7f, -15.7f, -15.7f, -15.7f, -15.7f, -15.8f },
                  { -11.7f, -14.8f, -15.5f, -15.6f, -15.6f, -15.7f, -15.7f, -15.7f, -15.8f, -15.8f, -15.8f },
                  { -13.9f, -15.1f, -15.4f, -15.4f, -15.5f, -15.5f, -15.5f, -15.5f, -15.6f, -15.6f, -15.6f },
                  { -11.4f, -12.4f, -9.6f, -8.0f, -4.9f, -4.8f, -6.3f, -7.8f, -8.3f, -8.4f, -8.5f } },
            };
            const float x = juce::jlimit (0.0f, 1.0f, drive01) * 10.0f;
            const int i = juce::jmin (9, (int) x);
            const float f = x - (float) i;
            const auto& row = table[unhinged ? 1 : 0][m];
            return row[i] + (row[i + 1] - row[i]) * f;
        }

        float onePoleG (double hz) const
        {
            const double g = std::tan (juce::MathConstants<double>::pi * hz / sampleRate);
            return (float) (g / (1.0 + g));
        }

        void updateToneCoeffs (float t)
        {
            const double lpHz = juce::jmin (600.0 * std::pow (30.0, juce::jmin (1.0, t / 0.75)), sampleRate * 0.45);
            const double g = std::tan (juce::MathConstants<double>::pi * lpHz / sampleRate);
            constexpr double k = 1.41421356;
            const double a1 = 1.0 / (1.0 + g * (g + k));
            svfA1 = (float) a1;
            svfA2 = (float) (g * a1);
            svfA3 = (float) (g * g * a1);
            const float shelfDb = t > 0.5f ? 7.0f * (t - 0.5f) / 0.5f : 0.0f;
            shelfAmt = juce::Decibels::decibelsToGain (shelfDb) - 1.0f;
        }

        void processChunk (juce::AudioBuffer<float>& buffer, int start, int n, const dsp::Transport& t)
        {
            const int chans = juce::jmin (numCh, buffer.getNumChannels());
            if (chans == 0 || n == 0) return;
            float* w[maxChannels] = { buffer.getWritePointer (0, start), chans > 1 ? buffer.getWritePointer (1, start) : nullptr };

            // 1) Latency-aligned dry copy + subsonic HPF on the wet path.
            {
                int pos = delayPos;
                for (int i = 0; i < n; ++i)
                {
                    for (int c = 0; c < chans; ++c)
                    {
                        auto& dl = dryDelay[(size_t) c];
                        const float x = w[c][i];
                        if (latency > 0) { dryBuffer.setSample (c, i, dl[(size_t) pos]); dl[(size_t) pos] = x; }
                        else dryBuffer.setSample (c, i, x);

                        auto& s = ch[(size_t) c];
                        const float v = (x - s.hpf) * hpfCoeff;
                        const float lp = v + s.hpf;
                        s.hpf = lp + v;
                        w[c][i] = x - lp;
                    }
                    if (latency > 0) pos = (pos + 1) % latency;
                }
            }

            // 2) SLOT A (before the drive)
            slots[0].process (w, chans, n, t);

            // Keep a latency-aligned copy of the slot A output for the ALIAS path.
            {
                int pos = delayPos;
                for (int i = 0; i < n; ++i)
                {
                    for (int c = 0; c < chans; ++c)
                    {
                        auto& dl = aliasDelay[(size_t) c];
                        const float x = w[c][i];
                        if (latency > 0) { aliasBuffer.setSample (c, i, dl[(size_t) pos]); dl[(size_t) pos] = x; }
                        else aliasBuffer.setSample (c, i, x);
                    }
                    if (latency > 0) pos = (pos + 1) % latency;
                }
                delayPos = pos;
            }

            // 3) Oversampled DRIVE
            juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) chans, (size_t) start, (size_t) n);
            auto up = oversampler->processSamplesUp (block);
            const int upN = (int) up.getNumSamples();
            float* upCh[maxChannels] = { up.getChannelPointer (0), chans > 1 ? up.getChannelPointer (1) : nullptr };
            for (int i = 0; i < upN; ++i)
            {
                const float g = driveGain.getNextValue();
                const float u = unhingedOS.getNextValue();
                if (modeFade < 1.0f) modeFade = juce::jmin (1.0f, modeFade + modeFadeStep);
                for (int c = 0; c < chans; ++c)
                {
                    const float x = upCh[c][i] * g;
                    float y = shapers::shape (curMode, x, ch[(size_t) c].interOS, u, interHpOS);
                    if (modeFade < 1.0f)
                    {
                        float tmp = ch[(size_t) c].interOS;
                        const float a = shapers::shape (prevMode, x, tmp, u, interHpOS);
                        y = a + (y - a) * modeFade;
                    }
                    upCh[c][i] = y;
                }
            }
            oversampler->processSamplesDown (block);

            // 4) ALIAS blend + DC blocker
            for (int i = 0; i < n; ++i)
            {
                const float a01 = aliasSm.getNextValue();
                const float gB = driveGainBase.getNextValue();
                const float uB = unhingedBase.getNextValue();
                const bool fadingBase = modeFadeBase < 1.0f;
                if (fadingBase) modeFadeBase = juce::jmin (1.0f, modeFadeBase + modeFadeStepBase);

                const bool aliasing = a01 > 1.0e-4f;
                const float aBlend = juce::jmin (1.0f, a01 * 2.5f);
                const float aHold  = 1.0f + 47.0f * a01 * a01 * 1.0137f;
                const float aFlip  = juce::jlimit (0.0f, 0.35f, (a01 - 0.45f) * 0.7f);
                const float flipGain = (parity & 1) ? 1.0f - 2.0f * aFlip : 1.0f;
                parity ^= 1;

                for (int c = 0; c < chans; ++c)
                {
                    auto& s = ch[(size_t) c];
                    float y = w[c][i];
                    if (aliasing)
                    {
                        const float in = aliasBuffer.getSample (c, i);
                        const float v = (in - s.hpfA) * hpfCoeff;
                        const float lp = v + s.hpfA;
                        s.hpfA = lp + v;
                        const float xa = (in - lp) * gB;
                        float ya = shapers::shape (curMode, xa, s.interBase, uB, interHpBase);
                        if (fadingBase)
                        {
                            float tmp = s.interBase;
                            const float yp = shapers::shape (prevMode, xa, tmp, uB, interHpBase);
                            ya = yp + (ya - yp) * modeFadeBase;
                        }
                        s.aPhase += 1.0f;
                        if (s.aPhase >= aHold) { s.aPhase -= aHold; s.aHeld = ya; }
                        y += (s.aHeld * flipGain - y) * aBlend;
                    }
                    else s.aPhase = 0.0f;

                    const float dc = y - s.dcX + dcR * s.dcY;
                    s.dcX = y;
                    s.dcY = dc;
                    w[c][i] = dc;
                }
            }

            // 5) SLOT B (after the drive)
            slots[1].process (w, chans, n, t);

            // 6) LAB section, auto gain, limiter, mix, output
            clock.sync (t);
            const double period = lab::gateBeats[gateRate];
            for (int i = 0; i < n; ++i)
            {
                const float tn = toneSm.getNextValue();
                if (toneSm.isSmoothing() || (i & 15) == 0)
                    if (std::abs (tn - lastTone) > 1.0e-6f) { updateToneCoeffs (tn); lastTone = tn; }

                const float c01 = crushSm.getNextValue();
                const float d01 = downSm.getNextValue();
                const float cb = combSm.getNextValue();
                const float cf = lab::combHz (combFreqSm.getNextValue());
                const float wd = widthSm.getNextValue();
                const float gt = gateSm.getNextValue();
                const float mk = makeup.getNextValue();
                const float mix = mixSm.getNextValue();
                const float outG = outGain.getNextValue();

                const bool crushing = c01 > 1.0e-4f;
                const float steps = crushing ? std::exp2 (16.0f - 14.0f * std::pow (c01, 0.6f) - 1.0f) : 0.0f;
                const float crushWet = juce::jmin (1.0f, c01 * 12.0f);
                const bool down = d01 > 1.0e-4f;
                const float holdLen = 1.0f + 30.73f * d01 * d01;
                const float downWet = juce::jmin (1.0f, d01 * 12.0f);
                const float combFb = 0.35f + 0.6f * cb;
                const float combDelay = (float) sampleRate / cf;

                // Tempo-synced gate (50% duty, 1.5 ms edges)
                const double beat = clock.step (t, sampleRate);
                const double ph = beat / period - std::floor (beat / period);
                gateGain += ((ph < 0.5 ? 1.0f : 1.0f - gt) - gateGain) * gateSmoothA;

                float ys[maxChannels] {};
                float inP = 0.0f;
                for (int c = 0; c < chans; ++c)
                {
                    auto& s = ch[(size_t) c];
                    float y = w[c][i];
                    const float dry = dryBuffer.getSample (c, i);
                    inP += dry * dry;

                    if (crushing) y += (std::round (y * steps) / steps - y) * crushWet;
                    if (down)
                    {
                        s.holdPhase += 1.0f;
                        if (s.holdPhase >= holdLen) { s.holdPhase -= holdLen; s.held = y; }
                        y += (s.held - y) * downWet;
                    }
                    else { s.holdPhase = 0.0f; s.held = y; }

                    if (cb > 1.0e-4f)
                    {
                        const float delayed = combDl[(size_t) c].read (juce::jmax (2.0f, combDelay));
                        const float cIn = y + combFb * combDamp[(size_t) c].lp (delayed);
                        combDl[(size_t) c].push (dsp::fastTanh (cIn * 0.5f) * 2.0f);
                        y += (cIn * (1.0f - combFb * 0.6f) - y) * juce::jmin (1.0f, cb * 1.5f);
                    }
                    else combDl[(size_t) c].push (0.0f);

                    {
                        const float v = (y - s.shelf) * shelfCoeff;
                        const float lp = v + s.shelf;
                        s.shelf = lp + v;
                        y += shelfAmt * (y - lp);
                    }
                    {
                        const float v3 = y - s.ic2;
                        const float v1 = svfA1 * s.ic1 + svfA2 * v3;
                        const float v2 = s.ic2 + svfA2 * s.ic1 + svfA3 * v3;
                        s.ic1 = 2.0f * v1 - s.ic1;
                        s.ic2 = 2.0f * v2 - s.ic2;
                        y = v2;
                    }
                    ys[c] = y * gateGain * mk;
                }

                // Width: mid/side, with a decorrelated mid added above 100%.
                if (chans == 2)
                {
                    const float m = 0.5f * (ys[0] + ys[1]);
                    float sd = 0.5f * (ys[0] - ys[1]) * juce::jmin (1.0f, wd);
                    if (wd > 1.0f)
                    {
                        float a = m, b = m;
                        const float ca[3] = { 0.62f, -0.31f, 0.77f }, cbk[3] = { -0.55f, 0.44f, -0.68f };
                        for (int k = 0; k < 3; ++k) { a = widthAp[0][(size_t) k].process (a, ca[k]); b = widthAp[1][(size_t) k].process (b, cbk[k]); }
                        sd += (a - b) * 0.5f * (wd - 1.0f) + 0.5f * (ys[0] - ys[1]) * (wd - 1.0f) * 0.5f;
                    }
                    ys[0] = m + sd;
                    ys[1] = m - sd;
                }

                const float ag = juce::Decibels::decibelsToGain (adaptDb);
                float peak = 0.0f;
                for (int c = 0; c < chans; ++c) peak = juce::jmax (peak, std::abs (ys[c] * ag));
                limEnv = juce::jmax (peak, limEnv * limRelease);
                const float lg = limEnv > limCeiling ? limCeiling / limEnv : 1.0f;

                float wetP = 0.0f;
                for (int c = 0; c < chans; ++c) { const float v = ys[c] * ag * lg; wetP += v * v; }
                // Loudness detectors (50 ms), same for input and output.
                inPow  += (inP  - inPow)  * detFast;
                wetPow += (wetP - wetPow) * detFast;
                // Only adapt while the input is present (within 30 dB of its recent level), so tails,
                // freezes and fade-outs are not chased.
                inPeakRef = juce::jmax (inPow, inPeakRef * refDecay);
                if (inPow > 1.0e-8f * (float) chans && inPow > inPeakRef * 1.0e-3f)
                {
                    // Like a compressor: turns down quickly (120 ms) when the output gets louder than the
                    // input, comes back up slowly (600 ms). Clamped so a moment of silence (e.g. an FFT
                    // effect starting up) cannot wind the gain up.
                    const float err = juce::jlimit (-24.0f, 12.0f, 10.0f * std::log10 ((inPow + 1.0e-12f) / (wetPow + 1.0e-12f)));
                    // Turning down only happens while the input is near its recent level, so decays,
                    // fade-outs and frozen tails are not chased down.
                    if (err >= 0.0f)                       adaptDb += err * glideCoeff;
                    else if (inPow > inPeakRef * 0.1f)     adaptDb += err * attackCoeff;
                    adaptDb = juce::jlimit (-36.0f, 18.0f, adaptDb);
                }

                for (int c = 0; c < chans; ++c)
                {
                    const float wet = ys[c] * ag * lg;
                    const float dry = dryBuffer.getSample (c, i);
                    w[c][i] = outG * (dry + (wet - dry) * mix);
                }
            }
        }

        //======================================================================
        double sampleRate = 44100.0;
        int numCh = 2, maxBlock = 512, latency = 0, osStages = 3;

        std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
        std::array<std::vector<float>, maxChannels> dryDelay, aliasDelay;
        int delayPos = 0;
        juce::AudioBuffer<float> dryBuffer, aliasBuffer;

        std::array<ChannelState, maxChannels> ch {};
        std::array<EffectSlot, 2> slots;
        std::array<dsp::DelayLine, maxChannels> combDl;
        std::array<dsp::OnePole, maxChannels> combDamp;
        std::array<std::array<dsp::Allpass1, 3>, 2> widthAp;
        dsp::BeatClock clock;

        using Mult = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;
        Mult driveGain { 1.0f }, driveGainBase { 1.0f }, outGain { 1.0f }, makeup { 1.0f };
        juce::SmoothedValue<float> unhingedOS { 0.0f }, unhingedBase { 0.0f }, toneSm { 0.5f }, crushSm { 0.0f },
                                   downSm { 0.0f }, aliasSm { 0.0f }, combSm { 0.0f }, combFreqSm { 0.4f },
                                   widthSm { 1.0f }, gateSm { 0.0f }, mixSm { 1.0f };
        int gateRate = 3;
        float gateGain = 1.0f, gateSmoothA = 0.01f;

        int curMode = 0, prevMode = 0;
        float modeFade = 1.0f, modeFadeStep = 0.001f, modeFadeBase = 1.0f, modeFadeStepBase = 0.01f;
        float interHpOS = 0.0001f, interHpBase = 0.001f;

        float hpfCoeff = 0.0f, shelfCoeff = 0.0f, dcR = 0.999f;
        float svfA1 = 0.0f, svfA2 = 0.0f, svfA3 = 0.0f, shelfAmt = 0.0f, lastTone = -1.0f;

        float inPeakRef = 0.0f, refDecay = 0.9999f;
        float detCoeff = 0.0f, glideCoeff = 0.0f, detFast = 0.0f, attackCoeff = 0.0f, inPow = 0.0f, wetPow = 0.0f, adaptDb = 0.0f;
        float limEnv = 0.0f, limRelease = 0.999f;
        static constexpr float limCeiling = 0.891f;
        int parity = 0;
    };
}
