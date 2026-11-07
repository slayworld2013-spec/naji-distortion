#include "PluginEditor.h"

using namespace naji::ui;

namespace
{
    constexpr int editorW = 920, editorH = 540;

    float textWidth (const juce::Font& f, const juce::String& s)
    {
        return juce::GlyphArrangement::getStringWidth (f, s);
    }

    inline uint32_t hash32 (uint32_t x) noexcept
    {
        x ^= x >> 16; x *= 0x7feb352dU;
        x ^= x >> 15; x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }

    // Which photo each mode shows.
    constexpr int modePhoto[8] = { 0, 2, 1, 3, 0, 2, 1, 4 };
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
            const juce::String& labelText, LookAndFeel& lnf, bool big)
    : laf (lnf), label (labelText), isBig (big)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (big ? 320 : 260);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);

    if (auto* p = state.getParameter (paramID))
    {
        const float def = p->getDefaultValue();
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (def));
        if (def > 0.001f && def < 0.999f && paramID == ParamIDs::output)
            slider.getProperties().set ("arcOrigin", def);   // output arc grows from 0 dB
    }

    slider.onValueChange = [this] { repaint(); };
}

void Knob::resized()
{
    auto b = getLocalBounds();
    const int labelH = isBig ? 46 : 40;
    auto knobArea = b.withTrimmedBottom (labelH);
    const int size = juce::jmin (knobArea.getWidth(), knobArea.getHeight());
    slider.setBounds (knobArea.withSizeKeepingCentre (size, size));
}

void Knob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float labelH = isBig ? 46.0f : 40.0f;
    auto text = b.removeFromBottom (labelH);

    g.setColour (colours::text);
    g.setFont (laf.boldFont (isBig ? 15.0f : 12.5f).withExtraKerningFactor (0.14f));
    g.drawText (label, text.removeFromTop (labelH * 0.5f), juce::Justification::centredBottom, false);

    g.setColour (colours::textDim);
    g.setFont (laf.mediumFont (isBig ? 13.0f : 11.5f));
    g.drawText (slider.getTextFromValue (slider.getValue()), text, juce::Justification::centred, false);
}

//==============================================================================
ModeSelector::ModeSelector (juce::RangedAudioParameter& param, LookAndFeel& lnf)
    : laf (lnf),
      attachment (param, [this] (float v) { selected = juce::jlimit (0, names.size() - 1, juce::roundToInt (v)); repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> ModeSelector::segmentBounds (int index) const
{
    constexpr int cols = 4;
    const float w = (float) getWidth() / (float) cols;
    const float h = (float) getHeight() * 0.5f;
    return { w * (float) (index % cols), h * (float) (index / cols), w, h };
}

int ModeSelector::segmentAt (juce::Point<float> p) const
{
    for (int i = 0; i < names.size(); ++i)
        if (segmentBounds (i).contains (p))
            return i;
    return -1;
}

void ModeSelector::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const float corner = 4.0f;

    g.setColour (colours::panel);
    g.fillRoundedRectangle (b, corner);

    for (int i = 0; i < names.size(); ++i)
    {
        auto seg = segmentBounds (i).reduced (3.0f);
        const bool extreme = i >= 4;

        if (i == selected)
        {
            g.setColour (extreme ? colours::hot : colours::red);
            g.fillRoundedRectangle (seg, corner - 1.0f);
        }
        else if (i == hovered)
        {
            g.setColour (colours::track);
            g.fillRoundedRectangle (seg, corner - 1.0f);
        }

        g.setColour (i == selected ? juce::Colours::white
                                   : (i == hovered ? colours::text
                                                   : (extreme ? colours::red.withAlpha (0.8f) : colours::textDim)));
        g.setFont (laf.boldFont (11.5f).withExtraKerningFactor (0.12f));
        g.drawText (names[i], seg, juce::Justification::centred, false);
    }

    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (b, corner, 1.0f);
}

void ModeSelector::mouseDown (const juce::MouseEvent& e)
{
    const int i = segmentAt (e.position);
    if (i >= 0 && i != selected)
        attachment.setValueAsCompleteGesture ((float) i);
}

void ModeSelector::mouseMove (const juce::MouseEvent& e)
{
    const int i = segmentAt (e.position);
    if (i != hovered) { hovered = i; repaint(); }
}

void ModeSelector::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

//==============================================================================
UnhingedButton::UnhingedButton (juce::AudioProcessorValueTreeState& state, LookAndFeel& lnf)
    : laf (lnf)
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, ParamIDs::unhinged, *this);
}

void UnhingedButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto b = getLocalBounds().toFloat();
    auto pill = b.removeFromTop (b.getHeight() * 0.62f).reduced (4.0f);
    const bool on = getToggleState();

    if (on)
    {
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (colours::hot.withAlpha (0.10f * (float) (4 - i)));
            g.fillRoundedRectangle (pill.expanded ((float) i * 3.0f), 8.0f + (float) i * 3.0f);
        }
        g.setGradientFill (juce::ColourGradient (colours::hot, pill.getX(), pill.getY(),
                                                 colours::red.darker (0.3f), pill.getX(), pill.getBottom(), false));
        g.fillRoundedRectangle (pill, 8.0f);
    }
    else
    {
        g.setColour (highlighted ? colours::track : colours::panel);
        g.fillRoundedRectangle (pill, 8.0f);
        g.setColour (colours::red.withAlpha (highlighted ? 1.0f : 0.7f));
        g.drawRoundedRectangle (pill.reduced (0.5f), 8.0f, 1.5f);
    }

    g.setColour (on ? juce::Colours::white : colours::red);
    g.setFont (laf.boldFont (14.0f).withExtraKerningFactor (0.16f));
    g.drawText ("UNHINGED", pill, juce::Justification::centred, false);

    g.setColour (colours::textDim);
    g.setFont (laf.mediumFont (11.5f));
    g.drawText (on ? "+30 dB  ON" : "OFF", b, juce::Justification::centred, false);
}

//==============================================================================
PhotoPanel::PhotoPanel (NajiDistortionProcessor& p, LookAndFeel& lnf)
    : proc (p), laf (lnf)
{
    const void* data[numPhotos] = { BinaryData::photo1_jpg, BinaryData::photo2_jpg, BinaryData::photo3_jpg,
                                    BinaryData::photo4_jpg, BinaryData::photo5_jpg };
    const int sizes[numPhotos]  = { BinaryData::photo1_jpgSize, BinaryData::photo2_jpgSize, BinaryData::photo3_jpgSize,
                                    BinaryData::photo4_jpgSize, BinaryData::photo5_jpgSize };
    for (int i = 0; i < numPhotos; ++i)
        originals[(size_t) i] = juce::ImageFileFormat::loadFrom (data[i], (size_t) sizes[i]);

    setInterceptsMouseClicks (false, false);
    pollParameters();
    startTimerHz (30);
}

void PhotoPanel::resized()
{
    rw = juce::jmax (1, getWidth() * 2);
    rh = juce::jmax (1, getHeight() * 2);

    for (int i = 0; i < numPhotos; ++i)
    {
        auto& dst = scaled[(size_t) i];
        dst.assign ((size_t) rw * (size_t) rh, 0x101012u);
        const auto& img = originals[(size_t) i];
        if (! img.isValid()) continue;

        // Cover-crop, keeping more of the top (faces) than the bottom.
        const float s = juce::jmax ((float) rw / (float) img.getWidth(), (float) rh / (float) img.getHeight());
        const float dw = (float) img.getWidth() * s, dh = (float) img.getHeight() * s;
        const float dx = ((float) rw - dw) * 0.5f, dy = ((float) rh - dh) * 0.3f;

        juce::Image tmp (juce::Image::ARGB, rw, rh, true);
        {
            juce::Graphics g (tmp);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (img, juce::Rectangle<float> (dx, dy, dw, dh));
        }
        juce::Image::BitmapData bd (tmp, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < rh; ++y)
            for (int x = 0; x < rw; ++x)
            {
                const auto c = bd.getPixelColour (x, y);
                dst[(size_t) y * (size_t) rw + (size_t) x] = ((uint32_t) c.getRed() << 16)
                                                          | ((uint32_t) c.getGreen() << 8) | c.getBlue();
            }
    }
    dirty = true;
}

void PhotoPanel::showPhoto (int index)
{
    index = juce::jlimit (0, numPhotos - 1, index);
    if (index == current) return;
    previous = current;
    current = index;
    transition = 0.0f;
}

void PhotoPanel::pollParameters()
{
    auto& ap = proc.apvts;
    auto v = [&ap] (const char* id) { return ap.getRawParameterValue (id)->load(); };
    const Snapshot now { v (ParamIDs::drive), v (ParamIDs::tone), v (ParamIDs::crush), v (ParamIDs::alias),
                         v (ParamIDs::mix), v (ParamIDs::output), v (ParamIDs::type), v (ParamIDs::unhinged) };

    if (! havePoll)
    {
        havePoll = true;
        last = now;
        current = previous = modePhoto[juce::jlimit (0, 7, juce::roundToInt (now.type))];
        unhingedVis = now.unhinged > 0.5f ? 1.0f : 0.0f;
        return;
    }

    auto moved = [] (float a, float b) { return std::abs (a - b) > 1.0e-4f; };
    int photo = -1;
    float jolt = 0.0f;

    if (moved (now.drive,  last.drive))  { photo = 2; jolt += std::abs (now.drive - last.drive) * 0.06f; }
    if (moved (now.tone,   last.tone))   { photo = 0; jolt += std::abs (now.tone - last.tone) * 0.04f; }
    if (moved (now.crush,  last.crush))  { photo = 1; jolt += std::abs (now.crush - last.crush) * 0.05f; }
    if (moved (now.alias,  last.alias))  { photo = 3; jolt += std::abs (now.alias - last.alias) * 0.05f; }
    if (moved (now.mix,    last.mix))    { photo = 1; jolt += std::abs (now.mix - last.mix) * 0.03f; }
    if (moved (now.output, last.output)) { photo = 0; jolt += std::abs (now.output - last.output) * 0.08f; }
    if (moved (now.type,   last.type))   { photo = modePhoto[juce::jlimit (0, 7, juce::roundToInt (now.type))]; jolt += 0.9f; }
    if (moved (now.unhinged, last.unhinged)) { photo = now.unhinged > 0.5f ? 4 : 2; jolt += 1.0f; }

    if (photo >= 0)
    {
        showPhoto (photo);
        kick = juce::jmin (1.0f, kick + 0.25f + jolt);
        dirty = true;
    }
    last = now;
}

void PhotoPanel::timerCallback()
{
    pollParameters();

    const bool wasActive = kick > 0.01f || transition < 1.0f;

    kick *= 0.84f;
    if (transition < 1.0f) transition = juce::jmin (1.0f, transition + 1.0f / 9.0f);

    const float unTarget = last.unhinged > 0.5f ? 1.0f : 0.0f;
    unhingedVis += (unTarget - unhingedVis) * 0.25f;

    const float lv = proc.outputLevel.load (std::memory_order_relaxed);
    const bool reacting = lv > 0.02f && (last.drive > 25.0f || last.alias > 1.0f || last.crush > 1.0f
                                         || last.unhinged > 0.5f || last.type > 3.5f);
    const bool active = kick > 0.01f || transition < 1.0f || reacting || std::abs (unhingedVis - unTarget) > 0.01f;

    if (active || wasActive || dirty)
    {
        ++frameCounter;
        dirty = true;
        repaint();
    }
}

void PhotoPanel::renderFrame()
{
    using namespace juce;
    if (rw <= 0 || rh <= 0 || scaled[0].empty()) return;
    if (! frame.isValid() || frame.getWidth() != rw || frame.getHeight() != rh)
        frame = Image (Image::ARGB, rw, rh, false);

    const float d  = last.drive * 0.01f;
    const float cr = last.crush * 0.01f;
    const float al = last.alias * 0.01f;
    const float tn = last.tone * 0.01f;
    const bool  nukeMode = juce::roundToInt (last.type) == 7;
    const float lv = jlimit (0.0f, 1.0f, proc.outputLevel.load (std::memory_order_relaxed) * 1.5f);
    const float un = jmax (unhingedVis, nukeMode ? 0.5f : 0.0f);
    const float k  = kick;

    Random rng ((int64) frameCounter * 7919 + 17);
    const float energy = jlimit (0.0f, 2.0f, d * d * (0.1f + lv) + un * (0.25f + lv) + k * 0.9f);
    const float zoom = 1.0f + 0.10f * d + 0.06f * un + 0.04f * energy;
    const float sc = (float) rw / 240.0f;
    const float shakeAmp = 8.0f * sc * energy;
    const int shakeX = roundToInt ((rng.nextFloat() * 2.0f - 1.0f) * shakeAmp);
    const int shakeY = roundToInt ((rng.nextFloat() * 2.0f - 1.0f) * shakeAmp * 0.6f);
    const int blk = jmax (1, roundToInt (1.0f + std::pow (cr, 1.4f) * 16.0f * sc));
    const int split = roundToInt ((al * 9.0f + un * 4.0f + k * 3.0f) * sc);
    const float glitchP = jlimit (0.0f, 0.9f, 0.18f * al + 0.10f * cr + 0.22f * un * (0.4f + lv) + 0.35f * k);
    const int bandH = jmax (2, roundToInt ((5.0f + rng.nextFloat() * 18.0f) * sc));
    const bool invert = un > 0.5f && rng.nextFloat() < 0.04f + 0.10f * lv;
    const bool flashNuke = un > 0.5f && rng.nextFloat() < 0.05f * (0.3f + lv);

    // Colour lookup: contrast, red push, warm/cool tone tint, posterise, invert.
    const float contrast = 1.0f + 0.7f * d + 0.4f * un;
    const int sat256 = roundToInt (256.0f * (1.0f + 0.6f * d + 0.5f * un));
    const int levels = cr > 0.001f ? jmax (3, roundToInt (256.0f - std::pow (cr, 0.7f) * 252.0f)) : 256;
    const float tint = (tn - 0.5f) * 0.12f;
    uint8 lut[3][256];
    for (int c = 0; c < 3; ++c)
        for (int v = 0; v < 256; ++v)
        {
            float f = ((float) v / 255.0f - 0.5f) * contrast + 0.5f;
            if (c == 0) f += 0.16f * d * d + 0.20f * un - tint;
            else        f -= 0.05f * d * d + 0.08f * un;
            if (c == 2) f += tint;
            f = jlimit (0.0f, 1.0f, f);
            if (levels < 256) f = std::round (f * (float) (levels - 1)) / (float) (levels - 1);
            if (invert) f = 1.0f - f;
            lut[c][v] = (uint8) roundToInt (f * 255.0f);
        }

    std::vector<int> xmap ((size_t) rw);
    for (int x = 0; x < rw; ++x)
    {
        const int xq = (x / blk) * blk + blk / 2;
        xmap[(size_t) x] = (int) std::floor (((float) xq - (float) rw * 0.5f) / zoom + (float) rw * 0.5f) + shakeX;
    }

    const auto& cur = scaled[(size_t) (flashNuke ? 4 : current)];
    const auto& old = scaled[(size_t) previous];
    const uint32_t fc = frameCounter;

    Image::BitmapData bd (frame, Image::BitmapData::writeOnly);
    for (int y = 0; y < rh; ++y)
    {
        const uint32_t band = (uint32_t) (y / bandH);
        const uint32_t h = hash32 (band * 2654435761U ^ (fc * 40503U + 7U));
        const float r1 = (float) (h & 0xffffU) / 65535.0f;
        const float r2 = (float) ((h >> 16) & 0xffffU) / 65535.0f;

        int off = r1 < glitchP ? roundToInt ((r2 * 2.0f - 1.0f) * (float) rw * 0.18f) : 0;
        const std::vector<uint32_t>* src = &cur;
        if (transition < 1.0f)
        {
            const uint32_t h2 = hash32 (band * 2246822519U + 99U);
            if ((float) (h2 & 0xffffU) / 65535.0f > transition) src = &old;
            off += roundToInt ((r2 * 2.0f - 1.0f) * (float) rw * 0.25f * (1.0f - transition));
        }

        const int yq = (y / blk) * blk + blk / 2;
        const int sy = jlimit (0, rh - 1, (int) std::floor (((float) yq - (float) rh * 0.5f) / zoom + (float) rh * 0.5f) + shakeY);
        const uint32_t* row = src->data() + (size_t) sy * (size_t) rw;
        uint8* dst = bd.getLinePointer (y);

        for (int x = 0; x < rw; ++x)
        {
            const int sx = xmap[(size_t) x] + off;
            const int sr = jlimit (0, rw - 1, sx + split);
            const int sg = jlimit (0, rw - 1, sx);
            const int sb = jlimit (0, rw - 1, sx - split);
            int r = (int) ((row[sr] >> 16) & 255U);
            int g = (int) ((row[sg] >> 8) & 255U);
            int b = (int) (row[sb] & 255U);
            if (sat256 != 256)
            {
                const int l = (r * 77 + g * 150 + b * 29) >> 8;
                r = jlimit (0, 255, l + (((r - l) * sat256) >> 8));
                g = jlimit (0, 255, l + (((g - l) * sat256) >> 8));
                b = jlimit (0, 255, l + (((b - l) * sat256) >> 8));
            }
            reinterpret_cast<PixelARGB*> (dst + x * bd.pixelStride)->setARGB (255, lut[0][r], lut[1][g], lut[2][b]);
        }
    }
}

void PhotoPanel::paint (juce::Graphics& g)
{
    if (dirty) { renderFrame(); dirty = false; }

    const auto panel = getLocalBounds().toFloat();
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (panel, 8.0f);
        g.reduceClipRegion (clip);

        g.setColour (colours::panel);
        g.fillRect (panel);
        if (frame.isValid())
            g.drawImage (frame, panel, juce::RectanglePlacement::stretchToFit);

        // Bottom fade + mode caption
        auto cap = panel.withTop (panel.getBottom() - 70.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), 0.0f, cap.getY(),
                                                 juce::Colours::black.withAlpha (0.75f), 0.0f, cap.getBottom(), false));
        g.fillRect (cap);

        static const char* modeNames[] = { "SOFT", "HARD", "FUZZ", "DIGITAL", "OCTAVE", "MELT", "GLITCH", "NUKE" };
        const int m = juce::jlimit (0, 7, juce::roundToInt (last.type));
        auto text = cap.reduced (14.0f, 10.0f);
        g.setFont (laf.boldFont (22.0f).withExtraKerningFactor (0.10f));
        g.setColour (m >= 4 ? colours::hot : juce::Colours::white);
        g.drawText (modeNames[m], text, juce::Justification::bottomLeft, false);

        if (last.unhinged > 0.5f)
        {
            g.setFont (laf.boldFont (11.0f).withExtraKerningFactor (0.18f));
            g.setColour (colours::hot);
            g.drawText ("UNHINGED", text, juce::Justification::bottomRight, false);
        }
    }

    g.setColour (last.unhinged > 0.5f ? colours::hot.withAlpha (0.8f) : colours::panelEdge);
    g.drawRoundedRectangle (panel.reduced (0.5f), 8.0f, last.unhinged > 0.5f ? 2.0f : 1.0f);
}

//==============================================================================
NajiDistortionEditor::NajiDistortionEditor (NajiDistortionProcessor& p)
    : AudioProcessorEditor (p),
      proc (p),
      modeSelector (*p.apvts.getParameter (ParamIDs::type), laf),
      photo  (p, laf),
      drive  (p.apvts, ParamIDs::drive,  "DRIVE",  laf, true),
      tone   (p.apvts, ParamIDs::tone,   "TONE",   laf, false),
      crush  (p.apvts, ParamIDs::crush,  "CRUSH",  laf, false),
      alias  (p.apvts, ParamIDs::alias,  "ALIAS",  laf, false),
      mix    (p.apvts, ParamIDs::mix,    "MIX",    laf, false),
      output (p.apvts, ParamIDs::output, "OUTPUT", laf, false),
      unhinged (p.apvts, laf)
{
    setLookAndFeel (&laf);

    for (auto* c : std::initializer_list<juce::Component*> { &modeSelector, &photo, &drive, &tone, &crush,
                                                             &alias, &mix, &output, &unhinged })
        addAndMakeVisible (c);

    setSize (editorW, editorH);
    startTimerHz (10);
}

NajiDistortionEditor::~NajiDistortionEditor()
{
    setLookAndFeel (nullptr);
}

void NajiDistortionEditor::timerCallback()
{
    const float ag = proc.autoGainDb.load (std::memory_order_relaxed);
    if (std::abs (ag - shownAutoGain) > 0.05f)
    {
        shownAutoGain = ag;
        repaint (footerArea);
    }
}

void NajiDistortionEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Header
    {
        const auto titleFont = laf.boldFont (30.0f).withExtraKerningFactor (0.08f);
        const juce::String first = "NAJI ", second = "DISTORTION";
        const float x = 28.0f, y = 18.0f, h = 40.0f;
        const float w1 = textWidth (titleFont, first);

        g.setFont (titleFont);
        g.setColour (colours::text);
        g.drawText (first, juce::Rectangle<float> (x, y, w1 + 4.0f, h), juce::Justification::centredLeft, false);
        g.setColour (colours::red);
        g.drawText (second, juce::Rectangle<float> (x + w1, y, 400.0f, h), juce::Justification::centredLeft, false);

        g.setFont (laf.mediumFont (11.0f).withExtraKerningFactor (0.14f));
        g.setColour (colours::textFaint);
        g.drawText ("AUTO-GAIN DISTORTION", juce::Rectangle<float> (x + 2.0f, y + h - 2.0f, 300.0f, 16.0f),
                    juce::Justification::centredLeft, false);

        g.setColour (colours::divider);
        g.fillRect (0, 92, getWidth(), 1);
    }

    // Footer
    {
        g.setColour (colours::divider);
        g.fillRect (0, getHeight() - 34, getWidth(), 1);

        g.setFont (laf.mediumFont (11.0f).withExtraKerningFactor (0.1f));
        g.setColour (colours::textFaint);
        auto footer = getLocalBounds().removeFromBottom (33).reduced (28, 0);
        g.drawText ("v" JucePlugin_VersionString, footer, juce::Justification::centredLeft, false);

        const juce::String agText = juce::String::formatted ("AUTO GAIN %+.1f dB", (double) shownAutoGain)
                                    + juce::String (juce::CharPointer_UTF8 ("   \xc2\xb7   OVERSAMPLED"));
        g.drawText (agText, footer, juce::Justification::centredRight, false);
    }
}

void NajiDistortionEditor::resized()
{
    modeSelector.setBounds (getWidth() - 28 - 440, 16, 440, 62);
    footerArea = getLocalBounds().removeFromBottom (33);

    auto body = getLocalBounds().withTrimmedTop (93).withTrimmedBottom (35).reduced (24, 16);

    photo.setBounds (body.removeFromLeft (240));
    body.removeFromLeft (16);

    auto driveArea = body.removeFromLeft (228);
    drive.setBounds (driveArea.withSizeKeepingCentre (200, 252));

    body.removeFromLeft (8);
    auto top = body.removeFromTop (body.getHeight() / 2);
    auto bottom = body;
    const int colW = top.getWidth() / 3;

    tone.setBounds  (top.removeFromLeft (colW).withSizeKeepingCentre (112, 140));
    crush.setBounds (top.removeFromLeft (colW).withSizeKeepingCentre (112, 140));
    alias.setBounds (top.withSizeKeepingCentre (112, 140));

    mix.setBounds    (bottom.removeFromLeft (colW).withSizeKeepingCentre (112, 140));
    output.setBounds (bottom.removeFromLeft (colW).withSizeKeepingCentre (112, 140));
    unhinged.setBounds (bottom.withSizeKeepingCentre (118, 84));
}
