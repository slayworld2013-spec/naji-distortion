#include "Widgets.h"

namespace naji::ui
{
    using namespace juce;

    namespace
    {
        const int categoryOrder[4][4] = {
            { fx::Destroy, fx::Fuzz, fx::Vinyl, 0 },
            { fx::Octave, fx::Warp, fx::Ring, fx::FreqShift },
            { fx::Glitch, fx::Reverse, fx::Granular, fx::Melt },
            { fx::Formant, fx::Robot, fx::Spectral, 0 },
        };

        void setParam (AudioProcessorValueTreeState& s, const String& id, float plain)
        {
            if (auto* p = s.getParameter (id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (plain));
                p->endChangeGesture();
            }
        }
    }

    //==========================================================================
    Knob::Knob (AudioProcessorValueTreeState& state, const String& paramID, const String& l, LookAndFeel& lnf, int size)
        : laf (lnf), label (l), knobSize (size)
    {
        slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (220);
        slider.setVelocityBasedMode (false);
        slider.setMouseCursor (MouseCursor::PointingHandCursor);
        addAndMakeVisible (slider);
        attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);
        if (auto* p = state.getParameter (paramID))
        {
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
            if (paramID == ParamIDs::output || paramID == ParamIDs::width)
                slider.getProperties().set ("arcOrigin", p->getDefaultValue());
        }
        slider.onValueChange = [this] { repaint(); };
        slider.onDragStart = [this] { repaint(); };
    }

    void Knob::resized()
    {
        auto b = getLocalBounds();
        const int s = jmin (knobSize, b.getWidth(), b.getHeight() - 30);
        slider.setBounds (b.removeFromTop (s).withSizeKeepingCentre (s, s));
    }

    void Knob::tick()
    {
        const float target = (float) slider.valueToProportionOfLength (slider.getValue());
        const float next = disp < 0.0f || slider.isMouseButtonDown() ? target : ease (disp, target, 0.22f);
        if (std::abs (next - disp) > 1.0e-4f)
        {
            disp = next;
            slider.getProperties().set ("disp", disp);
            slider.repaint();
        }
    }

    void Knob::paint (Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        b.removeFromTop ((float) slider.getBottom() + 2.0f);
        g.setFont (laf.boldFont (11.0f).withExtraKerningFactor (0.14f));
        g.setColour (colours::text);
        g.drawText (label.toUpperCase(), b.removeFromTop (15.0f), Justification::centred, false);
        g.setFont (laf.mediumFont (11.0f));
        g.setColour (slider.isMouseOverOrDragging() ? colours::accent2 : colours::textDim);
        g.drawText (slider.getTextFromValue (slider.getValue()), b.removeFromTop (14.0f), Justification::centred, false);
    }

    //==========================================================================
    EffectSelector::EffectSelector (NajiDistortionProcessor& p, EditorState& s, LookAndFeel& lnf)
        : proc (p), state (s), laf (lnf)
    {
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    int EffectSelector::slotValue (int s) const
    {
        return roundToInt (proc.apvts.getRawParameterValue (s == 0 ? ParamIDs::slotA : ParamIDs::slotB)->load());
    }

    void EffectSelector::setSlot (int s, int effect)
    {
        setParam (proc.apvts, s == 0 ? ParamIDs::slotA : ParamIDs::slotB, (float) effect);
    }

    void EffectSelector::resized()
    {
        auto b = getLocalBounds().toFloat().reduced (12.0f, 10.0f);
        b.removeFromTop (18.0f);
        for (int s = 0; s < 2; ++s)
        {
            slotPills[(size_t) s] = b.removeFromTop (36.0f);
            b.removeFromTop (5.0f);
        }
        b.removeFromTop (2.0f);
        tiles[0] = b.removeFromTop (24.0f);
        b.removeFromTop (4.0f);
        const float colW = (b.getWidth() - 6.0f) * 0.5f;
        for (int c = 0; c < 4; ++c)
        {
            headers[(size_t) c] = b.removeFromTop (17.0f);
            int count = 0;
            for (int e : categoryOrder[c]) if (e) ++count;
            for (int r = 0; r < (count + 1) / 2; ++r)
            {
                auto row = b.removeFromTop (29.0f);
                for (int col = 0; col < 2; ++col)
                {
                    const int idx = r * 2 + col;
                    if (idx < 4 && categoryOrder[c][idx])
                        tiles[(size_t) categoryOrder[c][idx]] = row.withX (row.getX() + (float) col * (colW + 6.0f)).withWidth (colW);
                }
                b.removeFromTop (4.0f);
            }
        }
        highlight = tiles[(size_t) slotValue (state.focusedSlot)];
    }

    int EffectSelector::effectAt (Point<float> pt) const
    {
        for (int i = 0; i <= fx::numEffects; ++i)
            if (tiles[(size_t) i].contains (pt)) return i;
        return -1;
    }

    void EffectSelector::mouseDown (const MouseEvent& e)
    {
        for (int s = 0; s < 2; ++s)
            if (slotPills[(size_t) s].contains (e.position)) { state.setFocus (s); repaint(); return; }
        const int fxId = effectAt (e.position);
        if (fxId >= 0) setSlot (state.focusedSlot, fxId);
    }

    void EffectSelector::mouseMove (const MouseEvent& e)
    {
        int h = effectAt (e.position);
        for (int s = 0; s < 2; ++s) if (slotPills[(size_t) s].contains (e.position)) h = 100 + s;
        if (h != hovered) { hovered = h; repaint(); }
    }

    void EffectSelector::mouseExit (const MouseEvent&) { hovered = -1; repaint(); }

    void EffectSelector::tick (float lvl)
    {
        time += 1.0f / 60.0f;
        const float oldLevel = level;
        level = ease (level, lvl, 0.3f);
        const auto target = tiles[(size_t) slotValue (state.focusedSlot)];
        const auto old = highlight;
        highlight = { ease (highlight.getX(), target.getX(), 0.25f), ease (highlight.getY(), target.getY(), 0.25f),
                      ease (highlight.getWidth(), target.getWidth(), 0.25f), ease (highlight.getHeight(), target.getHeight(), 0.25f) };
        const int a = slotValue (0), b = slotValue (1);
        if (highlight != old || std::abs (level - oldLevel) > 0.002f || level > 0.01f || a != lastA || b != lastB)
            repaint();
        lastA = a; lastB = b;
    }

    void EffectSelector::paint (Graphics& g)
    {
        drawPanel (g, getLocalBounds().toFloat());
        auto top = getLocalBounds().toFloat().reduced (14.0f, 10.0f).removeFromTop (16.0f);
        g.setFont (laf.boldFont (10.5f).withExtraKerningFactor (0.22f));
        g.setColour (colours::textDim);
        g.drawText ("EFFECT CHAIN", top, Justification::centredLeft);

        // Slot pills
        for (int s = 0; s < 2; ++s)
        {
            const auto r = slotPills[(size_t) s];
            const bool focused = state.focusedSlot == s;
            const int e = slotValue (s);
            if (focused)
            {
                g.setColour (colours::glow.withAlpha (0.25f + 0.25f * level));
                g.fillRoundedRectangle (r.expanded (2.0f), 11.0f);
            }
            g.setGradientFill (ColourGradient (Colours::white, r.getX(), r.getY(), colours::panelGrey, r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (focused ? colours::accent : (hovered == 100 + s ? colours::glow : colours::edge));
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, focused ? 1.6f : 1.0f);

            auto badge = Rectangle<float> (24.0f, 24.0f).withCentre ({ r.getX() + 20.0f, r.getCentreY() });
            g.setGradientFill (ColourGradient (colours::accent, badge.getX(), badge.getY(), colours::accent2, badge.getRight(), badge.getBottom(), false));
            g.fillEllipse (badge);
            g.setColour (Colours::white);
            g.setFont (laf.boldFont (12.0f));
            g.drawText (s == 0 ? "A" : "B", badge, Justification::centred);

            auto text = r.withTrimmedLeft (40.0f).withTrimmedRight (34.0f);
            g.setFont (laf.boldFont (13.0f).withExtraKerningFactor (0.06f));
            g.setColour (e == 0 ? colours::textFaint : colours::text);
            g.drawText (fx::effectName (e), text.withTrimmedBottom (12.0f).translated (0.0f, 2.0f), Justification::centredLeft);
            g.setFont (laf.boldFont (8.5f).withExtraKerningFactor (0.2f));
            g.setColour (colours::textFaint);
            g.drawText (s == 0 ? "BEFORE DRIVE" : "AFTER DRIVE", text.withTrimmedTop (20.0f), Justification::centredLeft);

            const float pulse = 1.0f + (e != 0 ? 0.18f * level : 0.0f);
            drawIcon (g, e, Rectangle<float> (16.0f * pulse, 16.0f * pulse).withCentre ({ r.getRight() - 20.0f, r.getCentreY() }),
                      e == 0 ? colours::textFaint : colours::accent2);
        }

        // Category headers
        for (int c = 0; c < 4; ++c)
        {
            g.setFont (laf.boldFont (9.0f).withExtraKerningFactor (0.24f));
            g.setColour (colours::textFaint);
            g.drawText (fx::categoryName ((fx::Category) c), headers[(size_t) c].withTrimmedBottom (2.0f), Justification::bottomLeft);
        }

        // Animated selection highlight
        g.setColour (colours::glow.withAlpha (0.35f));
        g.fillRoundedRectangle (highlight.expanded (2.5f), 9.0f);
        g.setGradientFill (ColourGradient (colours::accent, highlight.getX(), highlight.getY(), colours::accent2,
                                           highlight.getRight(), highlight.getBottom(), false));
        g.fillRoundedRectangle (highlight, 7.0f);

        const int sel = slotValue (state.focusedSlot), other = slotValue (1 - state.focusedSlot);
        for (int i = 0; i <= fx::numEffects; ++i)
        {
            const auto r = tiles[(size_t) i];
            const bool selected = i == sel;
            const bool hl = selected && highlight.getCentre().getDistanceFrom (r.getCentre()) < 3.0f;
            if (! selected)
            {
                g.setColour (hovered == i ? colours::icy : colours::panelGrey.withAlpha (0.8f));
                g.fillRoundedRectangle (r, 7.0f);
            }
            const Colour fg = hl ? Colours::white : (i == 0 ? colours::textDim : colours::text);
            if (i == 0)
            {
                g.setFont (laf.boldFont (10.0f).withExtraKerningFactor (0.2f));
                g.setColour (fg);
                g.drawText ("SLOT OFF", r, Justification::centred);
                continue;
            }
            const float pulse = selected ? 1.0f + 0.2f * level * (0.6f + 0.4f * std::sin (time * 8.0f)) : 1.0f;
            drawIcon (g, i, Rectangle<float> (14.0f * pulse, 14.0f * pulse).withCentre ({ r.getX() + 15.0f, r.getCentreY() }),
                      hl ? Colours::white : colours::accent2);
            g.setFont (laf.boldFont (10.0f).withExtraKerningFactor (0.06f));
            g.setColour (fg);
            g.drawText (fx::def (i).name, r.withTrimmedLeft (27.0f).withTrimmedRight (12.0f), Justification::centredLeft);
            if (i == other)
            {
                auto badge = Rectangle<float> (13.0f, 13.0f).withCentre ({ r.getRight() - 10.0f, r.getCentreY() });
                g.setColour (hl ? Colours::white.withAlpha (0.9f) : colours::accent.withAlpha (0.25f));
                g.fillEllipse (badge);
                g.setColour (hl ? colours::accent2 : colours::accent2);
                g.setFont (laf.boldFont (8.5f));
                g.drawText (state.focusedSlot == 0 ? "B" : "A", badge, Justification::centred);
            }
        }
    }

    //==========================================================================
    EffectPanel::EffectPanel (NajiDistortionProcessor& p, EditorState& s, LookAndFeel& lnf)
        : proc (p), state (s), laf (lnf)
    {
        for (int e = 1; e <= fx::numEffects; ++e)
            for (int k = 0; k < fx::numParams; ++k)
            {
                auto kn = std::make_unique<Knob> (proc.apvts, fx::paramId (e, k), fx::def (e).p[k].name, laf, 72);
                addChildComponent (*kn);
                knobs.push_back (std::move (kn));
            }
    }

    void EffectPanel::showEffect (int effect, int slot)
    {
        for (int e = 1; e <= fx::numEffects; ++e)
            for (int k = 0; k < fx::numParams; ++k)
                knobs[(size_t) ((e - 1) * fx::numParams + k)]->setVisible (e == effect);
        shownEffect = effect;
        shownSlot = slot;
        fade = 0.0f;
        resized();
    }

    void EffectPanel::resized()
    {
        auto b = getLocalBounds().reduced (6, 0);
        b.removeFromTop (78);
        const int cw = b.getWidth() / 3, ch = b.getHeight() / 2;
        for (int k = 0; k < fx::numParams; ++k)
        {
            Rectangle<int> cell (b.getX() + (k % 3) * cw, b.getY() + (k / 3) * ch, cw, ch);
            const int offset = roundToInt ((1.0f - fade) * 14.0f);
            for (int e = 1; e <= fx::numEffects; ++e)
                knobs[(size_t) ((e - 1) * fx::numParams + k)]->setBounds (cell.withSizeKeepingCentre (96, 112).translated (0, offset));
        }
    }

    void EffectPanel::tick()
    {
        const int slot = state.focusedSlot;
        const int e = roundToInt (proc.apvts.getRawParameterValue (slot == 0 ? ParamIDs::slotA : ParamIDs::slotB)->load());
        if (e != shownEffect || slot != shownSlot) showEffect (e, slot);
        if (fade < 1.0f)
        {
            fade = ease (fade, 1.0f, 0.16f);
            resized();
            for (int k = 0; k < fx::numParams && e > 0; ++k)
                knobs[(size_t) ((e - 1) * fx::numParams + k)]->setAlpha (fade);
            repaint();
        }
        if (e > 0)
            for (int k = 0; k < fx::numParams; ++k) knobs[(size_t) ((e - 1) * fx::numParams + k)]->tick();
    }

    void EffectPanel::paint (Graphics& g)
    {
        auto b = getLocalBounds().toFloat().reduced (16.0f, 4.0f);
        auto head = b.removeFromTop (74.0f);
        const float a = fade;
        const float dy = (1.0f - a) * 8.0f;

        auto chip = Rectangle<float> (head.getX(), head.getY() + dy, 150.0f, 18.0f);
        g.setColour (colours::icy.withAlpha (a));
        g.fillRoundedRectangle (chip, 9.0f);
        g.setFont (laf.boldFont (9.0f).withExtraKerningFactor (0.2f));
        g.setColour (colours::accent2.withAlpha (a));
        g.drawText (shownSlot == 0 ? "SLOT A  BEFORE DRIVE" : "SLOT B  AFTER DRIVE", chip, Justification::centred);

        if (shownEffect <= 0)
        {
            g.setFont (laf.boldFont (22.0f));
            g.setColour (colours::textFaint.withAlpha (a));
            g.drawText ("SLOT OFF", head.withTrimmedTop (24.0f).translated (0, dy).withHeight (28.0f), Justification::centredLeft);
            g.setFont (laf.mediumFont (12.0f));
            g.setColour (colours::textDim.withAlpha (a));
            g.drawFittedText ("Pick an effect on the left to load it into this slot.", b.toNearestInt().withHeight (60), Justification::centredTop, 3);
            return;
        }
        const auto& d = fx::def (shownEffect);
        drawIcon (g, shownEffect, Rectangle<float> (20.0f, 20.0f).withPosition (head.getX(), head.getY() + 26.0f + dy), colours::accent2.withAlpha (a));
        g.setFont (laf.boldFont (22.0f).withExtraKerningFactor (0.05f));
        g.setColour (colours::text.withAlpha (a));
        g.drawText (d.name, Rectangle<float> (head.getX() + 28.0f, head.getY() + 22.0f + dy, head.getWidth() - 28.0f, 28.0f), Justification::centredLeft);
        g.setFont (laf.mediumFont (11.5f));
        g.setColour (colours::textDim.withAlpha (a));
        g.drawFittedText (d.blurb, Rectangle<float> (head.getX(), head.getY() + 50.0f + dy, head.getWidth(), 24.0f).toNearestInt(),
                          Justification::topLeft, 2, 0.95f);
    }

    //==========================================================================
    LabPanel::LabPanel (NajiDistortionProcessor& p, LookAndFeel& lnf) : laf (lnf)
    {
        const std::pair<const char*, const char*> items[] = {
            { ParamIDs::tone, "Tone" }, { ParamIDs::crush, "Crush" }, { ParamIDs::downsample, "Downsample" },
            { ParamIDs::comb, "Comb" }, { ParamIDs::combFreq, "Comb Freq" }, { ParamIDs::alias, "Alias" },
            { ParamIDs::width, "Width" }, { ParamIDs::gate, "Gate" }, { ParamIDs::gateRate, "Gate Rate" } };
        for (auto& [id, name] : items)
        {
            knobs.push_back (std::make_unique<Knob> (p.apvts, id, name, laf, 56));
            addAndMakeVisible (*knobs.back());
        }
    }

    void LabPanel::resized()
    {
        auto b = getLocalBounds().reduced (6, 0);
        b.removeFromTop (46);
        const int cw = b.getWidth() / 3, ch = b.getHeight() / 3;
        for (int k = 0; k < (int) knobs.size(); ++k)
            knobs[(size_t) k]->setBounds (Rectangle<int> (b.getX() + (k % 3) * cw, b.getY() + (k / 3) * ch, cw, ch).withSizeKeepingCentre (92, 96));
    }

    void LabPanel::tick() { for (auto& k : knobs) k->tick(); }

    void LabPanel::paint (Graphics& g)
    {
        auto b = getLocalBounds().toFloat().reduced (16.0f, 4.0f);
        g.setFont (laf.mediumFont (11.5f));
        g.setColour (colours::textDim);
        g.drawFittedText ("Global processing after both slots: crush, downsample, resonant comb, aliasing, stereo width and a tempo-synced gate.",
                          b.removeFromTop (44.0f).toNearestInt(), Justification::topLeft, 3, 1.0f);
    }

    //==========================================================================
    RightPanel::RightPanel (NajiDistortionProcessor& p, EditorState& s, LookAndFeel& lnf)
        : laf (lnf), effect (p, s, lnf), lab (p, lnf)
    {
        addAndMakeVisible (effect);
        addChildComponent (lab);
    }

    void RightPanel::resized()
    {
        auto b = getLocalBounds().reduced (6, 8);
        auto tabs = b.removeFromTop (34).reduced (10, 2).toFloat();
        tabBounds[0] = tabs.removeFromLeft (tabs.getWidth() * 0.5f);
        tabBounds[1] = tabs;
        b.removeFromTop (6);
        effect.setBounds (b);
        lab.setBounds (b);
    }

    void RightPanel::mouseDown (const MouseEvent& e)
    {
        for (int t = 0; t < 2; ++t)
            if (tabBounds[t].contains (e.position)) setTab (t);
    }

    void RightPanel::setTab (int t)
    {
        if (t == tab) return;
        tab = t;
        effect.setVisible (tab == 0);
        lab.setVisible (tab == 1);
        repaint();
    }

    void RightPanel::tick()
    {
        const float target = (float) tab;
        if (std::abs (tabAnim - target) > 1.0e-3f) { tabAnim = ease (tabAnim, target, 0.25f); repaint(); }
        if (tab == 0) effect.tick(); else lab.tick();
    }

    void RightPanel::paint (Graphics& g)
    {
        drawPanel (g, getLocalBounds().toFloat());
        auto track = tabBounds[0].getUnion (tabBounds[1]);
        g.setColour (colours::panelGrey);
        g.fillRoundedRectangle (track, track.getHeight() * 0.5f);
        auto pill = tabBounds[0].translated (tabAnim * tabBounds[0].getWidth(), 0.0f).reduced (3.0f);
        g.setColour (colours::glow.withAlpha (0.3f));
        g.fillRoundedRectangle (pill.expanded (1.5f), pill.getHeight() * 0.5f);
        g.setGradientFill (ColourGradient (colours::accent, pill.getX(), pill.getY(), colours::accent2, pill.getRight(), pill.getBottom(), false));
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        g.setFont (laf.boldFont (11.0f).withExtraKerningFactor (0.2f));
        const char* names[] = { "EFFECT", "LAB" };
        for (int t = 0; t < 2; ++t)
        {
            const float sel = 1.0f - std::abs (tabAnim - (float) t);
            g.setColour (colours::textDim.interpolatedWith (Colours::white, jlimit (0.0f, 1.0f, sel)));
            g.drawText (names[t], tabBounds[t], Justification::centred);
        }
    }

    //==========================================================================
    Segmented::Segmented (RangedAudioParameter& param, StringArray n, LookAndFeel& lnf)
        : laf (lnf), names (std::move (n)),
          attachment (param, [this] (float v) { selected = jlimit (0, names.size() - 1, roundToInt (v)); repaint(); })
    {
        attachment.sendInitialUpdate();
        anim = (float) selected;
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    void Segmented::mouseDown (const MouseEvent& e)
    {
        const int i = jlimit (0, names.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) names.size())));
        if (i != selected) attachment.setValueAsCompleteGesture ((float) i);
    }

    void Segmented::tick()
    {
        if (std::abs (anim - (float) selected) > 1.0e-3f) { anim = ease (anim, (float) selected, 0.25f); repaint(); }
    }

    void Segmented::paint (Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (colours::panelGrey);
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (colours::edge);
        g.drawRoundedRectangle (b.reduced (0.5f), b.getHeight() * 0.5f, 1.0f);
        const float w = b.getWidth() / (float) names.size();
        auto pill = Rectangle<float> (b.getX() + anim * w, b.getY(), w, b.getHeight()).reduced (3.0f);
        g.setGradientFill (ColourGradient (colours::accent, pill.getX(), pill.getY(), colours::accent2, pill.getRight(), pill.getBottom(), false));
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        g.setFont (laf.boldFont (10.0f).withExtraKerningFactor (0.16f));
        for (int i = 0; i < names.size(); ++i)
        {
            const float sel = jlimit (0.0f, 1.0f, 1.0f - std::abs (anim - (float) i));
            g.setColour (colours::textDim.interpolatedWith (Colours::white, sel));
            g.drawText (names[i], Rectangle<float> (b.getX() + (float) i * w, b.getY(), w, b.getHeight()), Justification::centred);
        }
    }

    UnhingedSwitch::UnhingedSwitch (RangedAudioParameter& param, LookAndFeel& lnf)
        : laf (lnf), attachment (param, [this] (float v) { on = v > 0.5f; repaint(); })
    {
        attachment.sendInitialUpdate();
        anim = on ? 1.0f : 0.0f;
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    void UnhingedSwitch::mouseDown (const MouseEvent&) { attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

    void UnhingedSwitch::tick (float lvl)
    {
        const float oa = anim, ol = level;
        anim = ease (anim, on ? 1.0f : 0.0f, 0.25f);
        level = ease (level, lvl, 0.3f);
        if (std::abs (anim - oa) > 1.0e-4f || (on && std::abs (level - ol) > 0.002f)) repaint();
    }

    void UnhingedSwitch::paint (Graphics& g)
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        if (anim > 0.01f)
        {
            g.setColour (colours::glow.withAlpha (anim * (0.35f + 0.4f * level)));
            g.fillRoundedRectangle (b.expanded (2.0f), b.getHeight() * 0.5f + 2.0f);
        }
        g.setGradientFill (ColourGradient (colours::panelGrey.interpolatedWith (colours::accent, anim), b.getX(), b.getY(),
                                           Colours::white.interpolatedWith (colours::accent2, anim), b.getRight(), b.getBottom(), false));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (colours::edge.interpolatedWith (colours::accent2, anim));
        g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.0f);
        // toggle knob
        const float d = b.getHeight() - 8.0f;
        auto knob = Rectangle<float> (d, d).withCentre ({ b.getX() + 4.0f + d * 0.5f + anim * (b.getWidth() - d - 8.0f), b.getCentreY() });
        g.setColour (Colours::white);
        g.fillEllipse (knob);
        g.setColour (colours::edge);
        g.drawEllipse (knob, 1.0f);
        g.setFont (laf.boldFont (10.0f).withExtraKerningFactor (0.18f));
        g.setColour (colours::textDim.interpolatedWith (Colours::white, anim));
        g.drawText ("UNHINGED", b.withTrimmedLeft (on ? 0.0f : d + 4.0f).withTrimmedRight (on ? d + 4.0f : 0.0f), Justification::centred);
    }

    //==========================================================================
    void Meters::tick()
    {
        float v[4] = { proc.inPeak[0].exchange (0.0f), proc.inPeak[1].exchange (0.0f),
                       proc.outPeak[0].exchange (0.0f), proc.outPeak[1].exchange (0.0f) };
        for (int i = 0; i < 4; ++i)
        {
            lv[i] = jmax (v[i], lv[i] * 0.88f);
            if (lv[i] >= hold[i]) { hold[i] = lv[i]; holdTime[i] = 1.0f; }
            else if ((holdTime[i] -= 1.0f / 60.0f) <= 0.0f) hold[i] *= 0.92f;
        }
        autoGain = ease (autoGain, proc.autoGainDb.load(), 0.1f);
        float sum = autoGain;
        for (int i = 0; i < 4; ++i) sum += lv[i] * 1000.0f + hold[i] * 1000.0f;
        if (std::abs (sum - lastSum) > 0.01f) repaint();
        lastSum = sum;
    }

    void Meters::paint (Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        auto text = b.removeFromBottom (16.0f);
        g.setFont (laf.boldFont (9.0f).withExtraKerningFactor (0.18f));
        g.setColour (colours::textFaint);
        g.drawText ("AUTO GAIN  " + String (autoGain, 1) + " dB", text, Justification::centred);
        b.removeFromBottom (4.0f);

        auto toY = [] (float lin) { return jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (lin, -60.0f) + 60.0f) / 60.0f); };
        const char* labels[2] = { "IN", "OUT" };
        auto half = b.getWidth() * 0.5f;
        for (int m = 0; m < 2; ++m)
        {
            auto area = Rectangle<float> (b.getX() + (float) m * half, b.getY(), half, b.getHeight()).reduced (8.0f, 0.0f);
            auto lab = area.removeFromBottom (14.0f);
            g.setColour (m == 0 ? colours::textDim : colours::accent2);
            g.drawText (labels[m], lab, Justification::centred);
            area.removeFromBottom (3.0f);
            const float bw = 7.0f;
            for (int c = 0; c < 2; ++c)
            {
                auto bar = Rectangle<float> (area.getCentreX() - bw - 2.0f + (float) c * (bw + 4.0f), area.getY(), bw, area.getHeight());
                g.setColour (colours::track);
                g.fillRoundedRectangle (bar, 3.5f);
                const int idx = m * 2 + c;
                const float h = bar.getHeight() * toY (lv[idx]);
                auto fill = bar.withTop (bar.getBottom() - h);
                g.setGradientFill (ColourGradient (colours::accent2, 0, bar.getBottom(), colours::glow, 0, bar.getY(), false));
                g.fillRoundedRectangle (fill, 3.5f);
                const float hy = bar.getBottom() - bar.getHeight() * toY (hold[idx]);
                g.setColour (hold[idx] > 0.94f ? colours::hot : colours::accent2);
                g.fillRect (bar.getX(), hy - 1.0f, bw, 2.0f);
            }
        }
    }

    //==========================================================================
    PresetBar::PresetBar (NajiDistortionProcessor& p, LookAndFeel& lnf) : proc (p), laf (lnf)
    {
        browseButton.setButtonText ("BROWSE");
        browseButton.setComponentID ("browseButton");
        for (auto* b : { &browseButton, &prevButton, &nextButton, &saveButton, &loadButton })
        {
            b->setMouseCursor (MouseCursor::PointingHandCursor);
            addAndMakeVisible (*b);
        }
        prevButton.onClick = [this] { proc.presets.previous(); };
        nextButton.onClick = [this] { proc.presets.next(); };
        browseButton.onClick = [this] { if (onBrowse) onBrowse(); };
        saveButton.onClick = [this] { savePreset(); };
        loadButton.onClick = [this] { loadPreset(); };
        proc.presets.addChangeListener (this);
        shownName = proc.presets.getCurrentName();
        shownCategory = proc.presets.getCurrentCategory();
    }

    PresetBar::~PresetBar() { proc.presets.removeChangeListener (this); }

    void PresetBar::changeListenerCallback (ChangeBroadcaster*)
    {
        if (proc.presets.getCurrentName() != shownName)
        {
            oldName = shownName;
            anim = 0.0f;
            flash = 1.0f;
        }
        shownName = proc.presets.getCurrentName();
        shownCategory = proc.presets.getCurrentCategory();
        repaint();
    }

    void PresetBar::resized()
    {
        auto b = getLocalBounds();
        browseButton.setBounds (b.removeFromLeft (78).reduced (0, 6));
        b.removeFromLeft (8);
        loadButton.setBounds (b.removeFromRight (58).reduced (0, 6));
        b.removeFromRight (6);
        saveButton.setBounds (b.removeFromRight (58).reduced (0, 6));
        b.removeFromRight (10);
        prevButton.setBounds (b.removeFromLeft (34).reduced (0, 6));
        nextButton.setBounds (b.removeFromRight (34).reduced (0, 6));
        nameBox = b.reduced (6, 2).toFloat();
    }

    void PresetBar::tick()
    {
        if (anim < 1.0f || flash > 0.0f)
        {
            anim = ease (anim, 1.0f, 0.14f);
            flash = jmax (0.0f, flash - 0.03f);
            repaint();
        }
    }

    void PresetBar::paint (Graphics& g)
    {
        auto r = nameBox;
        if (flash > 0.0f)
        {
            g.setColour (colours::glow.withAlpha (0.5f * flash));
            g.fillRoundedRectangle (r.expanded (3.0f), 14.0f);
        }
        g.setGradientFill (ColourGradient (Colours::white, r.getX(), r.getY(), colours::panelGrey, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (colours::accent.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

        auto inner = r.reduced (16.0f, 4.0f);
        auto chevron = inner.removeFromRight (14.0f);
        Path cp;
        cp.startNewSubPath (chevron.getX() + 2.0f, chevron.getCentreY() - 2.5f);
        cp.lineTo (chevron.getCentreX(), chevron.getCentreY() + 2.5f);
        cp.lineTo (chevron.getRight() - 2.0f, chevron.getCentreY() - 2.5f);
        g.setColour (colours::accent2);
        g.strokePath (cp, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));

        g.setFont (laf.boldFont (8.5f).withExtraKerningFactor (0.22f));
        g.setColour (colours::accent2);
        g.drawText (shownCategory.toUpperCase(), inner.removeFromTop (12.0f), Justification::centredLeft);

        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (inner.toNearestInt());
        g.setFont (laf.boldFont (15.0f));
        if (anim < 0.99f)
        {
            g.setColour (colours::text.withAlpha (1.0f - anim));
            g.drawText (oldName, inner.translated (0.0f, -anim * 14.0f), Justification::centredLeft);
        }
        g.setColour (colours::text.withAlpha (anim));
        g.drawText (shownName, inner.translated (0.0f, (1.0f - anim) * 14.0f), Justification::centredLeft);
    }

    void PresetBar::mouseDown (const MouseEvent& e)
    {
        if (nameBox.contains (e.position)) showMenu();
    }

    void PresetBar::showMenu()
    {
        PopupMenu menu;
        const auto& entries = proc.presets.getEntries();
        StringArray cats;
        for (auto& en : entries) cats.addIfNotAlreadyThere (en.category);
        for (auto& c : cats)
        {
            PopupMenu sub;
            for (int i = 0; i < entries.size(); ++i)
                if (entries.getReference (i).category == c)
                    sub.addItem (i + 1, entries.getReference (i).name, true, i == proc.presets.getCurrentEntry());
            menu.addSubMenu (c, sub);
        }
        menu.addSeparator();
        menu.addItem (10000, "Open preset browser...");
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (nameBox.toNearestInt())),
                            [this] (int r)
                            {
                                if (r == 10000) { if (onBrowse) onBrowse(); }
                                else if (r > 0) proc.presets.loadEntry (r - 1);
                            });
    }

    void PresetBar::savePreset()
    {
        auto folder = PresetManager::userFolder();
        folder.createDirectory();
        chooser = std::make_unique<FileChooser> ("Save NAJI preset", folder.getChildFile (proc.presets.getCurrentName() + ".najipreset"), "*.najipreset");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting,
                              [this] (const FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == File()) return;
                                  f = f.withFileExtension ("najipreset");
                                  proc.presets.saveToFile (f, f.getFileNameWithoutExtension(), "User");
                              });
    }

    void PresetBar::loadPreset()
    {
        chooser = std::make_unique<FileChooser> ("Load NAJI preset", PresetManager::userFolder(), "*.najipreset");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f.existsAsFile()) proc.presets.loadFile (f);
                              });
    }

    //==========================================================================
    PresetBrowser::PresetBrowser (NajiDistortionProcessor& p, LookAndFeel& lnf) : proc (p), laf (lnf)
    {
        search.setTextToShowWhenEmpty ("Search presets, categories, effects...", colours::textFaint);
        search.setFont (laf.mediumFont (14.0f));
        search.setIndents (12, 7);
        search.onTextChange = [this] { refilter(); };
        search.onEscapeKey = [this] { close(); };
        addAndMakeVisible (search);
        closeButton.onClick = [this] { close(); };
        addAndMakeVisible (closeButton);
        proc.presets.addChangeListener (this);
        setWantsKeyboardFocus (true);
        setVisible (false);
        refilter();
    }

    PresetBrowser::~PresetBrowser() { proc.presets.removeChangeListener (this); }

    void PresetBrowser::changeListenerCallback (ChangeBroadcaster*) { repaint(); }

    juce::String PresetBrowser::chainFor (int factoryIndex) const
    {
        if (factoryIndex < 0) return "USER PRESET";
        int a = 0, b = 0;
        for (auto& [id, v] : factoryPresets()[(size_t) factoryIndex].values)
        {
            if (String (id) == "slotA") a = (int) v;
            if (String (id) == "slotB") b = (int) v;
        }
        return fx::effectName (a) + "  >  " + fx::effectName (b);
    }

    void PresetBrowser::refilter()
    {
        proc.presets.rescan();
        const auto& entries = proc.presets.getEntries();
        categories = { "All" };
        for (auto& e : entries) categories.addIfNotAlreadyThere (e.category);
        filtered.clear();
        const auto q = search.getText().trim().toLowerCase();
        for (int i = 0; i < entries.size(); ++i)
        {
            const auto& e = entries.getReference (i);
            if (category != "All" && e.category != category) continue;
            const auto hay = (e.name + " " + e.category + " " + e.description + " " + chainFor (e.factoryIndex)).toLowerCase();
            if (q.isEmpty() || hay.contains (q)) filtered.add (i);
        }
        scrollTarget = 0.0f;
        resized();
        repaint();
    }

    void PresetBrowser::open()
    {
        refilter();
        target = 1.0f;
        setVisible (true);
        toFront (true);
        search.grabKeyboardFocus();
    }

    void PresetBrowser::close() { target = 0.0f; }

    void PresetBrowser::tick()
    {
        if (std::abs (anim - target) > 1.0e-3f)
        {
            anim = ease (anim, target, 0.2f);
            setAlpha (anim);
            setTransform (AffineTransform::translation (0.0f, (1.0f - anim) * -18.0f));
            if (anim < 0.01f && target < 0.5f) setVisible (false);
        }
        if (std::abs (scroll - scrollTarget) > 0.1f) { scroll = ease (scroll, scrollTarget, 0.3f); repaint(); }
    }

    void PresetBrowser::resized()
    {
        auto b = getLocalBounds().reduced (20, 16);
        auto top = b.removeFromTop (34);
        closeButton.setBounds (top.removeFromRight (34));
        top.removeFromRight (10);
        top.removeFromLeft (150);
        search.setBounds (top);
        b.removeFromTop (12);
        auto chips = b.removeFromTop (26).toFloat();
        chipBounds.clear();
        float x = chips.getX();
        for (auto& c : categories)
        {
            const float w = GlyphArrangement::getStringWidth (laf.boldFont (9.5f).withExtraKerningFactor (0.1f), c.toUpperCase()) + 18.0f;
            chipBounds.push_back ({ x, chips.getY(), w, chips.getHeight() });
            x += w + 5.0f;
        }
        b.removeFromTop (10);
        listArea = b.toFloat();
    }

    int PresetBrowser::rowAt (Point<float> pt) const
    {
        if (! listArea.contains (pt)) return -1;
        const int r = (int) ((pt.y - listArea.getY() + scroll) / rowH);
        return isPositiveAndBelow (r, filtered.size()) ? r : -1;
    }

    void PresetBrowser::mouseDown (const MouseEvent& e)
    {
        for (size_t i = 0; i < chipBounds.size(); ++i)
            if (chipBounds[i].contains (e.position)) { category = categories[(int) i]; refilter(); return; }
        const int r = rowAt (e.position);
        if (r >= 0) proc.presets.loadEntry (filtered[r]);
        else if (! listArea.contains (e.position) && e.position.y > listArea.getBottom()) close();
    }

    void PresetBrowser::mouseDoubleClick (const MouseEvent& e)
    {
        if (rowAt (e.position) >= 0) close();
    }

    void PresetBrowser::mouseMove (const MouseEvent& e)
    {
        const int r = rowAt (e.position);
        if (r != hovered) { hovered = r; repaint(); }
    }

    void PresetBrowser::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
    {
        const float maxScroll = jmax (0.0f, (float) filtered.size() * rowH - listArea.getHeight());
        scrollTarget = jlimit (0.0f, maxScroll, scrollTarget - w.deltaY * 240.0f);
    }

    bool PresetBrowser::keyPressed (const KeyPress& k)
    {
        if (k == KeyPress::escapeKey) { close(); return true; }
        return false;
    }

    void PresetBrowser::paint (Graphics& g)
    {
        auto b = getLocalBounds().toFloat();
        for (int i = 6; i >= 1; --i)
        {
            g.setColour (Colour (0xff1c3a5e).withAlpha (0.03f * (float) i));
            g.fillRoundedRectangle (b.reduced (6.0f - (float) i).translated (0.0f, (float) i * 2.0f), 18.0f);
        }
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.98f), 0, 0, Colour (0xfff2f6fb).withAlpha (0.98f), 0, b.getHeight(), false));
        g.fillRoundedRectangle (b.reduced (6.0f), 16.0f);
        g.setColour (colours::accent.withAlpha (0.5f));
        g.drawRoundedRectangle (b.reduced (6.5f), 16.0f, 1.0f);

        auto head = getLocalBounds().reduced (20, 16).removeFromTop (34).toFloat();
        g.setFont (laf.boldFont (16.0f).withExtraKerningFactor (0.1f));
        g.setColour (colours::text);
        g.drawText ("PRESETS", head.removeFromLeft (140.0f), Justification::centredLeft);

        g.setFont (laf.boldFont (9.5f).withExtraKerningFactor (0.1f));
        for (size_t i = 0; i < chipBounds.size(); ++i)
        {
            const bool sel = categories[(int) i] == category;
            const auto r = chipBounds[i];
            if (sel)
            {
                g.setGradientFill (ColourGradient (colours::accent, r.getX(), r.getY(), colours::accent2, r.getRight(), r.getBottom(), false));
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            }
            else
            {
                g.setColour (colours::panelGrey);
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            }
            g.setColour (sel ? Colours::white : colours::textDim);
            g.drawText (categories[(int) i].toUpperCase(), r, Justification::centred);
        }

        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (listArea.toNearestInt());
        const auto& entries = proc.presets.getEntries();
        for (int r = 0; r < filtered.size(); ++r)
        {
            auto row = Rectangle<float> (listArea.getX(), listArea.getY() + (float) r * rowH - scroll, listArea.getWidth(), rowH - 4.0f);
            if (row.getBottom() < listArea.getY() || row.getY() > listArea.getBottom()) continue;
            const auto& e = entries.getReference (filtered[r]);
            const bool current = filtered[r] == proc.presets.getCurrentEntry();
            if (current)
            {
                g.setColour (colours::icy);
                g.fillRoundedRectangle (row, 10.0f);
                g.setColour (colours::accent);
                g.drawRoundedRectangle (row.reduced (0.5f), 10.0f, 1.2f);
            }
            else if (r == hovered)
            {
                g.setColour (colours::panelGrey);
                g.fillRoundedRectangle (row, 10.0f);
            }
            auto inner = row.reduced (14.0f, 5.0f);
            auto num = inner.removeFromLeft (28.0f);
            g.setFont (laf.boldFont (11.0f));
            g.setColour (colours::textFaint);
            g.drawText (e.factoryIndex >= 0 ? String (e.factoryIndex + 1).paddedLeft ('0', 2) : String ("U"), num, Justification::centredLeft);
            auto tag = inner.removeFromRight (104.0f).withSizeKeepingCentre (100.0f, 18.0f);
            g.setColour (colours::accent.withAlpha (0.15f));
            g.fillRoundedRectangle (tag, 9.0f);
            g.setFont (laf.boldFont (9.0f).withExtraKerningFactor (0.15f));
            g.setColour (colours::accent2);
            g.drawText (e.category.toUpperCase(), tag, Justification::centred);
            auto chain = inner.removeFromRight (190.0f);
            g.setFont (laf.boldFont (9.5f).withExtraKerningFactor (0.1f));
            g.setColour (colours::textDim);
            g.drawText (chainFor (e.factoryIndex), chain, Justification::centredLeft);
            g.setFont (laf.boldFont (14.0f));
            g.setColour (colours::text);
            g.drawText (e.name, inner.removeFromTop (19.0f), Justification::centredLeft);
            g.setFont (laf.mediumFont (11.0f));
            g.setColour (colours::textDim);
            g.drawText (e.description, inner, Justification::centredLeft);
        }
        if (filtered.isEmpty())
        {
            g.setFont (laf.mediumFont (13.0f));
            g.setColour (colours::textDim);
            g.drawText ("No presets match your search.", listArea, Justification::centred);
        }
    }
}
