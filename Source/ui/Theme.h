#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"
#include "../EffectDefs.h"

// Bright "laboratory" theme: soft white, light grey glass panels, icy blue accents.
namespace naji::ui
{
    namespace colours
    {
        inline const juce::Colour bg        { 0xfff3f6fa };
        inline const juce::Colour bg2       { 0xffe8eef5 };
        inline const juce::Colour panel     { 0xffffffff };
        inline const juce::Colour panelGrey { 0xfff0f3f7 };
        inline const juce::Colour edge      { 0xffdde4ed };
        inline const juce::Colour track     { 0xffe1e7ef };
        inline const juce::Colour text      { 0xff262b33 };
        inline const juce::Colour textDim   { 0xff6b7686 };
        inline const juce::Colour textFaint { 0xffa4aebb };
        inline const juce::Colour accent    { 0xff5ab4ff };   // light blue
        inline const juce::Colour accent2   { 0xff2f7fd8 };   // deeper blue
        inline const juce::Colour icy       { 0xffd8efff };
        inline const juce::Colour glow      { 0xff8fd3ff };
        inline const juce::Colour hot       { 0xffff5a4e };
    }

    // Exponential approach used by every animation (frame-rate independent enough at 60 Hz).
    inline float ease (float current, float target, float speed = 0.18f)
    {
        const float d = target - current;
        return std::abs (d) < 1.0e-4f ? target : current + d * speed;
    }

    //==========================================================================
    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            bold   = juce::Typeface::createSystemTypefaceFor (BinaryData::InterBold_otf, BinaryData::InterBold_otfSize);
            medium = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_otf, BinaryData::InterMedium_otfSize);

            using namespace juce;
            setColour (ResizableWindow::backgroundColourId, colours::bg);
            setColour (PopupMenu::backgroundColourId, colours::panel);
            setColour (PopupMenu::textColourId, colours::text);
            setColour (PopupMenu::headerTextColourId, colours::accent2);
            setColour (PopupMenu::highlightedBackgroundColourId, colours::icy);
            setColour (PopupMenu::highlightedTextColourId, colours::text);
            setColour (TextEditor::backgroundColourId, colours::panelGrey);
            setColour (TextEditor::textColourId, colours::text);
            setColour (TextEditor::outlineColourId, colours::edge);
            setColour (TextEditor::focusedOutlineColourId, colours::accent);
            setColour (TextEditor::highlightColourId, colours::icy);
            setColour (CaretComponent::caretColourId, colours::accent2);
            setColour (ScrollBar::thumbColourId, colours::accent.withAlpha (0.5f));
            setColour (AlertWindow::backgroundColourId, colours::panel);
            setColour (AlertWindow::textColourId, colours::text);
            setColour (TextButton::buttonColourId, colours::panel);
            setColour (TextButton::textColourOffId, colours::text);
        }

        juce::Font boldFont (float h) const   { return juce::Font (juce::FontOptions (bold).withHeight (h)); }
        juce::Font mediumFont (float h) const { return juce::Font (juce::FontOptions (medium).withHeight (h)); }

        juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override { return f.isBold() ? bold : medium; }
        juce::Font getPopupMenuFont() override { return mediumFont (14.0f); }

        void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
        {
            g.fillAll (colours::panel);
            g.setColour (colours::edge);
            g.drawRect (0, 0, w, h);
        }

        // Glossy knob. "disp" (0..1) on the slider holds the animated position.
        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                               float startAngle, float endAngle, juce::Slider& slider) override
        {
            using namespace juce;
            if (auto* v = slider.getProperties().getVarPointer ("disp")) pos = (float) *v;
            const auto bounds = Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.0f);
            const float r = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto c = bounds.getCentre();
            const float arcR = r * 0.92f, arcW = jmax (3.0f, r * 0.085f);
            const float angle = startAngle + pos * (endAngle - startAngle);

            float origin = 0.0f;
            if (auto* o = slider.getProperties().getVarPointer ("arcOrigin")) origin = (float) *o;
            const float originAngle = startAngle + origin * (endAngle - startAngle);

            Path track;
            track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
            g.setColour (colours::track);
            g.strokePath (track, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));

            if (std::abs (angle - originAngle) > 0.002f)
            {
                Path value;
                value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, jmin (originAngle, angle), jmax (originAngle, angle), true);
                g.setColour (colours::glow.withAlpha (0.35f));
                g.strokePath (value, PathStrokeType (arcW * 2.4f, PathStrokeType::curved, PathStrokeType::rounded));
                g.setGradientFill (ColourGradient (colours::accent, c.x - r, c.y + r, colours::accent2, c.x + r, c.y - r, false));
                g.strokePath (value, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));
            }

            const float bodyR = r * 0.72f;
            const auto body = Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);

            // soft shadow
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (Colour (0xff1c3a5e).withAlpha (0.035f * (float) i));
                g.fillEllipse (body.translated (0.0f, r * 0.06f * (float) i).expanded ((float) (4 - i) * 1.5f));
            }
            g.setGradientFill (ColourGradient (Colours::white, c.x, body.getY(), Colour (0xffd9e1eb), c.x, body.getBottom(), false));
            g.fillEllipse (body);
            // glossy top highlight
            g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.95f), c.x, body.getY(),
                                               Colours::white.withAlpha (0.0f), c.x, c.y, false));
            g.fillEllipse (body.reduced (bodyR * 0.12f).withTrimmedBottom (bodyR * 0.7f));
            g.setColour (Colour (0xffc8d3e0));
            g.drawEllipse (body.reduced (0.5f), 1.0f);
            g.setColour (Colours::white);
            g.drawEllipse (body.reduced (2.0f), 1.0f);

            // pointer: blue dot near the rim
            const auto dot = c.getPointOnCircumference (bodyR * 0.68f, angle);
            const float dr = jmax (2.5f, r * 0.075f);
            g.setColour (colours::glow.withAlpha (slider.isMouseOverOrDragging() ? 0.6f : 0.35f));
            g.fillEllipse (Rectangle<float> (dr * 4.0f, dr * 4.0f).withCentre (dot));
            g.setColour (colours::accent2);
            g.fillEllipse (Rectangle<float> (dr * 2.0f, dr * 2.0f).withCentre (dot));
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const float c = r.getHeight() * 0.5f;
            g.setGradientFill (juce::ColourGradient (down ? colours::icy : juce::Colours::white, 0.0f, r.getY(),
                                                     down ? colours::glow.withAlpha (0.5f) : colours::panelGrey, 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, c);
            g.setColour (over ? colours::accent : colours::edge);
            g.drawRoundedRectangle (r, c, over ? 1.4f : 1.0f);
        }

        void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&) override
        {
            g.setColour (colours::panelGrey);
            g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, (float) h * 0.5f);
        }
        void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& te) override
        {
            g.setColour (te.hasKeyboardFocus (true) ? colours::accent : colours::edge);
            g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, (float) h * 0.5f, te.hasKeyboardFocus (true) ? 1.5f : 1.0f);
        }

        juce::Font getTextButtonFont (juce::TextButton&, int) override { return boldFont (11.0f).withExtraKerningFactor (0.12f); }

        void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                            int thumbStart, int thumbSize, bool over, bool) override
        {
            auto thumb = vertical ? juce::Rectangle<int> (x + w / 2 - 2, thumbStart, 4, thumbSize)
                                  : juce::Rectangle<int> (thumbStart, y + h / 2 - 2, thumbSize, 4);
            g.setColour (colours::accent.withAlpha (over ? 0.8f : 0.45f));
            g.fillRoundedRectangle (thumb.toFloat(), 2.0f);
        }

    private:
        juce::Typeface::Ptr bold, medium;
    };

    //==========================================================================
    // Soft white glass panel with a subtle shadow.
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner = 14.0f, float alpha = 0.92f)
    {
        using namespace juce;
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (Colour (0xff1c3a5e).withAlpha (0.018f * (float) i));
            g.fillRoundedRectangle (r.translated (0.0f, (float) i * 1.5f).expanded ((float) (5 - i)), corner + (float) (5 - i));
        }
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (alpha), r.getX(), r.getY(),
                                           Colour (0xfff4f7fb).withAlpha (alpha), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, corner);
        g.setColour (Colours::white);
        g.drawRoundedRectangle (r.reduced (1.0f), corner - 1.0f, 1.0f);
        g.setColour (colours::edge.withAlpha (0.8f));
        g.drawRoundedRectangle (r, corner, 0.8f);
    }

    //==========================================================================
    // Small vector icons for every effect, drawn into a unit square (0..1).
    inline juce::Path effectIcon (int effect)
    {
        using namespace juce;
        Path p;
        switch (effect)
        {
            case fx::Glitch:
                p.addRectangle (0.05f, 0.55f, 0.16f, 0.35f); p.addRectangle (0.29f, 0.2f, 0.16f, 0.7f);
                p.addRectangle (0.53f, 0.4f, 0.16f, 0.5f);   p.addRectangle (0.77f, 0.1f, 0.16f, 0.8f);
                break;
            case fx::Melt:
                p.startNewSubPath (0.05f, 0.25f);
                p.cubicTo (0.3f, 0.25f, 0.3f, 0.8f, 0.45f, 0.8f); p.cubicTo (0.6f, 0.8f, 0.55f, 0.35f, 0.75f, 0.45f);
                p.cubicTo (0.85f, 0.5f, 0.9f, 0.7f, 0.95f, 0.65f);
                break;
            case fx::Octave:
                p.addEllipse (0.28f, 0.05f, 0.44f, 0.4f); p.addEllipse (0.2f, 0.45f, 0.6f, 0.5f);
                break;
            case fx::Formant:
                p.addEllipse (0.05f, 0.25f, 0.9f, 0.55f);
                p.startNewSubPath (0.2f, 0.52f); p.quadraticTo (0.5f, 0.75f, 0.8f, 0.52f);
                break;
            case fx::Destroy:
                p.startNewSubPath (0.6f, 0.02f); p.lineTo (0.2f, 0.55f); p.lineTo (0.5f, 0.55f);
                p.lineTo (0.38f, 0.98f); p.lineTo (0.82f, 0.4f); p.lineTo (0.52f, 0.4f); p.closeSubPath();
                break;
            case fx::Warp:
                p.startNewSubPath (0.5f, 0.5f);
                for (int i = 1; i < 40; ++i)
                {
                    const float a = (float) i * 0.42f, rr = 0.012f * (float) i;
                    p.lineTo (0.5f + rr * std::cos (a), 0.5f + rr * std::sin (a));
                }
                break;
            case fx::Robot:
                p.addRoundedRectangle (0.15f, 0.3f, 0.7f, 0.6f, 0.1f);
                p.startNewSubPath (0.5f, 0.3f); p.lineTo (0.5f, 0.1f);
                p.addEllipse (0.44f, 0.02f, 0.12f, 0.12f);
                p.addEllipse (0.3f, 0.48f, 0.12f, 0.12f); p.addEllipse (0.58f, 0.48f, 0.12f, 0.12f);
                break;
            case fx::Vinyl:
                p.addEllipse (0.05f, 0.05f, 0.9f, 0.9f); p.addEllipse (0.38f, 0.38f, 0.24f, 0.24f);
                p.addEllipse (0.22f, 0.22f, 0.56f, 0.56f);
                break;
            case fx::Ring:
                p.addEllipse (0.05f, 0.25f, 0.5f, 0.5f); p.addEllipse (0.45f, 0.25f, 0.5f, 0.5f);
                break;
            case fx::Fuzz:
                p.startNewSubPath (0.02f, 0.5f);
                for (int i = 0; i < 4; ++i)
                {
                    const float x0 = 0.02f + (float) i * 0.24f;
                    p.lineTo (x0, 0.15f); p.lineTo (x0 + 0.12f, 0.15f); p.lineTo (x0 + 0.12f, 0.85f); p.lineTo (x0 + 0.24f, 0.85f);
                }
                break;
            case fx::Granular:
                for (auto [px, py, s] : { std::tuple { 0.15f, 0.2f, 0.16f }, { 0.55f, 0.1f, 0.12f }, { 0.75f, 0.45f, 0.18f },
                                          { 0.3f, 0.55f, 0.2f }, { 0.1f, 0.75f, 0.1f }, { 0.6f, 0.78f, 0.12f } })
                    p.addEllipse (px, py, s, s);
                break;
            case fx::Spectral:
                for (int i = 0; i < 6; ++i)
                {
                    const float hh = 0.2f + 0.7f * std::abs (std::sin ((float) i * 1.3f + 0.4f));
                    p.addRoundedRectangle (0.04f + (float) i * 0.16f, 0.95f - hh, 0.1f, hh, 0.03f);
                }
                break;
            case fx::FreqShift:
                p.startNewSubPath (0.05f, 0.35f); p.lineTo (0.85f, 0.35f);
                p.startNewSubPath (0.7f, 0.18f); p.lineTo (0.88f, 0.35f); p.lineTo (0.7f, 0.52f);
                p.startNewSubPath (0.95f, 0.68f); p.lineTo (0.15f, 0.68f);
                p.startNewSubPath (0.3f, 0.51f); p.lineTo (0.12f, 0.68f); p.lineTo (0.3f, 0.85f);
                break;
            case fx::Reverse:
                p.addCentredArc (0.5f, 0.55f, 0.36f, 0.36f, 0.0f, -2.2f, 2.0f, true);
                p.startNewSubPath (0.05f, 0.25f); p.lineTo (0.2f, 0.42f); p.lineTo (0.38f, 0.26f);
                break;
            default:
                p.addEllipse (0.2f, 0.2f, 0.6f, 0.6f);
                p.startNewSubPath (0.5f, 0.05f); p.lineTo (0.5f, 0.45f);
                break;
        }
        return p;
    }

    // Icons that read as outlines rather than solid shapes
    inline bool iconIsStroke (int effect)
    {
        switch (effect)
        {
            case fx::Melt: case fx::Formant: case fx::Warp: case fx::Robot: case fx::Vinyl: case fx::Ring:
            case fx::Fuzz: case fx::FreqShift: case fx::Reverse: case fx::Off: return true;
            default: return false;
        }
    }

    inline void drawIcon (juce::Graphics& g, int effect, juce::Rectangle<float> area, juce::Colour c)
    {
        auto p = effectIcon (effect);
        const float s = juce::jmin (area.getWidth(), area.getHeight());
        p.applyTransform (juce::AffineTransform::scale (s).translated (area.getCentreX() - s * 0.5f, area.getCentreY() - s * 0.5f));
        g.setColour (c);
        if (iconIsStroke (effect))
            g.strokePath (p, juce::PathStrokeType (juce::jmax (1.4f, s * 0.09f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        else
            g.fillPath (p);
    }
}
