#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

namespace naji::ui
{
    namespace colours
    {
        inline const juce::Colour background  { 0xff111113 };
        inline const juce::Colour panel       { 0xff18181b };
        inline const juce::Colour panelEdge   { 0xff26262b };
        inline const juce::Colour divider     { 0xff222226 };
        inline const juce::Colour track       { 0xff29292e };
        inline const juce::Colour knobTop     { 0xff2c2c31 };
        inline const juce::Colour knobBottom  { 0xff19191c };
        inline const juce::Colour knobEdge    { 0xff38383e };
        inline const juce::Colour text        { 0xffeeeeef };
        inline const juce::Colour textDim     { 0xff8d8d94 };
        inline const juce::Colour textFaint   { 0xff5a5a61 };
        inline const juce::Colour red         { 0xffe0262b };
    }

    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            bold   = juce::Typeface::createSystemTypefaceFor (BinaryData::InterBold_otf,   BinaryData::InterBold_otfSize);
            medium = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_otf, BinaryData::InterMedium_otfSize);
            setColour (juce::ResizableWindow::backgroundColourId, colours::background);
        }

        juce::Font boldFont (float height) const    { return juce::Font (juce::FontOptions (bold).withHeight (height)); }
        juce::Font mediumFont (float height) const  { return juce::Font (juce::FontOptions (medium).withHeight (height)); }

        juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
        {
            return f.isBold() ? bold : medium;
        }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                               float startAngle, float endAngle, juce::Slider& slider) override
        {
            using namespace juce;

            const auto bounds = Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (2.0f);
            const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto  c = bounds.getCentre();

            const float arcR = radius * 0.94f;
            const float arcW = jmax (2.5f, radius * 0.06f);
            const float angle = startAngle + pos * (endAngle - startAngle);

            // Track
            Path track;
            track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
            g.setColour (colours::track);
            g.strokePath (track, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));

            // Value arc (bipolar knobs start from their origin, e.g. 0 dB)
            float originPos = 0.0f;
            if (auto* v = slider.getProperties().getVarPointer ("arcOrigin"))
                originPos = (float) *v;
            const float originAngle = startAngle + originPos * (endAngle - startAngle);

            if (std::abs (angle - originAngle) > 0.001f)
            {
                Path value;
                value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f,
                                     jmin (originAngle, angle), jmax (originAngle, angle), true);
                g.setColour (slider.isEnabled() ? colours::red : colours::textFaint);
                g.strokePath (value, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));
            }

            // Knob body
            const float bodyR = radius * 0.76f;
            const auto body = Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);

            g.setColour (Colours::black.withAlpha (0.45f));
            g.fillEllipse (body.translated (0.0f, radius * 0.04f).expanded (1.0f));

            g.setGradientFill (ColourGradient (colours::knobTop, c.x, body.getY(),
                                               colours::knobBottom, c.x, body.getBottom(), false));
            g.fillEllipse (body);

            g.setColour (colours::knobEdge);
            g.drawEllipse (body.reduced (0.5f), 1.0f);

            // Pointer
            const float pw = jmax (2.0f, radius * 0.045f);
            const auto p1 = c.getPointOnCircumference (bodyR * 0.30f, angle);
            const auto p2 = c.getPointOnCircumference (bodyR * 0.84f, angle);
            g.setColour (slider.isMouseOverOrDragging() ? Colours::white : colours::text);
            Path pointer;
            pointer.startNewSubPath (p1);
            pointer.lineTo (p2);
            g.strokePath (pointer, PathStrokeType (pw, PathStrokeType::curved, PathStrokeType::rounded));
        }

    private:
        juce::Typeface::Ptr bold, medium;
    };
}
