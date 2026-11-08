#include "PhotoStage.h"

namespace naji::ui
{
    namespace
    {
        struct SlotPose { float x, y, rot, scale, tint; };
        // Fan positions (x relative to half width, y in px, rotation in degrees)
        constexpr SlotPose poses[5] = {
            { -0.66f, 14.0f, -13.0f, 0.74f, 0.22f },
            { -0.35f,  4.0f,  -6.5f, 0.86f, 0.10f },
            {  0.0f,  -8.0f,   0.0f, 1.00f, 0.0f  },
            {  0.35f,  4.0f,   6.5f, 0.86f, 0.10f },
            {  0.66f, 14.0f,  13.0f, 0.74f, 0.22f },
        };
    }

    PhotoStage::PhotoStage (LookAndFeel& lnf) : laf (lnf)
    {
        const void* data[numPhotos] = { BinaryData::photo1_jpg, BinaryData::photo2_jpg, BinaryData::photo3_jpg,
                                        BinaryData::photo4_jpg, BinaryData::photo5_jpg };
        const int sizes[numPhotos] = { BinaryData::photo1_jpgSize, BinaryData::photo2_jpgSize, BinaryData::photo3_jpgSize,
                                       BinaryData::photo4_jpgSize, BinaryData::photo5_jpgSize };
        for (int i = 0; i < numPhotos; ++i)
            originals[(size_t) i] = juce::ImageFileFormat::loadFrom (data[i], (size_t) sizes[i]);
        setInterceptsMouseClicks (false, false);
    }

    void PhotoStage::resized()
    {
        cardH = juce::roundToInt ((float) getHeight() * 0.70f);
        cardW = juce::roundToInt ((float) cardH * 0.6f);
        buildCardImages();
        buildGlow();
        layoutCards (true);
        renderBackground();
    }

    void PhotoStage::renderBackground()
    {
        using namespace juce;
        const float W = (float) getWidth(), H = (float) getHeight();
        if (W < 1.0f || H < 1.0f) return;
        if (! background.isValid() || background.getWidth() != getWidth() * 2)
            background = Image (Image::ARGB, getWidth() * 2, getHeight() * 2, true);
        background.clear (background.getBounds());
        Graphics g (background);
        g.addTransform (AffineTransform::scale (2.0f));
        const Rectangle<float> b (0, 0, W, H);
        Path rounded;
        rounded.addRoundedRectangle (b, 16.0f);
        g.reduceClipRegion (rounded);   // corners baked in, so paint() needs no clip
        g.setGradientFill (ColourGradient (Colour (0xfffbfdff), 0, 0, Colour (0xffe3eef9), 0, H, false));
        g.fillRect (b);
        const float bx1 = W * (0.28f + 0.06f * std::sin (time * 0.21f)), by1 = H * (0.38f + 0.07f * std::cos (time * 0.17f));
        const float bx2 = W * (0.74f + 0.05f * std::cos (time * 0.15f)), by2 = H * (0.62f + 0.06f * std::sin (time * 0.19f));
        g.setGradientFill (ColourGradient (colours::glow.withAlpha (0.38f), bx1, by1, colours::glow.withAlpha (0.0f), bx1 + W * 0.42f, by1, true));
        g.fillRect (b);
        g.setGradientFill (ColourGradient (colours::accent.withAlpha (0.22f), bx2, by2, colours::accent.withAlpha (0.0f), bx2 + W * 0.38f, by2, true));
        g.fillRect (b);
        g.setColour (colours::accent2.withAlpha (0.045f));
        for (float x = std::fmod (time * 6.0f, 28.0f); x < W; x += 28.0f) g.drawVerticalLine ((int) x, 0.0f, H);
        for (float y = 0.0f; y < H; y += 28.0f) g.drawHorizontalLine ((int) y, 0.0f, W);
    }

    void PhotoStage::buildGlow()
    {
        using namespace juce;
        const int s = 256;
        glow = Image (Image::ARGB, s, s, true);
        Graphics g (glow);
        g.setGradientFill (ColourGradient (colours::accent, (float) s * 0.5f, (float) s * 0.5f,
                                           colours::accent.withAlpha (0.0f), (float) s, (float) s * 0.5f, true));
        g.fillEllipse (0.0f, 0.0f, (float) s, (float) s);
    }

    void PhotoStage::buildCardImages()
    {
        // Pre-scale each photo once (2x for hi-dpi), cover-cropped, slightly top-biased.
        const int w = cardW * 2, h = cardH * 2;
        for (int i = 0; i < numPhotos; ++i)
        {
            const auto& src = originals[(size_t) i];
            juce::Image img (juce::Image::ARGB, w, h, true);
            if (src.isValid())
            {
                juce::Graphics g (img);
                g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                const float s = juce::jmax ((float) w / (float) src.getWidth(), (float) h / (float) src.getHeight());
                const float dw = (float) src.getWidth() * s, dh = (float) src.getHeight() * s;
                g.drawImage (src, juce::Rectangle<float> (((float) w - dw) * 0.5f, ((float) h - dh) * 0.25f, dw, dh));
            }
            cardImages[(size_t) i] = img;
        }

        // Finished cards (shadow, white frame, rounded photo, blue fade, edge), rendered once at 2x,
        // so each animation frame only draws a handful of images.
        using namespace juce;
        const float pad = (float) cardPad;
        const int cw = (cardW + cardPad * 2) * 2, chh = (cardH + cardPad * 2) * 2;
        const auto innerR = Rectangle<float> (pad, pad, (float) cardW, (float) cardH).reduced (4.0f);
        Path card, inner;
        card.addRoundedRectangle (pad, pad, (float) cardW, (float) cardH, 14.0f);
        inner.addRoundedRectangle (innerR, 11.0f);
        for (int i = 0; i < numPhotos; ++i)
        {
            Image comp (Image::ARGB, cw, chh, true);
            Graphics g (comp);
            g.addTransform (AffineTransform::scale (2.0f));
            Path sh;
            sh.addRoundedRectangle (pad, pad + 8.0f, (float) cardW, (float) cardH, 14.0f);
            DropShadow (Colour (0xff1d3c66).withAlpha (0.35f), 26, {}).drawForPath (g, sh);
            g.setColour (Colours::white);
            g.fillPath (card);
            {
                Graphics::ScopedSaveState s (g);
                g.reduceClipRegion (inner);
                g.setImageResamplingQuality (Graphics::highResamplingQuality);
                g.drawImage (cardImages[(size_t) i], innerR);
                g.setGradientFill (ColourGradient (colours::accent.withAlpha (0.0f), 0, pad + (float) cardH * 0.6f,
                                                   colours::accent2.withAlpha (0.35f), 0, pad + (float) cardH, false));
                g.fillRect (innerR);
            }
            g.setColour (colours::edge);
            g.strokePath (card, PathStrokeType (1.0f));
            composites[(size_t) i] = comp;
        }
        // Icy tint layer (drawn with variable opacity over the back cards)
        tintMask = Image (Image::ARGB, cw, chh, true);
        {
            Graphics g (tintMask);
            g.addTransform (AffineTransform::scale (2.0f));
            g.setGradientFill (ColourGradient (colours::icy.withAlpha (0.9f), 0, pad, Colours::white.withAlpha (0.7f), 0, pad + (float) cardH, false));
            g.fillPath (inner);
        }
    }

    void PhotoStage::layoutCards (bool snap)
    {
        for (int i = 0; i < numPhotos; ++i)
        {
            auto& c = cards[(size_t) i];
            c.slot = ((i - hero + 2) % numPhotos + numPhotos) % numPhotos;
            if (snap)
            {
                const auto& pose = poses[c.slot];
                c.x = pose.x; c.y = pose.y; c.rot = pose.rot; c.scale = pose.scale; c.tint = pose.tint;
            }
        }
    }

    void PhotoStage::tick (float dt, float lvl, int slotA, int slotB)
    {
        time += dt;
        level = ease (level, juce::jlimit (0.0f, 1.0f, lvl), lvl > level ? 0.5f : 0.08f);
        sweep += dt * 0.22f;
        if (sweep > 1.6f) sweep -= 1.6f;

        const int newHero = fx::photoFor (slotA);
        const bool first = shownA < 0;
        if (newHero != hero) { hero = newHero; layoutCards (first); }
        if (first) { shownA = prevA = slotA; shownB = prevB = slotB; labelAnim = 1.0f; }
        if (slotA != shownA || slotB != shownB)
        {
            prevA = shownA < 0 ? slotA : shownA;
            prevB = shownB < 0 ? slotB : shownB;
            shownA = slotA; shownB = slotB;
            labelAnim = 0.0f;
        }
        labelAnim = ease (labelAnim, 1.0f, 0.12f);

        for (auto& c : cards)
        {
            const auto& pose = poses[c.slot];
            c.x = ease (c.x, pose.x, 0.12f);
            c.y = ease (c.y, pose.y, 0.12f);
            c.rot = ease (c.rot, pose.rot, 0.12f);
            c.scale = ease (c.scale, pose.scale, 0.12f);
            c.tint = ease (c.tint, pose.tint, 0.12f);
        }
        // The drifting background moves slowly: refresh its cached image ~8 times a second.
        if (++bgCounter >= 4) { bgCounter = 0; renderBackground(); }
        repaint();
    }

    void PhotoStage::paint (juce::Graphics& g)
    {
        using namespace juce;
        const auto b = getLocalBounds().toFloat();
        const float W = b.getWidth(), H = b.getHeight();

        // Background: cached soft gradient with slowly drifting icy blobs and a faint grid.
        {
            g.setImageResamplingQuality (Graphics::lowResamplingQuality);
            if (background.isValid()) g.drawImage (background, b);

            // Glow behind the front card, brighter with the audio (pre-rendered, scaled)
            const float gx = W * 0.5f, gy = H * 0.46f;
            const float gw = (float) cardW * (2.0f + 1.0f * level), gh = (float) cardH * (1.0f + 0.5f * level);
            g.setOpacity (0.18f + 0.45f * level);
            g.drawImage (glow, Rectangle<float> (gx - gw, gy - gh * 0.8f, gw * 2.0f, gh * 1.6f));
            g.setOpacity (1.0f);

            // Pulse rings
            for (int k = 0; k < 2; ++k)
            {
                const float ph = std::fmod (time * 0.35f + (float) k * 0.5f, 1.0f);
                const float rr = (float) cardW * (0.6f + 1.2f * ph);
                g.setColour (colours::accent.withAlpha ((1.0f - ph) * (0.10f + 0.25f * level)));
                g.drawEllipse (gx - rr, gy - rr * 0.8f, rr * 2.0f, rr * 1.6f, 1.2f);
            }

            // Cards, back to front
            std::array<int, numPhotos> order { 0, 1, 2, 3, 4 };
            std::sort (order.begin(), order.end(), [this] (int a, int c) { return cards[(size_t) a].scale < cards[(size_t) c].scale; });
            for (int idx : order)
            {
                const auto& c = cards[(size_t) idx];
                const bool front = c.slot == 2;
                const float bob = std::sin (time * 0.9f + (float) idx * 1.3f) * 3.0f;
                const float wob = std::sin (time * 0.7f + (float) idx) * 0.6f;
                const float sc = c.scale * (front ? 1.0f + 0.035f * level : 1.0f);
                const float cx = W * 0.5f + c.x * W * 0.5f * 0.86f, cy = H * 0.45f + c.y + bob;
                const auto t = AffineTransform::translation (-(float) cardW * 0.5f, -(float) cardH * 0.5f)
                                   .rotated (degreesToRadians (c.rot + wob)).scaled (sc).translated (cx, cy);

                const auto tc = AffineTransform::scale (0.5f).translated (-(float) cardPad, -(float) cardPad).followedBy (t);
                g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
                g.setOpacity (1.0f);   // images use the current colour's alpha, so reset it
                g.drawImageTransformed (composites[(size_t) idx], tc);
                if (c.tint > 0.01f)
                {
                    // icy tint blends the back cards into the design
                    g.setOpacity (jmin (1.0f, c.tint));
                    g.drawImageTransformed (tintMask, tc);
                    g.setOpacity (1.0f);
                }
                if (! front) continue;

                Graphics::ScopedSaveState cs (g);
                g.addTransform (t);
                Path card;
                card.addRoundedRectangle (0.0f, 0.0f, (float) cardW, (float) cardH, 14.0f);
                Path inner;
                const auto innerR = Rectangle<float> (0, 0, (float) cardW, (float) cardH).reduced (4.0f);
                inner.addRoundedRectangle (innerR, 11.0f);
                {
                    Graphics::ScopedSaveState is (g);
                    g.reduceClipRegion (inner);
                    {
                        // glossy light sweep
                        const float sx = (sweep - 0.3f) * (float) cardW * 2.0f;
                        ColourGradient gl (Colours::white.withAlpha (0.0f), sx, 0.0f, Colours::white.withAlpha (0.0f), sx + 90.0f, 90.0f, false);
                        gl.addColour (0.5, Colours::white.withAlpha (0.35f));
                        g.setGradientFill (gl);
                        g.fillRect (innerR);
                    }
                }
                g.setColour (colours::accent.withAlpha (0.9f));
                g.strokePath (card, PathStrokeType (2.0f));
            }

            // Effect readout pill
            const float a = labelAnim;
            auto pill = Rectangle<float> (W * 0.5f - 170.0f, H - 52.0f, 340.0f, 38.0f);
            g.setColour (Colours::white.withAlpha (0.82f));
            g.fillRoundedRectangle (pill, 19.0f);
            g.setColour (colours::accent.withAlpha (0.5f));
            g.drawRoundedRectangle (pill, 19.0f, 1.0f);

            auto drawChain = [&] (int fa, int fb, float alpha, float dy)
            {
                auto r = pill.translated (0.0f, dy).reduced (18.0f, 0.0f);
                const auto nameA = fx::effectName (fa), nameB = fx::effectName (fb);
                const auto big = laf.boldFont (15.0f).withExtraKerningFactor (0.08f);
                const auto small = laf.boldFont (10.0f).withExtraKerningFactor (0.15f);
                const float wa = GlyphArrangement::getStringWidth (big, nameA), wb = GlyphArrangement::getStringWidth (big, nameB);
                const float total = 14.0f + wa + 30.0f + 14.0f + wb;
                float x = r.getCentreX() - total * 0.5f;
                g.setFont (small);
                g.setColour (colours::textFaint.withAlpha (alpha));
                g.drawText ("A", Rectangle<float> (x, r.getY(), 12.0f, r.getHeight()), Justification::centredLeft);
                x += 14.0f;
                g.setFont (big);
                g.setColour ((fa == 0 ? colours::textFaint : colours::accent2).withAlpha (alpha));
                g.drawText (nameA, Rectangle<float> (x, r.getY(), wa + 2.0f, r.getHeight()), Justification::centredLeft);
                x += wa + 8.0f;
                Path arrow;
                arrow.startNewSubPath (x, r.getCentreY()); arrow.lineTo (x + 14.0f, r.getCentreY());
                arrow.startNewSubPath (x + 9.0f, r.getCentreY() - 4.0f); arrow.lineTo (x + 14.0f, r.getCentreY()); arrow.lineTo (x + 9.0f, r.getCentreY() + 4.0f);
                g.setColour (colours::textFaint.withAlpha (alpha));
                g.strokePath (arrow, PathStrokeType (1.5f));
                x += 22.0f;
                g.setFont (small);
                g.drawText ("B", Rectangle<float> (x, r.getY(), 12.0f, r.getHeight()), Justification::centredLeft);
                x += 14.0f;
                g.setFont (big);
                g.setColour ((fb == 0 ? colours::textFaint : colours::accent2).withAlpha (alpha));
                g.drawText (nameB, Rectangle<float> (x, r.getY(), wb + 2.0f, r.getHeight()), Justification::centredLeft);
            };
            Graphics::ScopedSaveState ps (g);
            g.reduceClipRegion (pill.toNearestInt());
            if (a < 0.99f) drawChain (prevA, prevB, 1.0f - a, -a * 18.0f);
            drawChain (shownA, shownB, a, (1.0f - a) * 18.0f);
        }

        g.setColour (Colours::white);
        g.drawRoundedRectangle (b.reduced (1.0f), 15.0f, 1.5f);
        g.setColour (colours::edge);
        g.drawRoundedRectangle (b.reduced (0.5f), 16.0f, 0.8f);

        // live indicator
        g.setColour (colours::accent.withAlpha (0.3f + 0.7f * level));
        g.fillEllipse (16.0f, 16.0f, 8.0f, 8.0f);
        g.setFont (laf.boldFont (10.0f).withExtraKerningFactor (0.2f));
        g.setColour (colours::textDim);
        g.drawText ("NAJI LAB", Rectangle<float> (30.0f, 10.0f, 120.0f, 20.0f), Justification::centredLeft);
    }

    //==========================================================================
    void WaveformView::tick()
    {
        const int w = proc.scope.written.load (std::memory_order_acquire);
        if (w - readPos > ScopeFeed::size) readPos = w - ScopeFeed::size;
        int added = 0;
        while (readPos < w)
        {
            const auto i = (size_t) (readPos & (ScopeFeed::size - 1));
            std::copy (in.begin() + 1, in.end(), in.begin());
            std::copy (out.begin() + 1, out.end(), out.begin());
            in.back() = proc.scope.in[i];
            out.back() = proc.scope.out[i];
            ++readPos;
            ++added;
        }
        idle += 0.03f;
        repaint();
    }

    void WaveformView::paint (juce::Graphics& g)
    {
        using namespace juce;
        const auto b = getLocalBounds().toFloat();
        drawPanel (g, b, 14.0f);
        auto area = b.reduced (16.0f, 12.0f);
        auto header = area.removeFromTop (14.0f);
        g.setFont (laf.boldFont (10.0f).withExtraKerningFactor (0.2f));
        g.setColour (colours::textDim);
        g.drawText ("SIGNAL", header, Justification::centredLeft);
        g.setColour (colours::textFaint);
        g.drawText ("IN", header.withTrimmedRight (40.0f), Justification::centredRight);
        g.setColour (colours::accent2);
        g.drawText ("OUT", header, Justification::centredRight);

        const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;
        g.setColour (colours::track);
        g.drawHorizontalLine ((int) mid, area.getX(), area.getRight());

        auto shape = [&] (const std::array<float, points>& v, float gain)
        {
            Path p;
            const float dx = area.getWidth() / (float) (points - 1);
            p.startNewSubPath (area.getX(), mid);
            for (int i = 0; i < points; ++i)
            {
                const float a = std::sqrt (jlimit (0.0f, 1.0f, v[(size_t) i] * gain));
                const float breathe = 0.015f * std::sin (idle + (float) i * 0.08f);
                p.lineTo (area.getX() + (float) i * dx, mid - (a + breathe) * half);
            }
            for (int i = points - 1; i >= 0; --i)
            {
                const float a = std::sqrt (jlimit (0.0f, 1.0f, v[(size_t) i] * gain));
                const float breathe = 0.015f * std::sin (idle + (float) i * 0.08f);
                p.lineTo (area.getX() + (float) i * dx, mid + (a + breathe) * half);
            }
            p.closeSubPath();
            return p;
        };

        g.setColour (colours::textFaint.withAlpha (0.35f));
        g.fillPath (shape (in, 1.0f));
        auto outP = shape (out, 1.0f);
        g.setGradientFill (ColourGradient (colours::accent.withAlpha (0.85f), area.getX(), area.getY(),
                                           colours::accent2.withAlpha (0.9f), area.getRight(), area.getBottom(), false));
        g.fillPath (outP);
        g.setColour (colours::accent2);
        g.strokePath (outP, PathStrokeType (1.0f));

        // fade the oldest samples into the panel
        g.setGradientFill (ColourGradient (Colours::white, area.getX(), 0, Colours::white.withAlpha (0.0f), area.getX() + 60.0f, 0, false));
        g.fillRect (area.withWidth (60.0f));
    }
}
