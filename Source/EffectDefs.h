#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

/*
    Effect catalogue: one place that defines every effect, its 6 parameters,
    how each 0..1 parameter maps to a real unit, and how it is displayed.
    The DSP engines, the parameter layout and the UI all read from here, so
    the numbers shown on a knob are exactly what the DSP uses.
*/
namespace naji::fx
{
    enum Id : int
    {
        Off = 0, Glitch, Melt, Octave, Formant, Destroy, Warp, Robot,
        Vinyl, Ring, Fuzz, Granular, Spectral, FreqShift, Reverse,
        count
    };
    constexpr int numEffects = 14;     // excluding Off
    constexpr int numParams  = 6;

    enum class Category : int { Distort = 0, Pitch, Time, Voice };
    inline const char* categoryName (Category c)
    {
        switch (c)
        {
            case Category::Distort: return "DISTORT";
            case Category::Pitch:   return "PITCH + FREQ";
            case Category::Time:    return "TIME + TEXTURE";
            case Category::Voice:   return "VOICE + SPECTRAL";
        }
        return "";
    }

    //==========================================================================
    // Value maps (v is the normalised 0..1 parameter value)
    namespace map
    {
        inline float expo (float v, float lo, float hi) { return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, v)); }
        inline float bipolar (float v)                  { return juce::jlimit (-1.0f, 1.0f, v * 2.0f - 1.0f); }
        inline int   steps (float v, int n)             { return juce::jlimit (0, n - 1, (int) std::lround (v * (float) (n - 1))); }

        // Glitch
        inline constexpr float glitchBeats[] = { 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };
        inline constexpr const char* glitchNames[] = { "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
        inline int   glitchSlice (float v)   { return steps (v, 6); }
        inline int   glitchRepeats (float v) { return 1 + (int) std::lround (v * 15.0f); }

        // Melt
        inline float meltSpeedHz (float v)   { return expo (v, 0.1f, 8.0f); }
        inline float meltCents (float v)     { return 220.0f * v; }

        // Octave
        inline float octaveGrainMs (float v) { return 20.0f + 100.0f * v; }

        // Formant: vowel position 0..4 along A E I O U
        inline float formantVowel (float v)  { return 4.0f * juce::jlimit (0.0f, 1.0f, v); }
        inline float formantShiftSt (float v){ return bipolar (v) * 12.0f; }
        inline float formantQ (float v)      { return 3.0f + 27.0f * v * v; }
        inline float formantModRate (float v){ return 0.25f + 5.5f * v * v; }

        // Destroy
        inline float destroyBits (float v)   { return 16.0f - 14.0f * std::pow (juce::jlimit (0.0f, 1.0f, v), 0.6f); }

        // Warp
        inline float warpBendSt (float v)    { return bipolar (v) * 12.0f; }
        inline float warpSweepSt (float v)   { return 24.0f * v; }
        inline float warpRateHz (float v)    { return expo (v, 0.05f, 12.0f); }
        inline constexpr const char* warpShapes[] = { "Sine", "Triangle", "Saw", "Square", "Random" };
        inline int   warpShape (float v)     { return steps (v, 5); }

        // Robot
        inline float robotPitchHz (float v)  { return expo (v, 40.0f, 600.0f); }

        // Ring
        inline float ringFreqHz (float v)    { return expo (v, 1.0f, 4000.0f); }

        // Granular
        inline float granSizeMs (float v)    { return expo (v, 10.0f, 400.0f); }
        inline float granDensity (float v)   { return expo (v, 2.0f, 100.0f); }
        inline float granPosMs (float v)     { return 2000.0f * v * v; }

        // Spectral (bins at 2048-point FFT)
        inline float specShiftBins (float v) { const float b = bipolar (v); return 96.0f * b * std::abs (b); }
        inline float specStretch (float v)   { return std::pow (2.0f, bipolar (v)); }

        // Freq shift
        inline float fshiftHz (float v)      { const float b = bipolar (v); return 2000.0f * b * b * b; }
        inline float fshiftModHz (float v)   { return 600.0f * v * v; }
        inline float fshiftRateHz (float v)  { return expo (v, 0.05f, 8.0f); }

        // Reverse
        inline constexpr float reverseBeats[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
        inline constexpr const char* reverseNames[] = { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars" };
        inline int   reverseLen (float v)    { return steps (v, 6); }
        inline constexpr float reverseSpeeds[] = { 0.5f, 1.0f, 2.0f };
        inline constexpr const char* reverseSpeedNames[] = { "0.5x", "1x", "2x" };
        inline int   reverseSpeed (float v)  { return steps (v, 3); }
    }

    //==========================================================================
    namespace fmt
    {
        using F = juce::String (*) (float);
        inline juce::String pct (float v)   { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
        inline juce::String hz (float x)
        {
            if (x >= 1000.0f) return juce::String (x / 1000.0f, x >= 10000.0f ? 1 : 2) + " kHz";
            if (x >= 100.0f)  return juce::String (juce::roundToInt (x)) + " Hz";
            return juce::String (x, x < 10.0f ? 2 : 1) + " Hz";
        }
        inline juce::String st (float s)    { return (s > 0.05f ? "+" : "") + juce::String (s, 1) + " st"; }
        inline juce::String ms (float m)    { return m < 100.0f ? juce::String (m, 1) + " ms" : juce::String (juce::roundToInt (m)) + " ms"; }

        inline juce::String glitchSlice (float v)  { return map::glitchNames[map::glitchSlice (v)]; }
        inline juce::String glitchRepeat (float v) { return "x" + juce::String (map::glitchRepeats (v)); }
        inline juce::String meltWobble (float v)   { return juce::String (juce::roundToInt (map::meltCents (v))) + " ct"; }
        inline juce::String meltSpeed (float v)    { return hz (map::meltSpeedHz (v)); }
        inline juce::String octaveGrain (float v)  { return ms (map::octaveGrainMs (v)); }
        inline juce::String vowel (float v)
        {
            static const char* names[] = { "A", "E", "I", "O", "U" };
            const float p = map::formantVowel (v);
            const int i = juce::jmin (3, (int) p);
            const float f = p - (float) i;
            if (f < 0.12f) return names[i];
            if (f > 0.88f) return names[i + 1];
            return juce::String (names[i]) + " > " + names[i + 1];
        }
        inline juce::String formantShift (float v) { return st (map::formantShiftSt (v)); }
        inline juce::String formantRes (float v)   { return "Q " + juce::String (map::formantQ (v), 1); }
        inline juce::String bits (float v)         { return juce::String (map::destroyBits (v), 1) + " bit"; }
        inline juce::String warpBend (float v)     { return st (map::warpBendSt (v)); }
        inline juce::String warpSweep (float v)    { return v < 0.002f ? juce::String ("0 st") : "+/- " + juce::String (map::warpSweepSt (v), 1) + " st"; }
        inline juce::String warpRate (float v)     { return hz (map::warpRateHz (v)); }
        inline juce::String warpShape (float v)    { return map::warpShapes[map::warpShape (v)]; }
        inline juce::String robotPitch (float v)   { return hz (map::robotPitchHz (v)); }
        inline juce::String ringFreq (float v)     { return hz (map::ringFreqHz (v)); }
        inline juce::String granSize (float v)     { return ms (map::granSizeMs (v)); }
        inline juce::String granDensity (float v)  { return juce::String (map::granDensity (v), 1) + " /s"; }
        inline juce::String granPos (float v)      { return ms (map::granPosMs (v)); }
        inline juce::String granPitch (float v)
        {
            if (v < 0.005f) return "0 st";
            if (v <= 0.5f) return "+/- " + juce::String (24.0f * v, 1) + " st";
            return "Oct " + juce::String (juce::roundToInt ((v - 0.5f) * 200.0f)) + "%";
        }
        inline juce::String specShift (float v)
        {
            const float h = map::specShiftBins (v) * 23.4375f;   // bin width at 48 kHz / 2048
            return (h > 0.5f ? "+" : (h < -0.5f ? "-" : "")) + hz (std::abs (h));
        }
        inline juce::String specStretch (float v)  { return juce::String (map::specStretch (v), 2) + "x"; }
        inline juce::String fshift (float v)
        {
            const float h = map::fshiftHz (v);
            return (h > 0.05f ? "+" : (h < -0.05f ? "-" : "")) + hz (std::abs (h));
        }
        inline juce::String fshiftMod (float v)    { return v < 0.002f ? juce::String ("0 Hz") : "+/- " + hz (map::fshiftModHz (v)); }
        inline juce::String fshiftRate (float v)   { return hz (map::fshiftRateHz (v)); }
        inline juce::String revLen (float v)       { return map::reverseNames[map::reverseLen (v)]; }
        inline juce::String revSpeed (float v)     { return map::reverseSpeedNames[map::reverseSpeed (v)]; }
    }

    //==========================================================================
    struct ParamDef
    {
        const char* suffix;
        const char* name;
        float def;          // 0..1
        fmt::F format;
    };

    struct EffectDef
    {
        const char* id;       // parameter ID prefix
        const char* name;     // display name
        Category cat;
        const char* blurb;
        int photo;            // which photo the centre display features
        ParamDef p[numParams];
    };

    inline const EffectDef& def (int effect)
    {
        using namespace fmt;
        static const EffectDef defs[numEffects] = {
            { "glitch", "GLITCH", Category::Time, "Tempo-synced buffer stutters, reverses, tape stops and dropouts", 1,
              { { "slice", "Slice", 0.6f, glitchSlice }, { "chaos", "Chaos", 0.55f, pct }, { "repeat", "Repeat", 0.3f, glitchRepeat },
                { "pitch", "Pitch", 0.3f, pct }, { "chop", "Chop", 0.2f, pct }, { "mix", "Mix", 1.0f, pct } } },
            { "melt", "MELT", Category::Time, "Unstable tape pitch, smeared phase and sagging transients", 4,
              { { "wobble", "Wobble", 0.45f, meltWobble }, { "speed", "Speed", 0.35f, meltSpeed }, { "drift", "Drift", 0.4f, pct },
                { "smear", "Smear", 0.5f, pct }, { "sag", "Sag", 0.4f, pct }, { "mix", "Mix", 1.0f, pct } } },
            { "octave", "OCTAVE", Category::Pitch, "Real octave up and down voices, analog sub and rectified harmonics", 2,
              { { "down", "Down", 0.7f, pct }, { "up", "Up", 0.45f, pct }, { "sub", "Sub", 0.3f, pct },
                { "harm", "Harmonics", 0.3f, pct }, { "grain", "Grain", 0.4f, octaveGrain }, { "blend", "Blend", 0.7f, pct } } },
            { "formant", "FORMANT", Category::Voice, "Vowel resonator bank: talking, singing, mutating tones", 0,
              { { "vowel", "Vowel", 0.0f, vowel }, { "shift", "Shift", 0.5f, formantShift }, { "res", "Resonance", 0.45f, formantRes },
                { "mod", "Mod", 0.35f, pct }, { "move", "Movement", 0.3f, pct }, { "amount", "Intensity", 0.85f, pct } } },
            { "destroy", "DESTROY", Category::Distort, "Brutal hard clipping, folding, bit smashing and sputter", 3,
              { { "crunch", "Crunch", 0.6f, pct }, { "fold", "Fold", 0.45f, pct }, { "bits", "Bits", 0.75f, bits },
                { "rect", "Rectify", 0.35f, pct }, { "sputter", "Sputter", 0.25f, pct }, { "mix", "Mix", 1.0f, pct } } },
            { "warp", "WARP", Category::Pitch, "Pitch bends, sweeps, stepped random pitch and self-FM", 2,
              { { "bend", "Bend", 0.5f, warpBend }, { "sweep", "Sweep", 0.35f, warpSweep }, { "rate", "Rate", 0.45f, warpRate },
                { "shape", "Shape", 0.0f, warpShape }, { "fm", "FM", 0.15f, pct }, { "mix", "Mix", 1.0f, pct } } },
            { "robot", "ROBOT", Category::Voice, "Phase-zeroed robot voice, tuned metal comb and ring", 4,
              { { "pitch", "Pitch", 0.35f, robotPitch }, { "robot", "Robotize", 0.8f, pct }, { "metal", "Metal", 0.4f, pct },
                { "res", "Resonance", 0.5f, pct }, { "ring", "Dalek", 0.2f, pct }, { "mix", "Mix", 1.0f, pct } } },
            { "vinyl", "VINYL", Category::Distort, "Worn record: wow, flutter, crackle, hiss and age", 0,
              { { "wow", "Wow", 0.4f, pct }, { "flutter", "Flutter", 0.3f, pct }, { "crackle", "Crackle", 0.5f, pct },
                { "noise", "Noise", 0.3f, pct }, { "age", "Age", 0.5f, pct }, { "mix", "Mix", 1.0f, pct } } },
            { "ring", "RING", Category::Pitch, "Ring modulation with FM carrier, tracking and stereo spread", 3,
              { { "freq", "Freq", 0.55f, ringFreq }, { "shape", "Shape", 0.2f, pct }, { "track", "Track", 0.0f, pct },
                { "fm", "FM", 0.2f, pct }, { "spread", "Spread", 0.3f, pct }, { "mix", "Mix", 1.0f, pct } } },
            { "fuzz", "FUZZ", Category::Distort, "Starved, biased, stacked transistor fuzz", 1,
              { { "fuzz", "Fuzz", 0.6f, pct }, { "bias", "Bias", 0.35f, pct }, { "gate", "Gate", 0.15f, pct },
                { "stack", "Stack", 0.55f, pct }, { "scoop", "Scoop", 0.45f, pct }, { "mix", "Mix", 1.0f, pct } } },
            { "gran", "GRANULAR", Category::Time, "Clouds of tiny grains with spray, position and pitch", 2,
              { { "size", "Size", 0.45f, granSize }, { "density", "Density", 0.5f, granDensity }, { "pos", "Position", 0.25f, granPos },
                { "spray", "Spray", 0.4f, pct }, { "pitch", "Pitch", 0.15f, granPitch }, { "mix", "Mix", 1.0f, pct } } },
            { "spec", "SPECTRAL", Category::Voice, "Phase-vocoder freeze, frequency shift, stretch and blur", 0,
              { { "freeze", "Freeze", 0.5f, pct }, { "shift", "Shift", 0.5f, specShift }, { "stretch", "Stretch", 0.5f, specStretch },
                { "blur", "Blur", 0.3f, pct }, { "thin", "Thin", 0.0f, pct }, { "mix", "Mix", 1.0f, pct } } },
            { "fshift", "FREQ SHIFT", Category::Pitch, "Single-sideband frequency shifting with feedback spirals", 3,
              { { "shift", "Shift", 0.7f, fshift }, { "fb", "Feedback", 0.2f, pct }, { "mod", "Mod", 0.2f, fshiftMod },
                { "rate", "Rate", 0.4f, fshiftRate }, { "stereo", "Stereo", 0.3f, pct }, { "mix", "Mix", 1.0f, pct } } },
            { "rev", "REVERSE", Category::Time, "Reversed buffer slices, rhythmic flips and swells", 1,
              { { "len", "Length", 0.4f, revLen }, { "chance", "Chance", 0.8f, pct }, { "fb", "Feedback", 0.2f, pct },
                { "fade", "Fade", 0.4f, pct }, { "speed", "Speed", 0.5f, revSpeed }, { "mix", "Mix", 1.0f, pct } } },
        };
        return defs[juce::jlimit (1, numEffects, effect) - 1];
    }

    inline juce::String paramId (int effect, int p)
    {
        const auto& d = def (effect);
        return juce::String (d.id) + "_" + d.p[p].suffix;
    }

    inline juce::String effectName (int effect) { return effect <= 0 ? "OFF" : def (effect).name; }

    // Photo shown for each effect (index into the 5 photos).
    inline int photoFor (int effect) { return effect <= 0 ? 2 : def (effect).photo; }
}
