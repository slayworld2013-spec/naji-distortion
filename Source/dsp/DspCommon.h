#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Small DSP building blocks shared by the effect engines.
namespace naji::dsp
{
    constexpr float pi    = 3.14159265358979f;
    constexpr float twoPi = 6.28318530717959f;

    // Host tempo / position, passed to tempo-synced engines once per block.
    struct Transport
    {
        double bpm = 120.0;
        bool   playing = false;
        double ppq = 0.0;        // position in quarter notes at the start of the block
    };

    //==========================================================================
    // Deterministic, allocation-free random numbers (xorshift32).
    struct Rng
    {
        uint32_t s = 0x12345678u;
        explicit Rng (uint32_t seed = 0x12345678u) : s (seed ? seed : 1u) {}
        uint32_t nextU() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float next01() noexcept   { return (float) (nextU() >> 8) * (1.0f / 16777216.0f); }
        float nextBi() noexcept   { return next01() * 2.0f - 1.0f; }
    };

    inline float fastTanh (float x) noexcept
    {
        if (x > 3.0f) return 1.0f;
        if (x < -3.0f) return -1.0f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    inline float fastSin (float x) noexcept
    {
        constexpr float invTwoPi = 0.159154943f;
        x -= twoPi * std::floor (x * invTwoPi + 0.5f);
        const float x2 = x * x;
        return x * (1.0f + x2 * (-0.16605f + x2 * (0.00761f + x2 * -0.000142f)));
    }

    inline float expMap (float v, float lo, float hi) noexcept { return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, v)); }

    // 0..1 -> 0..1 equal-power-ish raised cosine window
    inline float hann01 (float p) noexcept { return 0.5f - 0.5f * std::cos (twoPi * p); }

    //==========================================================================
    struct OnePole
    {
        float z = 0.0f, a = 0.0f;
        void setHz (float hz, double sr) noexcept { a = 1.0f - std::exp (-twoPi * hz / (float) sr); }
        void setTime (float seconds, double sr) noexcept { a = 1.0f - std::exp (-1.0f / (seconds * (float) sr)); }
        float lp (float x) noexcept { z += a * (x - z); return z; }
        float hp (float x) noexcept { z += a * (x - z); return x - z; }
        void reset() noexcept { z = 0.0f; }
    };

    // Attack/release envelope follower on |x|
    struct Envelope
    {
        float env = 0.0f, att = 0.0f, rel = 0.0f;
        void set (float attackS, float releaseS, double sr) noexcept
        {
            att = 1.0f - std::exp (-1.0f / (attackS * (float) sr));
            rel = 1.0f - std::exp (-1.0f / (releaseS * (float) sr));
        }
        float process (float x) noexcept
        {
            const float a = std::abs (x);
            env += (a > env ? att : rel) * (a - env);
            return env;
        }
        void reset() noexcept { env = 0.0f; }
    };

    // TPT state-variable filter (Cytomic / Zavalishin). Coefficients can be
    // changed every sample without blowing up.
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;

        void set (float hz, float q, double sr) noexcept
        {
            hz = juce::jlimit (10.0f, (float) sr * 0.45f, hz);
            g = std::tan (pi * hz / (float) sr);
            k = 1.0f / juce::jmax (0.05f, q);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        struct Out { float lp, bp, hp; };
        Out process (float x) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return { v2, v1, x - k * v1 - v2 };
        }
        // Band-pass normalised to 0 dB at the peak.
        float bpNorm (float x) noexcept { return k * process (x).bp; }
        void reset() noexcept { ic1 = ic2 = 0.0f; }
    };

    // First-order allpass  y = -a x + x1 + a y1
    struct Allpass1
    {
        float x1 = 0.0f, y1 = 0.0f;
        float process (float x, float a) noexcept
        {
            const float y = -a * x + x1 + a * y1;
            x1 = x; y1 = y;
            return y;
        }
        void reset() noexcept { x1 = y1 = 0.0f; }
    };

    //==========================================================================
    // Power-of-two circular buffer with fractional (cubic) reads.
    struct DelayLine
    {
        std::vector<float> buf;
        int mask = 0, w = 0;

        void prepare (int minSize)
        {
            int size = 1;
            while (size < minSize) size <<= 1;
            buf.assign ((size_t) size, 0.0f);
            mask = size - 1;
            w = 0;
        }
        void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        int size() const noexcept { return mask + 1; }

        void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }

        // Sample written `d` samples ago (d >= 1 is the last pushed sample).
        float at (int d) const noexcept { return buf[(size_t) ((w - d) & mask)]; }

        // Absolute index read (index wraps), integer
        float idx (int64_t i) const noexcept { return buf[(size_t) (i & mask)]; }

        // Fractional delay read, cubic Hermite. d >= 1.
        float read (float d) const noexcept
        {
            d = juce::jlimit (1.0f, (float) (mask - 3), d);
            const int di = (int) d;
            const float f = d - (float) di;
            const float xm1 = at (di - 1 < 1 ? 1 : di - 1);
            const float x0 = at (di);
            const float x1 = at (di + 1);
            const float x2 = at (di + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }

        // Fractional read at an absolute (monotonic) position, linear.
        float readAbs (double pos) const noexcept
        {
            const double fl = std::floor (pos);
            const auto i = (int64_t) fl;
            const float f = (float) (pos - fl);
            const float a = idx (i), b = idx (i + 1);
            return a + (b - a) * f;
        }
    };

    //==========================================================================
    // Delay-line pitch shifter: two read heads sweep through a window and
    // crossfade with a raised-cosine, so any ratio (0.25x .. 4x) works with
    // no clicks. The input must be pushed by the owner.
    struct PitchShifter
    {
        float phase = 0.0f;

        // Returns the shifted sample. window = grain length in samples.
        float process (const DelayLine& d, float ratio, float window) noexcept
        {
            window = juce::jmax (64.0f, window);
            phase += (1.0f - ratio) / window;
            phase -= std::floor (phase);
            const float p2 = phase + 0.5f >= 1.0f ? phase - 0.5f : phase + 0.5f;
            const float a = d.read (2.0f + phase * window);
            const float b = d.read (2.0f + p2 * window);
            return a * hann01 (phase) + b * hann01 (p2);
        }
        void reset() noexcept { phase = 0.0f; }
    };

    //==========================================================================
    // 90-degree phase splitter (Olli Niemitalo's 8th-order IIR design).
    // Gives an analytic signal (re, im) for single-sideband frequency shifting.
    struct Hilbert
    {
        struct Section
        {
            float a2 = 0.0f, x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
            float process (float x) noexcept
            {
                const float y = a2 * (x + y2) - x2;
                x2 = x1; x1 = x; y2 = y1; y1 = y;
                return y;
            }
        };
        std::array<Section, 4> p1, p2;
        float delayed = 0.0f;

        Hilbert()
        {
            const float c1[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
            const float c2[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
            for (int i = 0; i < 4; ++i) { p1[(size_t) i].a2 = c1[i] * c1[i]; p2[(size_t) i].a2 = c2[i] * c2[i]; }
        }
        void process (float x, float& re, float& im) noexcept
        {
            float a = x, b = x;
            for (auto& s : p1) a = s.process (a);
            for (auto& s : p2) b = s.process (b);
            re = delayed;    // path 1 delayed by one sample
            delayed = a;
            im = b;
        }
        void reset() noexcept
        {
            for (auto& s : p1) s.x1 = s.x2 = s.y1 = s.y2 = 0.0f;
            for (auto& s : p2) s.x1 = s.x2 = s.y1 = s.y2 = 0.0f;
            delayed = 0.0f;
        }
    };

    // Smooth random (filtered noise) in roughly -1..1.
    struct SmoothRandom
    {
        Rng rng;
        float target = 0.0f, value = 0.0f, counter = 0.0f;
        float process (float rateHz, double sr) noexcept
        {
            counter -= rateHz / (float) sr;
            if (counter <= 0.0f) { counter += 1.0f; target = rng.nextBi(); }
            value += (target - value) * juce::jmin (1.0f, 6.0f * rateHz / (float) sr);
            return value;
        }
        void reset() noexcept { target = value = counter = 0.0f; }
    };
}
