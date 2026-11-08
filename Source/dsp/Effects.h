#pragma once

#include "DspCommon.h"
#include "../EffectDefs.h"
#include <memory>

/*
    The 14 effect engines. Each is a separate DSP design, not a different
    curve on one waveshaper:

      GLITCH     tempo-synced buffer repeat / reverse / tape-stop / dropout events
      MELT       modulated delay (wow + random drift + envelope sag) into a
                 modulated allpass smear chain
      OCTAVE     two delay-line pitch shifters (0.5x and 2x), analog flip-flop
                 sub octave, rectified octave harmonics
      FORMANT    4-band vowel resonator bank (A-E-I-O-U morph), shift, Q,
                 LFO + envelope movement
      DESTROY    4x oversampled gain smash -> wavefold -> rectify -> bit
                 crush -> sputter gate
      WARP       pitch shifter driven by a shaped LFO (bend, sweep, random
                 steps) plus signal-driven delay FM
      ROBOT      STFT robotisation (phases zeroed, frames re-emitted at the
                 robot pitch), tuned feedback comb, 30 Hz "Dalek" ring
      VINYL      wow + flutter delay, band-limiting, crackle/pops, hiss, hum
      RING       ring modulator with an FM-modulated carrier, envelope
                 tracking, shape and stereo spread
      FUZZ       4x oversampled biased transistor stages, stackable, with
                 gating and a mid-scoop tone stack
      GRANULAR   up to 48 windowed grains from a 5 s buffer: size, density,
                 position, spray, pitch, reverse grains
      SPECTRAL   phase vocoder: freeze, bin shift, stretch, blur, thin
      FREQ SHIFT Hilbert transformer single-sideband shifter with feedback
      REVERSE    double-buffered reverse slices with chance, feedback, speed

    Every engine processes in place, takes per-sample smoothed parameters,
    uses no locks or allocation in process(), and is block-size independent.
*/
namespace naji::dsp
{
    // Per-sample smoothed parameter values (0..1), one pointer per parameter.
    struct ParamBlock
    {
        const float* v[fx::numParams] {};
        float at (int p, int i) const noexcept { return v[p][i]; }
    };

    class Effect
    {
    public:
        virtual ~Effect() = default;
        virtual void prepare (double sampleRate, int maxBlock) = 0;
        virtual void reset() = 0;
        virtual void process (float* const* ch, int numCh, int n, const ParamBlock& p, const Transport& t) = 0;
    };

    // Shared tempo clock: beat position, re-synced to the host while playing.
    struct BeatClock
    {
        double beat = 0.0;
        void sync (const Transport& t) { if (t.playing) beat = t.ppq; }
        double step (const Transport& t, double sr) { const double b = beat; beat += juce::jlimit (20.0, 400.0, t.bpm) / (60.0 * sr); return b; }
        static double samplesPerBeat (const Transport& t, double sr) { return sr * 60.0 / juce::jlimit (20.0, 400.0, t.bpm); }
    };

    //==========================================================================
    class GlitchFx final : public Effect
    {
    public:
        void prepare (double s, int) override
        {
            sr = s;
            for (auto& b : buf) b.prepare ((int) (sr * 5.0));
            chopSmooth.setTime (0.0015f, sr);
            reset();
        }
        void reset() override
        {
            for (auto& b : buf) b.reset();
            written = 0; lastSlice = -1; type = 0; t = 0.0; clock.beat = 0.0; rng = Rng (0xC0FFEEu);
            chopSmooth.z = 1.0f; tapePos = 0.0;
        }

        void process (float* const* ch, int numCh, int n, const ParamBlock& p, const Transport& tr) override
        {
            clock.sync (tr);
            const double spb = BeatClock::samplesPerBeat (tr, sr);
            for (int i = 0; i < n; ++i)
            {
                const int sliceIdx = fx::map::glitchSlice (p.at (0, i));
                const double sliceBeats = fx::map::glitchBeats[sliceIdx];
                const double beat = clock.step (tr, sr);
                const auto slice = (int64_t) std::floor (beat / sliceBeats);
                sliceLen = juce::jlimit (64.0, sr * 2.0, sliceBeats * spb);

                for (int c = 0; c < numCh; ++c) buf[(size_t) c].push (ch[c][i]);
                ++written;

                if (slice != lastSlice)
                {
                    lastSlice = slice;
                    startEvent (p.at (1, i), fx::map::glitchRepeats (p.at (2, i)), p.at (3, i));
                }

                const float fade = (float) juce::jmin (sliceLen * 0.25, sr * 0.002);
                const float sliceEnv = juce::jmin (1.0f, (float) t / fade, (float) (sliceLen - t) / fade);
                const float chopAmt = p.at (4, i);
                const bool chopOn = std::fmod (t, sliceLen * 0.5) < sliceLen * 0.25;
                const float chopG = chopSmooth.lp (chopOn ? 1.0f : 1.0f - chopAmt);
                const float mix = p.at (5, i);

                for (int c = 0; c < numCh; ++c)
                {
                    const float x = ch[c][i];
                    float ev = x, inner = 1.0f;
                    const auto& b = buf[(size_t) c];
                    const double srcStart = (double) (sliceStartAbs - (int64_t) sliceLen);

                    switch (type)
                    {
                        case 1: case 5:  // stutter (loop the start of the previous slice)
                        {
                            const double local = std::fmod (t * rate, loopLen);
                            ev = b.readAbs (srcStart + local);
                            const float f2 = (float) juce::jmin (loopLen * 0.25, sr * 0.0015);
                            inner = juce::jmin (1.0f, (float) local / f2, (float) (loopLen - local) / f2);
                            break;
                        }
                        case 2:  // reverse the previous slice
                        {
                            const double local = std::fmod (t * rate, sliceLen);
                            ev = b.readAbs (srcStart + sliceLen - 1.0 - local);
                            break;
                        }
                        case 3:  // tape stop
                            ev = b.readAbs (srcStart + tapePos);
                            break;
                        case 4:  // digital dropout: half silent, half crushed
                        {
                            if (t < sliceLen * 0.5) ev = 0.0f;
                            else { const float q = 6.0f; ev = std::round (b.readAbs (srcStart + std::floor (t / 12.0) * 12.0) * q) / q; }
                            break;
                        }
                        default: break;
                    }

                    const float wet = x + (ev * inner - x) * (type == 0 ? 0.0f : sliceEnv);
                    ch[c][i] = x + (wet * chopG - x) * mix;
                }

                if (type == 3) tapePos += std::pow (juce::jmax (0.0, 1.0 - t / sliceLen), 1.6);
                t += 1.0;
            }
        }

    private:
        void startEvent (float chaos, int repeats, float pitch)
        {
            sliceStartAbs = written - 1;
            t = 0.0;
            tapePos = 0.0;
            type = 0;
            rate = 1.0;
            loopLen = juce::jmax (32.0, sliceLen / repeats);
            if (rng.next01() >= chaos) return;

            const float r = rng.next01();
            if      (r < 0.32f) type = 1;   // stutter
            else if (r < 0.52f) type = 2;   // reverse
            else if (r < 0.66f) type = 5;   // pitched stutter
            else if (r < 0.80f) type = 3;   // tape stop
            else                type = 4;   // dropout

            if (type == 1) rate = std::pow (2.0, (double) (rng.nextBi() * pitch * 0.5f));
            if (type == 2) rate = std::pow (2.0, (double) (rng.nextBi() * pitch * 0.5f));
            if (type == 5) rate = rng.next01() < 0.5f ? 2.0 : 0.5;
            if (type == 5 && pitch < 0.05f) type = 1;
        }

        double sr = 48000.0;
        std::array<DelayLine, 2> buf;
        int64_t written = 0, lastSlice = -1, sliceStartAbs = 0;
        int type = 0;
        double t = 0.0, sliceLen = 4800.0, loopLen = 1200.0, rate = 1.0, tapePos = 0.0;
        BeatClock clock;
        Rng rng { 0xC0FFEEu };
        OnePole chopSmooth;
    };

    //==========================================================================
    class MeltFx final : public Effect
    {
    public:
        void prepare (double s, int) override
        {
            sr = s;
            for (auto& d : dl) d.prepare ((int) (sr * 0.2));
            for (auto& e : env) e.set (0.004f, 0.35f, sr);
            reset();
        }
        void reset() override
        {
            for (auto& d : dl) d.reset();
            for (auto& c : ap) for (auto& a : c) a.reset();
            for (auto& e : env) e.reset();
            drift[0].reset(); drift[1].reset();
            drift[1].rng = Rng (777u);
            lfo = 0.0; apPhase = 0.0f; counter = 0;
            std::fill (std::begin (apCoef), std::end (apCoef), 0.0f);
        }

        void process (float* const* ch, int numCh, int n, const ParamBlock& p, const Transport&) override
        {
            for (int i = 0; i < n; ++i)
            {
                const float speed = fx::map::meltSpeedHz (p.at (1, i));
                const float cents = fx::map::meltCents (p.at (0, i));
                const float driftAmt = p.at (2, i), smear = p.at (3, i), sag = p.at (4, i), mix = p.at (5, i);

                lfo += speed / sr;
                lfo -= std::floor (lfo);
                const float delta = std::pow (2.0f, cents / 1200.0f) - 1.0f;
                const float depth = juce::jmin ((float) sr * 0.03f, delta * (float) sr / (twoPi * speed));

                if ((counter++ & 15) == 0)
                {
                    apPhase += 16.0f * twoPi * 0.23f / (float) sr;
                    for (int k = 0; k < 8; ++k)
                        apCoef[k] = juce::jlimit (-0.92f, 0.92f, smear * (0.55f + 0.35f * std::sin (apPhase + (float) k * 0.9f)));
                }

                for (int c = 0; c < numCh; ++c)
                {
                    const float x = ch[c][i];
                    const float l = std::sin (twoPi * (float) lfo + (float) c * 1.3f);
                    const float dr = drift[(size_t) c].process (1.1f, sr) * driftAmt * 0.012f * (float) sr;
                    const float e = juce::jmin (1.0f, env[(size_t) c].process (x) * 3.0f);
                    const float d = 0.045f * (float) sr + depth * l + dr + sag * e * 0.02f * (float) sr;

                    dl[(size_t) c].push (x);
                    float y = dl[(size_t) c].read (juce::jmax (2.0f, d));

                    float z = y;
                    for (int k = 0; k < 8; ++k)
                        z = ap[(size_t) c][(size_t) k].process (z, apCoef[k]);
                    y += (z - y) * smear;

                    ch[c][i] = x + (y - x) * mix;
                }
            }
        }

    private:
        double sr = 48000.0, lfo = 0.0;
        std::array<DelayLine, 2> dl;
        std::array<std::array<Allpass1, 8>, 2> ap;
        std::array<Envelope, 2> env;
        std::array<SmoothRandom, 2> drift;
        float apPhase = 0.0f, apCoef[8] {};
        uint32_t counter = 0;
    };

    //==========================================================================
    class OctaveFx final : public Effect
    {
    public:
        void prepare (double s, int) override
        {
            sr = s;
            for (auto& d : dl) d.prepare ((int) (sr * 0.4));
            for (auto& f : subLp) f.set (180.0f, 0.7f, sr);
            for (auto& e : env) e.set (0.003f, 0.12f, sr);
            for (auto& o : subSmooth) o.setHz (900.0f, sr);
            for (auto& o : harmHp) o.setHz (60.0f, sr);
            reset();
        }
        void reset() override
        {
            for (auto& d : dl) d.reset();
            for (auto& s : down) s.reset();
            for (auto& h : hil) h.reset();
            for (auto& f : subLp) f.reset();
            for (auto& e : env) e.reset();
            for (auto& o : subSmooth) o.reset();
            for (auto& o : harmHp) o.reset();
            ff = { false, false }; armed = { false, false };
        }

        void process (float* const* ch, int numCh, int n, const ParamBlock& p, const Transport&) override
        {
            for (int i = 0; i < n; ++i)
            {
                const float window = fx::map::octaveGrainMs (p.at (4, i)) * 0.001f * (float) sr;
                for (int c = 0; c < numCh; ++c)
                {
                    const auto cs = (size_t) c;
                    const float x = ch[c][i];
                    dl[cs].push (x);
                    const float e = env[cs].process (x);

                    const float dn = down[cs].process (dl[cs], 0.5f, window);
                    // Octave up: square the analytic signal (doubles the instantaneous frequency).
                    float re = 0.0f, im = 0.0f;
                    hil[cs].process (x, re, im);
                    const float upv = (re * re - im * im) / (std::sqrt (re * re + im * im) + 1.0e-9f);

                    // Analog-style flip-flop sub: toggles on every other zero crossing.
                    const float lp = subLp[cs].process (x).lp;
                    const float thr = juce::jmax (1.0e-4f, e * 0.08f);
                    if (! armed[cs] && lp < -thr) armed[cs] = true;
                    if (armed[cs] && lp > thr) { armed[cs] = false; ff[cs] = ! ff[cs]; }
                    const float sub = subSmooth[cs].lp (ff[cs] ? 1.0f : -1.0f) * juce::jmin (1.0f, e * 1.6f);

                    const float harm = fastTanh (2.5f * harmHp[cs].hp (std::abs (x)));

                    const float gen = dn * 1.2f * p.at (0, i) + upv * 1.2f * p.at (1, i)
                                    + sub * 0.8f * p.at (2, i) + harm * p.at (3, i);
                    const float blend = p.at (5, i);
                    ch[c][i] = x * (1.0f - blend) + gen * blend;
                }
            }
        }

    private:
        double sr = 48000.0;
        std::array<DelayLine, 2> dl;
        std::array<PitchShifter, 2> down;
        std::array<Hilbert, 2> hil;
        std::array<Svf, 2> subLp;
        std::array<Envelope, 2> env;
        std::array<OnePole, 2> subSmooth, harmHp;
        std::array<bool, 2> ff { false, false }, armed { false, false };
    };

    //==========================================================================
    class FormantFx final : public Effect
    {
    public:
        void prepare (double s, int) override
        {
            sr = s;
            env.set (0.01f, 0.25f, sr);
            for (auto& o : emph) o.setHz (300.0f, sr);
            posCoeff = 1.0f - std::exp (-1.0f / (0.06f * (float) sr));
            reset();
        }
        void reset() override
        {
            for (auto& c : bands) for (auto& b : c) b.reset();
            env.reset();
            for (auto& o : emph) o.reset();
            lfo = 0.0; counter = 0; posSmooth = -1.0f;
        }

        void process (float* const* ch, int numCh, int n, const ParamBlock& p, const Transport&) override
        {
            if (posSmooth < 0.0f) posSmooth = fx::map::formantVowel (p.at (0, 0));
            // Formants (Hz) and levels (dB) for A E I O U
            static constexpr float F[5][4] = { { 800, 1150, 2900, 3900 }, { 400, 1700, 2600, 3300 }, { 270, 2140, 2950, 3900 },
                                               { 450, 800, 2830, 3800 },  { 325, 700, 2530, 3500 } };
            static constexpr float G[5][4] = { { 0, -4, -16, -22 }, { 0, -6, -12, -20 }, { 0, -8, -14, -20 },
                                               { 0, -4, -18, -24 }, { 0, -6, -20, -26 } };
            for (int i = 0; i < n; ++i)
            {
                float mono = 0.0f;
                for (int c = 0; c < numCh; ++c) mono += ch[c][i];
                const float e = juce::jmin (1.0f, env.process (mono / (float) numCh) * 4.0f);

                const float mod = p.at (3, i);
                lfo += fx::map::formantModRate (mod) / sr;
                lfo -= std::floor (lfo);

                const float posTarget = fx::map::formantVowel (p.at (0, i)) + mod * 1.4f * std::sin (twoPi * (float) lfo)
                                      + p.at (4, i) * e * 2.6f;
                posSmooth += (posTarget - posSmooth) * posCoeff;
                if ((counter++ & 3) == 0)
                {
                    float pos = posSmooth;
                    pos = std::fmod (std::abs (pos), 8.0f);
                    if (pos > 4.0f) pos = 8.0f - pos;     // reflect at the ends
                    const int a = juce::jmin (3, (int) pos);
                    const float f = pos - (float) a;
                    const float shift = std::pow (2.0f, fx::map::formantShiftSt (p.at (1, i)) / 12.0f);
                    const float q = fx::map::formantQ (p.at (2, i));
                    for (int k = 0; k < 4; ++k)
                    {
                        const float hz = std::exp (std::log (F[a][k]) * (1.0f - f) + std::log (F[a + 1][k]) * f) * shift;
                        gains[k] = juce::Decibels::decibelsToGain (G[a][k] * (1.0f - f) + G[a + 1][k] * f);
                        for (int c = 0; c < numCh; ++c)
                            bands[(size_t) c][(size_t) k].set (hz, q * (k == 0 ? 0.7f : 1.0f), sr);
                    }
                    makeup = 0.9f + 0.22f * std::sqrt (q);
                }

                const float amount = p.at (5, i);
                for (int c = 0; c < numCh; ++c)
                {
                    const auto cs = (size_t) c;
                    const float x = ch[c][i];
                    // Excitation: a little saturation plus pre-emphasis so the upper formants have energy.
                    const float sat = fastTanh (2.2f * x);
                    const float ex = sat + 1.4f * emph[cs].hp (sat);
                    float y = 0.0f;
                    for (int k = 0; k < 4; ++k)
                        y += gains[k] * bands[cs][(size_t) k].bpNorm (ex);
                    ch[c][i] = x * (1.0f - amount) + y * makeup * amount;
                }
            }
        }

    private:
        double sr = 48000.0, lfo = 0.0;
        std::array<std::array<Svf, 4>, 2> bands;
        std::array<OnePole, 2> emph;
        Envelope env;
        float gains[4] { 1, 0.5f, 0.2f, 0.1f }, makeup = 1.0f, posSmooth = -1.0f, posCoeff = 0.001f;
        uint32_t counter = 0;
    };

    //==========================================================================
    // Base for the two engines that run their nonlinear core 4x oversampled.
    class OversampledFx : public Effect
    {
    public:
        void prepare (double s, int maxBlock) override
        {
            sr = s;
            os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false);
            os->initProcessing ((size_t) maxBlock);
            dry.setSize (2, maxBlock);
            osRate = sr * 4.0;
            prepareCore();
            reset();
        }
        void reset() override { if (os) os->reset(); resetCore(); }

        void process (float* const* ch, int numCh, int n, const ParamBlock& p, const Transport&) override
        {
            for (int c = 0; c < numCh; ++c) dry.copyFrom (c, 0, ch[c], n);
            juce::dsp::AudioBlock<float> block (const_cast<float* const*> (ch), (size_t) numCh, (size_t) n);
            auto up = os->processSamplesUp (block);
            for (int j = 0; j < (int) up.getNumSamples(); ++j)
                for (int c = 0; c < numCh; ++c)
                {
                    float* d = up.getChannelPointer ((size_t) c);
                    d[j] = core (d[j], c, p, j >> 2);
                }
            os->processSamplesDown (block);
            post (ch, numCh, n, p);
        }

    protected:
        virtual void prepareCore() {}
        virtual void resetCore() {}
        virtual float core (float x, int c, const ParamBlock& p, int baseIndex) = 0;
        virtual void post (float* const* ch, int numCh, int n, const ParamBlock& p)
        {
            for (int c = 0; c < numCh; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float d = dry.getSample (c, i);
                    ch[c][i] = d + (ch[c][i] - d) * p.at (5, i);
                }
        }

        double sr = 48000.0, osRate = 192000.0;
        std::unique_ptr<juce::dsp::Oversampling<float>> os;
        juce::AudioBuffer<float> dry;
    };

    class DestroyFx final : public OversampledFx
    {
        void prepareCore() override
        {
            for (auto& e : env) e.set (0.0005f, 0.03f, osRate);
            gateA = 1.0f - std::exp (-1.0f / (0.0003f * (float) osRate));
        }
        void resetCore() override { for (auto& e : env) e.reset(); gate = { 1.0f, 1.0f }; rng = Rng (99u); }

        float core (float x, int c, const ParamBlock& p, int i) override
        {
            const float crunch = p.at (0, i), fold = p.at (1, i), rect = p.at (3, i), sput = p.at (4, i);
            const float e = env[(size_t) c].process (x);
            float v = x * (1.0f + crunch * crunch * 90.0f);
            const float hard = juce::jlimit (-1.0f, 1.0f, v);
            float t = 0.25f * (v * (1.0f + fold * 3.0f) + 1.0f);
            t -= std::floor (t);
            const float tri = 4.0f * std::abs (t - 0.5f) - 1.0f;
            float y = hard + (tri - hard) * fold;
            y += (std::abs (y) * 2.0f - 1.0f - y) * rect * (std::abs (x) > 1.0e-5f ? 1.0f : 0.0f);
            const float q = std::exp2 (fx::map::destroyBits (p.at (2, i)) - 1.0f);
            y = std::round (y * q) / q;
            // Sputter: a chattering gate that bites as notes decay.
            const float thr = sput * 0.12f;
            const float target = (sput > 0.0f && e < thr * (0.5f + rng.next01())) ? 0.0f : 1.0f;
            gate[(size_t) c] += (target - gate[(size_t) c]) * gateA;
            return juce::jlimit (-1.0f, 1.0f, y) * gate[(size_t) c] * 0.5f;
        }

        std::array<Envelope, 2> env;
        std::array<float, 2> gate { 1.0f, 1.0f };
        float gateA = 0.01f;
        Rng rng { 99u };
    };

    class FuzzFx final : public OversampledFx
    {
        static float stage (float u) noexcept
        {
            return u >= 0.0f ? 1.0f - std::exp (-u) : -0.55f * (1.0f - std::exp (u / 0.55f));
        }
        void prepareCore() override
        {
            for (auto& e : env) e.set (0.002f, 0.08f, osRate);
            for (auto& h : hp1) h.setHz (120.0f, osRate);
            for (auto& h : hp2) h.setHz (90.0f, osRate);
            for (auto& o : lp) o.setHz (650.0f, sr);
            for (auto& o : hp) o.setHz (1600.0f, sr);
            for (auto& o : air) o.setHz (5000.0f, sr);
            gateA = 1.0f - std::exp (-1.0f / (0.004f * (float) osRate));
        }
        void resetCore() override
        {
            for (auto& e : env) e.reset();
            for (auto& h : hp1) h.reset();
            for (auto& h : hp2) h.reset();
            for (auto& o : lp) o.reset();
            for (auto& o : hp) o.reset();
            for (auto& o : air) o.reset();
            gate = { 1.0f, 1.0f };
        }

        float core (float x, int c, const ParamBlock& p, int i) override
        {
            const auto cs = (size_t) c;
            const float fz = p.at (0, i), bias = p.at (1, i), gateAmt = p.at (2, i), stack = p.at (3, i);
            const float gain = 1.0f + fz * fz * 150.0f;
            const float e = env[cs].process (x);
            // Dynamic bias: louder signal pushes the transistor further off centre.
            const float b = bias * (0.5f + 0.9f * juce::jmin (1.0f, e * gain * 0.25f));
            const float s1 = stage (x * gain + b) - stage (b);
            const float s2 = std::tanh (7.0f * hp1[cs].hp (s1));
            const float s3 = std::tanh (6.0f * hp2[cs].hp (s2));
            const float st = stack * 2.0f;
            float y = st < 1.0f ? s1 + (s2 - s1) * st : s2 + (s3 - s2) * (st - 1.0f);
            const float thr = gateAmt * 0.04f;
            const float target = thr > 0.0f ? juce::jmin (1.0f, (e / thr) * (e / thr)) : 1.0f;
            gate[cs] += (target - gate[cs]) * gateA;
            return y * gate[cs];
        }

        void post (float* const* ch, int numCh, int n, const ParamBlock& p) override
        {
            for (int c = 0; c < numCh; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float y = ch[c][i];
                    const float sc = p.at (4, i);
                    const float l = lp[(size_t) c].lp (y), h = hp[(size_t) c].hp (y);
                    // Big-muff style tone stack, then an analog-style roll-off above ~5 kHz
                    const float toned = air[(size_t) c].lp (1.5f * (l * (1.0f - sc) + h * sc) + y * (1.0f - std::abs (sc * 2.0f - 1.0f)) * 0.2f);
                    const float d = dry.getSample (c, i);
                    ch[c][i] = d + (toned * 0.6f - d) * p.at (5, i);
                }
        }

        std::array<Envelope, 2> env;
        std::array<OnePole, 2> hp1, hp2, lp, hp, air;
        std::array<float, 2> gate { 1.0f, 1.0f };
        float gateA = 0.01f;
    };

    //==========================================================================
    class WarpFx final : public Effect
    {
    public:
        void prepare (double s, int) override
        {
            sr = s;
            for (auto& d : dl) d.prepare ((int) (sr * 0.3));
            for (auto& d : fmDl) d.prepare ((int) (sr * 0.05));
            reset();
        }
        void reset() override
        {
            for (auto& d : dl) d.reset();
            for (auto& d : fmDl) d.reset();
            for (auto& s : ps) s.reset();
            phase = 0.0; started = false; held = { 0.0f, 0.0f }; rng = Rng (4242u);
        }

        void process (float* const* ch, int numCh, int n, const ParamBlock& p, const Transport&) override
        {
            for (int i = 0; i < n; ++i)
            {
                    phase += fx::map::warpRateHz (p.at (2, i)) / sr;
                const bool wrapped = phase >= 1.0;
                phase -= std::floor (phase);
                if (wrapped || ! started) { started = true; held[0] = rng.nextBi(); held[1] = juce::jlimit (-1.0f, 1.0f, held[0] + rng.nextBi() * 0.3f); }

                const int shape = fx::map::warpShape (p.at (3, i));
                const float bend = fx::map::warpBendSt (p.at (0, i));
                const float sweep = fx::map::warpSweepSt (p.at (1, i));
                const float fm = p.at (4, i), mix = p.at (5, i);

                for (int c = 0; c < numCh; ++c)
                {
                    float ph = (float) phase + (float) c * 0.08f;
                    ph -= std::floor (ph);
                    float l = 0.0f;
                    switch (shape)
                    {
                        case 0: l = std::sin (twoPi * ph); break;
                        case 1: l = 1.0f - 4.0f * std::abs (ph - 0.5f); break;
                        case 2: l = 2.0f * ph - 1.0f; break;
                        case 3: l = ph < 0.5f ? 1.0f : -1.0f; break;
                        default: l = held[(size_t) c]; break;
                    }
                    const float ratio = juce::jlimit (0.25f, 4.0f, std::pow (2.0f, (bend + sweep * l) / 12.0f));

                    const float x = ch[c][i];
                    fmDl[(size_t) c].push (x);
                    const float fmd = 2.0f + fm * 0.004f * (float) sr * (1.0f + fastTanh (3.0f * x));
                    const float xf = fm > 0.001f ? fmDl[(size_t) c].read (fmd) : x;

                    dl[(size_t) c].push (xf);
                    const float y = ps[(size_t) c].process (dl[(size_t) c], ratio, 0.05f * (float) sr);
                    ch[c][i] = x + (y - x) * mix;
                }
            }
        }

    private:
        double sr = 48000.0, phase = 0.0;
        bool started = false;
        std::array<DelayLine, 2> dl, fmDl;
        std::array<PitchShifter, 2> ps;
        std::array<float, 2> held { 0.0f, 0.0f };
        Rng rng { 4242u };
    };

    //==========================================================================
    // Overlap-add STFT helper shared by ROBOT and SPECTRAL.
    struct StftChannel
    {
        DelayLine in;
        std::vector<float> out;      // accumulator ring
        int outMask = 0, outPos = 0;

        void prepare (int fftSize)
        {
            in.prepare (fftSize * 2);
            int size = 1;
            while (size < fftSize * 4) size <<= 1;
            out.assign ((size_t) size, 0.0f);
            outMask = size - 1;
            outPos = 0;
        }
        void reset() { in.reset(); std::fill (out.begin(), out.end(), 0.0f); outPos = 0; }

        float pop() noexcept
        {
            const float y = out[(size_t) outPos];
            out[(size_t) outPos] = 0.0f;
            outPos = (outPos + 1) & outMask;
            return y;
        }
        void addFrame (const float* frame, int size) noexcept
        {
            for (int k = 0; k < size; ++k)
                out[(size_t) ((outPos + k) & outMask)] += frame[k];
        }
    };

    class RobotFx final : public Effect
    {
    public:
        static constexpr int order = 10, N = 1 << order;

        void prepare (double s, int) override
        {
            sr = s;
            fft = std::make_unique<juce::dsp::FFT> (order);
            work.assign ((size_t) N * 2, 0.0f);
            frame.assign ((size_t) N, 0.0f);
            win.resize ((size_t) N);
            for (int k = 0; k < N; ++k) win[(size_t) k] = hann01 ((float) k / (float) N);
            for (auto& c : st) c.prepare (N);
            for (auto& d : comb) d.prepare ((int) (sr / 30.0) + 8);
            for (auto& o : combDamp) o.setHz (5000.0f, sr);
            reset();
        }
        void reset() override
        {
            for (auto& c : st) c.reset();
            for (auto& d : comb) d.reset();
            for (auto& o : combDamp) o.reset();
            hopCount = { 0.0f, 0.0f };
            ringPhase = 0.0;
            ramp = 0;
        }

        void process (float* const* ch, int numCh, int n, const ParamBlock& p, const Transport&) override
        {
            for (int i = 0; i < n; ++i)
            {
                const float pitch = fx::map::robotPitchHz (p.at (0, i));
                const float hop = (float) sr / pitch;
                ringPhase += 30.0 / sr;
                ringPhase -= std::floor (ringPhase);
                const float ringMod = std::sin (twoPi * (float) ringPhase);
                const float rampGain = ramp < N ? hann01 (0.5f * (float) ramp / (float) N) : 1.0f;
                if (ramp < N) ++ramp;

                for (int c = 0; c < numCh; ++c)
                {
                    const auto cs = (size_t) c;
                    const float x = ch[c][i];
                    st[cs].in.push (x * rampGain);
                    hopCount[cs] += 1.0f;
                    if (hopCount[cs] >= hop)
                    {
                        hopCount[cs] -= hop;
                        robotFrame (st[cs], hop);
                    }
                    const float robo = st[cs].pop();
                    float y = x + (robo - x) * p.at (1, i);

                    // Tuned metallic comb at the robot pitch
                    const float fb = 0.5f + 0.47f * p.at (3, i);
                    const float delayed = comb[cs].read (juce::jmax (2.0f, (float) sr / pitch));
                    const float cIn = y + fb * combDamp[cs].lp (delayed);
                    comb[cs].push (fastTanh (cIn * 0.7f) / 0.7f);
                    y += (cIn * (1.0f - fb) * 1.5f - y) * p.at (2, i);

                    const float dal = p.at (4, i);
                    y *= (1.0f - dal) + dal * ringMod;

                    ch[c][i] = x + (y - x) * p.at (5, i);
                }
            }
        }

    private:
        void robotFrame (StftChannel& c, float hop)
        {
            for (int k = 0; k < N; ++k) work[(size_t) k] = c.in.at (N - k) * win[(size_t) k];
            std::fill (work.begin() + N, work.end(), 0.0f);
            fft->performRealOnlyForwardTransform (work.data(), true);
            for (int k = 0; k <= N / 2; ++k)
            {
                const float re = work[(size_t) (2 * k)], im = work[(size_t) (2 * k + 1)];
                work[(size_t) (2 * k)] = std::sqrt (re * re + im * im);
                work[(size_t) (2 * k + 1)] = 0.0f;
            }
            fft->performRealOnlyInverseTransform (work.data());
            // Zero phase = a pulse at sample 0: centre it, window, overlap-add.
            const float g = 1.6f * hop / (float) N;
            for (int k = 0; k < N; ++k)
                frame[(size_t) k] = work[(size_t) ((k + N / 2) & (N - 1))] * win[(size_t) k] * g;
            c.addFrame (frame.data(), N);
        }

        double sr = 48000.0, ringPhase = 0.0;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> work, frame, win;
        std::array<StftChannel, 2> st;
        std::array<DelayLine, 2> comb;
        std::array<OnePole, 2> combDamp;
        std::array<float, 2> hopCount { 0.0f, 0.0f };
        int ramp = 0;
    };

    //==========================================================================
    class VinylFx final : public Effect
    {
    public:
        void prepare (double s, int) override
        {
            sr = s;
            for (auto& d : dl) d.prepare ((int) (sr * 0.1));
            presence.set (0.02f, 1.5f, sr);
            for (auto& o : hissBp) o.set (5000.0f, 0.6f, sr);
            for (auto& o : popLp) o.setHz (900.0f, sr);
            for (auto& o : clickHp) o.setHz (1500.0f, sr);
            reset();
        }
        void reset() override
        {
            for (auto& d : dl) d.reset();
            for (auto& f : hp) f.reset();
            for (auto& f : lp) f.reset();
            for (auto& o : hissBp) o.reset();
            for (auto& o : popLp) o.reset();
            for (auto& o : clickHp) o.reset();
            presence.reset();
            wow = flutter = humPhase = 0.0;
            crackleEnv = { 0.0f, 0.0f }; popEnv = { 0.0f, 0.0f };
            rng = Rng (31337u);
            counter = 0;
            lastAge = -1.0f;
        }

        void process (float* const* ch, int numCh, int n, const ParamBlock& p, const Transport&) override
        {
            for (int i = 0; i < n; ++i)
            {
                const float wowA = p.at (0, i), flA = p.at (1, i), crA = p.at (2, i), noiseA = p.at (3, i), age = p.at (4, i), mix = p.at (5, i);
                if ((counter++ & 31) == 0 && std::abs (age - lastAge) > 1.0e-4f)
                {
                    lastAge = age;
                    for (int c = 0; c < 2; ++c)
                    {
                        hp[(size_t) c].set (40.0f + 260.0f * age, 0.7f, sr);
                        lp[(size_t) c].set (18000.0f - 14500.0f * std::pow (age, 0.7f), 0.75f, sr);
                    }
                }

                wow += 0.55 / sr;  wow -= std::floor (wow);
                flutter += 7.3 / sr; flutter -= std::floor (flutter);
                humPhase += 60.0 / sr; humPhase -= std::floor (humPhase);
                const float wobble = wowA * 0.006f * (float) sr * (0.5f + 0.5f * std::sin (twoPi * (float) wow))
                                   + flA * 0.0004f * (float) sr * std::sin (twoPi * (float) flutter);

                float mono = 0.0f;
                for (int c = 0; c < numCh; ++c) mono += ch[c][i];
                const float pres = juce::jmin (1.0f, presence.process (mono) * 25.0f);

                for (int c = 0; c < numCh; ++c)
                {
                    const auto cs = (size_t) c;
                    const float x = ch[c][i];
                    dl[cs].push (x);
                    float y = dl[cs].read (0.012f * (float) sr + wobble);

                    // Age: narrow band, saturate, collapse stereo
                    y = hp[cs].process (y).hp;
                    y = lp[cs].process (y).lp;
                    y = fastTanh (y * (1.0f + 2.0f * age)) / (1.0f + age);

                    // Crackle (sharp clicks) and pops (soft thumps)
                    if (rng.next01() < crA * crA * 0.0016f) crackleEnv[cs] = (rng.nextBi() * 0.5f) * (0.3f + rng.next01());
                    if (rng.next01() < crA * 0.00004f) popEnv[cs] = rng.nextBi() * 0.6f;
                    const float click = clickHp[cs].hp (crackleEnv[cs]);
                    crackleEnv[cs] *= 0.6f;
                    const float pop = popLp[cs].lp (popEnv[cs]);
                    popEnv[cs] *= 0.97f;

                    const float hiss = hissBp[cs].bpNorm (rng.nextBi()) * 0.05f * noiseA;
                    const float hum = 0.012f * noiseA * std::sin (twoPi * (float) humPhase);
                    y += (click * 0.8f + pop * 0.5f + hiss + hum) * pres;

                    ch[c][i] = x + (y - x) * mix;
                }
                if (numCh == 2 && age > 0.0f)
                {
                    const float m = 0.5f * (ch[0][i] + ch[1][i]);
                    const float k = age * 0.7f * mix;
                    ch[0][i] += (m - ch[0][i]) * k;
                    ch[1][i] += (m - ch[1][i]) * k;
                }
            }
        }

    private:
        double sr = 48000.0, wow = 0.0, flutter = 0.0, humPhase = 0.0;
        std::array<DelayLine, 2> dl;
        std::array<Svf, 2> hp, lp, hissBp;
        std::array<OnePole, 2> popLp, clickHp;
        Envelope presence;
        std::array<float, 2> crackleEnv { 0.0f, 0.0f }, popEnv { 0.0f, 0.0f };
        Rng rng { 31337u };
        uint32_t counter = 0;
        float lastAge = -1.0f;
    };

    //==========================================================================
    class RingFx final : public Effect
    {
    public:
        void prepare (double s, int) override { sr = s; env.set (0.005f, 0.15f, sr); reset(); }
        void reset() override { carPhase = { 0.0, 0.25 }; modPhase = { 0.0, 0.1 }; env.reset(); }

        void process (float* const* ch, int numCh, int n, const ParamBlock& p, const Transport&) override
        {
            for (int i = 0; i < n; ++i)
            {
                float mono = 0.0f;
                for (int c = 0; c < numCh; ++c) mono += ch[c][i];
                const float e = juce::jmin (1.0f, env.process (mono) * 4.0f);

                const float f0 = fx::map::ringFreqHz (p.at (0, i)) * (1.0f + p.at (2, i) * e * 3.0f);
                const float k = 1.0f + p.at (1, i) * 14.0f;
                const float norm = 1.0f / std::tanh (k);
                const float index = p.at (3, i) * 4.0f;
                const float mix = p.at (5, i);

                for (int c = 0; c < numCh; ++c)
                {
                    const auto cs = (size_t) c;
                    const float f = f0 * (c == 1 ? 1.0f + p.at (4, i) * 0.04f : 1.0f);
                    carPhase[cs] += f / sr;                   carPhase[cs] -= std::floor (carPhase[cs]);
                    modPhase[cs] += f * 1.4142 / sr;          modPhase[cs] -= std::floor (modPhase[cs]);
                    const float mod = std::sin (twoPi * (float) modPhase[cs]) * index;
                    const float car = std::tanh (k * std::sin (twoPi * (float) carPhase[cs] + mod)) * norm;
                    const float x = ch[c][i];
                    ch[c][i] = x + (x * car - x) * mix;
                }
            }
        }

    private:
        double sr = 48000.0;
        std::array<double, 2> carPhase { 0.0, 0.25 }, modPhase { 0.0, 0.1 };
        Envelope env;
    };

    //==========================================================================
    class GranularFx final : public Effect
    {
    public:
        static constexpr int maxGrains = 48;

        void prepare (double s, int) override
        {
            sr = s;
            for (auto& b : buf) b.prepare ((int) (sr * 5.5));
            win.resize (2048);
            for (int k = 0; k < 2048; ++k) win[(size_t) k] = hann01 ((float) k / 2047.0f);
            gainSmooth.setTime (0.05f, sr);
            reset();
        }
        void reset() override
        {
            for (auto& b : buf) b.reset();
            for (auto& g : grains) g.active = false;
            written = 0; untilNext = 0.0; rng = Rng (2024u); gainSmooth.z = 1.0f;
        }

        void process (float* const* ch, int numCh, int n, const ParamBlock& p, const Transport&) override
        {
            for (int i = 0; i < n; ++i)
            {
                for (int c = 0; c < numCh; ++c) buf[(size_t) c].push (ch[c][i]);
                ++written;

                const float sizeS = fx::map::granSizeMs (p.at (0, i)) * 0.001f * (float) sr;
                const float density = fx::map::granDensity (p.at (1, i));
                const float spray = p.at (3, i);

                untilNext -= 1.0;
                if (untilNext <= 0.0)
                {
                    untilNext += (sr / density) * (1.0 + (double) (rng.nextBi() * spray * 0.8f));
                    spawn (sizeS, fx::map::granPosMs (p.at (2, i)) * 0.001f * (float) sr, spray, p.at (4, i));
                }

                const float g = gainSmooth.lp (1.0f / std::sqrt (juce::jmax (1.0f, density * sizeS / (float) sr)));
                float outL = 0.0f, outR = 0.0f;
                for (auto& gr : grains)
                {
                    if (! gr.active) continue;
                    const float w = win[(size_t) juce::jmin (2047, (int) (gr.age / gr.len * 2047.0f))];
                    const float l = buf[0].readAbs (gr.pos) * w;
                    const float r = numCh > 1 ? buf[1].readAbs (gr.pos) * w : l;
                    outL += l * gr.gl;
                    outR += r * gr.gr;
                    gr.pos += gr.inc;
                    gr.age += 1.0f;
                    if (gr.age >= gr.len) gr.active = false;
                }

                const float mix = p.at (5, i);
                ch[0][i] += (outL * g * 1.3f - ch[0][i]) * mix;
                if (numCh > 1) ch[1][i] += (outR * g * 1.3f - ch[1][i]) * mix;
            }
        }

    private:
        struct Grain { bool active = false; double pos = 0.0, inc = 1.0; float len = 1.0f, age = 0.0f, gl = 1.0f, gr = 1.0f; };

        void spawn (float len, float posBack, float spray, float pitch)
        {
            Grain* slot = nullptr;
            for (auto& g : grains) if (! g.active) { slot = &g; break; }
            if (slot == nullptr) return;

            float semis = 0.0f;
            if (pitch <= 0.5f) semis = rng.nextBi() * 24.0f * pitch;
            else
            {
                const float r = rng.next01();
                semis = r < (pitch - 0.5f) * 2.0f ? (rng.next01() < 0.5f ? 12.0f : -12.0f) : 0.0f;
                semis += rng.nextBi() * 0.15f;
            }
            double inc = std::pow (2.0, (double) semis / 12.0);
            const bool reverse = rng.next01() < spray * 0.35f;

            const double lead = (double) len * juce::jmax (1.0, inc) + 64.0;
            const double back = (double) posBack + (double) (rng.next01() * spray) * (double) len * 3.0 + lead;
            const double maxBack = (double) buf[0].size() - (double) len * 4.0 - 64.0;
            double start = (double) written - juce::jmin (back, maxBack);
            if (reverse) { start += (double) len * inc; inc = -inc; }

            const float pan = rng.nextBi() * spray;
            slot->active = true;
            slot->pos = start;
            slot->inc = inc;
            slot->len = juce::jmax (16.0f, len);
            slot->age = 0.0f;
            slot->gl = std::sqrt (0.5f * (1.0f - pan)) * 1.414f;
            slot->gr = std::sqrt (0.5f * (1.0f + pan)) * 1.414f;
        }

        double sr = 48000.0, untilNext = 0.0;
        std::array<DelayLine, 2> buf;
        std::array<Grain, maxGrains> grains;
        std::vector<float> win;
        int64_t written = 0;
        Rng rng { 2024u };
        OnePole gainSmooth;
    };

    //==========================================================================
    class SpectralFx final : public Effect
    {
    public:
        static constexpr int order = 11, N = 1 << order, H = N / 4, bins = N / 2 + 1;

        void prepare (double s, int) override
        {
            sr = s;
            fft = std::make_unique<juce::dsp::FFT> (order);
            work.assign ((size_t) N * 2, 0.0f);
            frame.assign ((size_t) N, 0.0f);
            win.resize ((size_t) N);
            for (int k = 0; k < N; ++k) win[(size_t) k] = hann01 ((float) k / (float) N);
            for (auto& c : chans) { c.st.prepare (N); c.alloc(); }
            mag.assign ((size_t) bins, 0.0f); omega.assign ((size_t) bins, 0.0f);
            outMag.assign ((size_t) bins, 0.0f); outOmega.assign ((size_t) bins, 0.0f);
            reset();
        }
        void reset() override
        {
            for (auto& c : chans) c.clear();
            rng = Rng (555u);
            ramp = 0;
        }

        void process (float* const* ch, int numCh, int n, const ParamBlock& p, const Transport&) override
        {
            for (int i = 0; i < n; ++i)
            {
                // After a reset the input fades into the analysis window over one frame,
                // so the sudden start is not frozen into the spectrum as a broadband click.
                const float rampGain = ramp < N ? hann01 (0.5f * (float) ramp / (float) N) : 1.0f;
                if (ramp < N) ++ramp;
                for (int c = 0; c < numCh; ++c)
                {
                    auto& cs = chans[(size_t) c];
                    const float x = ch[c][i];
                    cs.st.in.push (x * rampGain);
                    if (++cs.hopCount >= H)
                    {
                        cs.hopCount = 0;
                        doFrame (cs, p.at (0, i), fx::map::specShiftBins (p.at (1, i)), fx::map::specStretch (p.at (2, i)),
                                 p.at (3, i), p.at (4, i));
                    }
                    const float y = cs.st.pop();
                    ch[c][i] = x + (y - x) * p.at (5, i);
                }
            }
        }

    private:
        struct Chan
        {
            StftChannel st;
            std::vector<float> lastPhase, synthPhase, frozenMag, frozenOmega;
            int hopCount = 0;
            bool primed = false;
            void alloc() { for (auto* v : { &lastPhase, &synthPhase, &frozenMag, &frozenOmega }) v->assign ((size_t) bins, 0.0f); }
            void clear()
            {
                st.reset();
                for (auto* v : { &lastPhase, &synthPhase, &frozenMag, &frozenOmega }) std::fill (v->begin(), v->end(), 0.0f);
                hopCount = 0;
                primed = false;
            }
        };

        static float princarg (float a) noexcept { return a - twoPi * std::floor ((a + pi) / twoPi); }

        void doFrame (Chan& c, float freeze, float shiftBins, float stretch, float blur, float thin)
        {
            for (int k = 0; k < N; ++k) work[(size_t) k] = c.st.in.at (N - k) * win[(size_t) k];
            std::fill (work.begin() + N, work.end(), 0.0f);
            fft->performRealOnlyForwardTransform (work.data(), true);

            float peak = 0.0f;
            for (int k = 0; k < bins; ++k)
            {
                const float re = work[(size_t) (2 * k)], im = work[(size_t) (2 * k + 1)];
                const float m = std::sqrt (re * re + im * im);
                const float ph = std::atan2 (im, re);
                const float expected = twoPi * (float) k * (float) H / (float) N;
                const float dev = c.primed ? princarg (ph - c.lastPhase[(size_t) k] - expected) : 0.0f;
                c.lastPhase[(size_t) k] = ph;
                // First frame after a reset: start the synthesis phases from the analysed phases,
                // so the first overlapping frames line up instead of clicking.
                if (! c.primed) c.synthPhase[(size_t) k] = ph - expected;
                mag[(size_t) k] = m;
                omega[(size_t) k] = (expected + dev) / (float) H;
                peak = juce::jmax (peak, m);
            }

            // Freeze: each bin jumps up to new energy instantly and decays at a rate set by
            // Freeze (0 = live, 100 = held for ~20 s), so the held spectrum keeps sustaining.
            const float f = juce::jlimit (0.0f, 1.0f, freeze);
            const float a = juce::jmin (0.9995f, 1.0f - std::pow (1.0f - f, 3.0f));
            for (int k = 0; k < bins; ++k)
            {
                auto& fm = c.frozenMag[(size_t) k];
                auto& fo = c.frozenOmega[(size_t) k];
                const float m = mag[(size_t) k];
                if (m >= fm) { fm = m; fo = omega[(size_t) k]; }
                else { fm = a * fm + (1.0f - a) * m; fo = a * fo + (1.0f - a) * omega[(size_t) k]; }
            }

            // Blur: smear magnitudes across neighbouring bins.
            const int passes = (int) std::lround (blur * 6.0f);
            for (int k = 0; k < bins; ++k) mag[(size_t) k] = c.frozenMag[(size_t) k];
            for (int pass = 0; pass < passes; ++pass)
            {
                float prev = mag[0];
                for (int k = 1; k < bins - 1; ++k)
                {
                    const float cur = mag[(size_t) k];
                    mag[(size_t) k] = 0.25f * prev + 0.5f * cur + 0.25f * mag[(size_t) k + 1];
                    prev = cur;
                }
            }

            // Thin: spectral gate keeps only the strongest partials.
            const float thr = peak * thin * thin * 0.35f;

            // Shift + stretch: move bins to new frequencies (phase-vocoder style).
            std::fill (outMag.begin(), outMag.end(), 0.0f);
            std::fill (outOmega.begin(), outOmega.end(), 0.0f);
            const float binHz = twoPi / (float) N;
            for (int k = 1; k < bins - 1; ++k)
            {
                float m = mag[(size_t) k];
                if (thr > 0.0f) m *= juce::jlimit (0.0f, 1.0f, (m - thr) / (thr + 1.0e-9f));
                if (m <= 0.0f) continue;
                const int nk = (int) std::lround ((float) k * stretch + shiftBins);
                if (nk < 1 || nk >= bins - 1) continue;
                outMag[(size_t) nk] += m;
                outOmega[(size_t) nk] = c.frozenOmega[(size_t) k] * stretch + shiftBins * binHz;
            }

            for (int k = 0; k < bins; ++k)
            {
                const float w = outMag[(size_t) k] > 0.0f ? outOmega[(size_t) k] : binHz * (float) k;
                c.synthPhase[(size_t) k] = princarg (c.synthPhase[(size_t) k] + w * (float) H + rng.nextBi() * blur * blur * 1.5f);
                work[(size_t) (2 * k)]     = outMag[(size_t) k] * std::cos (c.synthPhase[(size_t) k]);
                work[(size_t) (2 * k + 1)] = outMag[(size_t) k] * std::sin (c.synthPhase[(size_t) k]);
            }
            c.primed = true;
            fft->performRealOnlyInverseTransform (work.data());
            const float g = 1.0f / 1.5f;   // Hann^2 overlap at 75%
            for (int k = 0; k < N; ++k) frame[(size_t) k] = work[(size_t) k] * win[(size_t) k] * g;
            c.st.addFrame (frame.data(), N);
        }

        double sr = 48000.0;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> work, frame, win, mag, omega, outMag, outOmega;
        std::array<Chan, 2> chans;
        Rng rng { 555u };
        int ramp = 0;
    };

    //==========================================================================
    class FreqShiftFx final : public Effect
    {
    public:
        void prepare (double s, int) override { sr = s; reset(); }
        void reset() override
        {
            for (auto& h : hil) h.reset();
            osc = { 0.0, 0.0 }; lfo = 0.0; fbState = { 0.0f, 0.0f };
        }

        void process (float* const* ch, int numCh, int n, const ParamBlock& p, const Transport&) override
        {
            for (int i = 0; i < n; ++i)
            {
                lfo += fx::map::fshiftRateHz (p.at (3, i)) / sr;
                lfo -= std::floor (lfo);
                const float hz = fx::map::fshiftHz (p.at (0, i)) + fx::map::fshiftModHz (p.at (2, i)) * std::sin (twoPi * (float) lfo);
                const float fb = p.at (1, i) * 0.85f, stereo = p.at (4, i), mix = p.at (5, i);

                for (int c = 0; c < numCh; ++c)
                {
                    const auto cs = (size_t) c;
                    const float f = c == 1 ? hz * (1.0f - 2.0f * stereo) : hz;
                    osc[cs] += f / sr;
                    osc[cs] -= std::floor (osc[cs]);
                    const float x = ch[c][i];
                    float re = 0.0f, im = 0.0f;
                    hil[cs].process (x + fb * fastTanh (fbState[cs]), re, im);
                    const float ph = twoPi * (float) osc[cs];
                    const float y = re * std::cos (ph) + im * std::sin (ph);
                    fbState[cs] = y;
                    ch[c][i] = x + (y - x) * mix;
                }
            }
        }

    private:
        double sr = 48000.0, lfo = 0.0;
        std::array<Hilbert, 2> hil;
        std::array<double, 2> osc { 0.0, 0.0 };
        std::array<float, 2> fbState { 0.0f, 0.0f };
    };

    //==========================================================================
    class ReverseFx final : public Effect
    {
    public:
        void prepare (double s, int) override
        {
            sr = s;
            for (auto& b : buf) b.prepare ((int) (sr * 9.0));
            reset();
        }
        void reset() override
        {
            for (auto& b : buf) b.reset();
            written = 0; t = 0.0; chunk = 0.0; srcEnd = 0; reversed = true; speed = 1.0;
            clock.beat = 0.0; lastChunk = -1; rng = Rng (8080u);
        }

        void process (float* const* ch, int numCh, int n, const ParamBlock& p, const Transport& tr) override
        {
            clock.sync (tr);
            const double spb = BeatClock::samplesPerBeat (tr, sr);
            for (int i = 0; i < n; ++i)
            {
                const double beats = fx::map::reverseBeats[fx::map::reverseLen (p.at (0, i))];
                const double beat = clock.step (tr, sr);
                const auto idx = (int64_t) std::floor (beat / beats);
                if (idx != lastChunk)
                {
                    lastChunk = idx;
                    chunk = juce::jlimit (256.0, sr * 4.0, beats * spb);
                    srcEnd = written;
                    t = 0.0;
                    reversed = rng.next01() < p.at (1, i);
                    speed = fx::map::reverseSpeeds[fx::map::reverseSpeed (p.at (4, i))];
                }

                const float fadeLen = (float) juce::jmax (64.0, (double) p.at (3, i) * chunk * 0.5);
                const float env = juce::jmin (1.0f, (float) t / fadeLen, (float) (chunk - t) / fadeLen);
                const float fb = p.at (2, i) * 0.8f, mix = p.at (5, i);
                const double local = t * speed;

                for (int c = 0; c < numCh; ++c)
                {
                    auto& b = buf[(size_t) c];
                    const double pos = reversed ? (double) srcEnd - 1.0 - local
                                                : (double) srcEnd - chunk + std::fmod (local, chunk);
                    const float y = b.readAbs (pos) * juce::jmax (0.0f, env);
                    const float x = ch[c][i];
                    b.push (x + fb * y);
                    ch[c][i] = x + (y - x) * mix;
                }
                ++written;
                t += 1.0;
            }
        }

    private:
        double sr = 48000.0, t = 0.0, chunk = 1.0, speed = 1.0;
        std::array<DelayLine, 2> buf;
        int64_t written = 0, srcEnd = 0, lastChunk = -1;
        bool reversed = true;
        BeatClock clock;
        Rng rng { 8080u };
    };

    //==========================================================================
    inline std::unique_ptr<Effect> createEffect (int id)
    {
        switch (id)
        {
            case fx::Glitch:    return std::make_unique<GlitchFx>();
            case fx::Melt:      return std::make_unique<MeltFx>();
            case fx::Octave:    return std::make_unique<OctaveFx>();
            case fx::Formant:   return std::make_unique<FormantFx>();
            case fx::Destroy:   return std::make_unique<DestroyFx>();
            case fx::Warp:      return std::make_unique<WarpFx>();
            case fx::Robot:     return std::make_unique<RobotFx>();
            case fx::Vinyl:     return std::make_unique<VinylFx>();
            case fx::Ring:      return std::make_unique<RingFx>();
            case fx::Fuzz:      return std::make_unique<FuzzFx>();
            case fx::Granular:  return std::make_unique<GranularFx>();
            case fx::Spectral:  return std::make_unique<SpectralFx>();
            case fx::FreqShift: return std::make_unique<FreqShiftFx>();
            case fx::Reverse:   return std::make_unique<ReverseFx>();
            default: break;
        }
        return {};
    }
}
