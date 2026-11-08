#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"

namespace naji::ui
{
    // Centre piece: all five photos as a floating, fanned card composition.
    // The photo for the slot A effect moves to the front with an animated
    // reshuffle; the stack breathes, glows and pulses with the output level.
    class PhotoStage final : public juce::Component
    {
    public:
        explicit PhotoStage (LookAndFeel& lnf);

        void paint (juce::Graphics&) override;
        void resized() override;

        // Called by the editor's animation clock (~60 Hz).
        void tick (float dt, float level, int slotA, int slotB);

        static constexpr int numPhotos = 5;

        // True while cards are reshuffling or the readout is changing.
        bool isBusy() const
        {
            if (labelAnim < 0.99f) return true;
            for (auto& c : cards) if (std::abs (c.scale - poseScale (c.slot)) > 0.002f) return true;
            return false;
        }

    private:
        static float poseScale (int slot) { return slot == 2 ? 1.0f : (slot == 1 || slot == 3 ? 0.86f : 0.74f); }
        struct Card
        {
            float x = 0, y = 0, rot = 0, scale = 1, tint = 0;     // animated
            int slot = 2;
        };

        void layoutCards (bool snap);
        void buildCardImages();

        LookAndFeel& laf;
        std::array<juce::Image, numPhotos> originals, cardImages;
        std::array<juce::Image, numPhotos> composites;
        juce::Image tintMask, background, glow;
        void buildGlow();
        static constexpr int cardPad = 40;
        int bgCounter = 0;
        void renderBackground();
        std::array<Card, numPhotos> cards;
        int hero = 2, shownA = -1, shownB = -1, prevA = -1, prevB = -1;
        float time = 0.0f, level = 0.0f, labelAnim = 1.0f, sweep = 0.0f;
        int cardW = 150, cardH = 240;
    };

    // Scrolling input/output waveform, fed from the processor's lock-free scope.
    class WaveformView final : public juce::Component
    {
    public:
        explicit WaveformView (NajiDistortionProcessor& p, LookAndFeel& lnf) : proc (p), laf (lnf) {}
        void paint (juce::Graphics&) override;
        void tick();

    private:
        static constexpr int points = 360;
        NajiDistortionProcessor& proc;
        LookAndFeel& laf;
        std::array<float, points> in {}, out {};
        int readPos = 0;
        float idle = 0.0f;
    };
}
