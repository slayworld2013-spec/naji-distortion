#include "PluginEditor.h"

using namespace naji::ui;

namespace
{
    constexpr int editorW = 1120, editorH = 700;
}

NajiDistortionEditor::NajiDistortionEditor (NajiDistortionProcessor& p)
    : AudioProcessorEditor (p),
      proc (p),
      presetBar (p, laf),
      selector (p, state, laf),
      stage (laf),
      wave (p, laf),
      right (p, state, laf),
      drive (p.apvts, ParamIDs::drive, "Drive", laf, 78),
      mix (p.apvts, ParamIDs::mix, "Mix", laf, 58),
      output (p.apvts, ParamIDs::output, "Output", laf, 58),
      driveType (*p.apvts.getParameter (ParamIDs::type), { "SOFT", "HARD", "TUBE", "FOLD" }, laf),
      unhinged (*p.apvts.getParameter (ParamIDs::unhinged), laf),
      meters (p, laf),
      browser (p, laf)
{
    setLookAndFeel (&laf);
    for (auto* c : std::initializer_list<juce::Component*> { &presetBar, &selector, &stage, &wave, &right, &drive, &mix,
                                                             &output, &driveType, &unhinged, &meters })
        addAndMakeVisible (c);
    addChildComponent (browser);

    presetBar.onBrowse = [this] { if (browser.isOpen()) browser.close(); else browser.open(); };
    state.onChange = [this] { selector.repaint(); right.repaint(); };

    setSize (editorW, editorH);
    lastTime = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

NajiDistortionEditor::~NajiDistortionEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void NajiDistortionEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.1, (now - lastTime) * 0.001);
    lastTime = now;

    meters.tick();
    const float level = juce::jlimit (0.0f, 1.0f, meters.getOutLevel() * 1.6f);
    const int a = proc.activeSlot[0].load (std::memory_order_relaxed);
    const int b = proc.activeSlot[1].load (std::memory_order_relaxed);
    const int pa = juce::roundToInt (proc.apvts.getRawParameterValue (ParamIDs::slotA)->load());
    const int pb = juce::roundToInt (proc.apvts.getRawParameterValue (ParamIDs::slotB)->load());
    // Show the selected effects immediately, even before audio has run the crossfade.
    juce::ignoreUnused (a, b);

    // The photo stage animates at 30 fps while audio plays or cards move, 15 fps when idle
    // (slow floating motion stays smooth, and it keeps drawing cost low on CPU renderers).
    stageDt += dt;
    const bool busy = level > 0.01f || stage.isBusy() || pa != lastA || pb != lastB;
    lastA = pa; lastB = pb;
    if (++frame % (busy ? 2 : 4) == 0) { stage.tick (stageDt, level, pa, pb); stageDt = 0.0f; }
    wave.tick();
    selector.tick (level);
    right.tick();
    presetBar.tick();
    drive.tick(); mix.tick(); output.tick();
    driveType.tick();
    unhinged.tick (level);
    browser.tick();
}

void NajiDistortionEditor::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (Colour (0xfff7f9fc), 0, 0, Colour (0xffe6edf5), 0, b.getHeight(), false));
    g.fillAll();
    g.setGradientFill (ColourGradient (colours::glow.withAlpha (0.25f), b.getWidth() * 0.5f, -80.0f,
                                       colours::glow.withAlpha (0.0f), b.getWidth() * 0.5f, 260.0f, true));
    g.fillRect (b.withHeight (260.0f));

    // Branding
    const auto big = laf.boldFont (26.0f).withExtraKerningFactor (0.06f);
    const float w1 = GlyphArrangement::getStringWidth (big, "NAJI ");
    g.setFont (big);
    g.setColour (colours::text);
    g.drawText ("NAJI", Rectangle<float> (26.0f, 12.0f, w1, 34.0f), Justification::centredLeft);
    g.setGradientFill (ColourGradient (colours::accent, 26.0f + w1, 0, colours::accent2, 26.0f + w1 + 170.0f, 0, false));
    g.drawText ("DISTORTION", Rectangle<float> (26.0f + w1, 12.0f, 220.0f, 34.0f), Justification::centredLeft);
    g.setFont (laf.boldFont (9.0f).withExtraKerningFactor (0.3f));
    g.setColour (colours::textFaint);
    g.drawText ("EXPERIMENTAL DISTORTION LAB", Rectangle<float> (27.0f, 44.0f, 260.0f, 14.0f), Justification::centredLeft);
    g.drawText ("v" JucePlugin_VersionString, Rectangle<float> (b.getWidth() - 90.0f, 22.0f, 66.0f, 20.0f), Justification::centredRight);

    // Bottom panel
    drawPanel (g, bottomPanel.toFloat());
    g.setColour (colours::edge);
    const float x1 = (float) unhinged.getRight() + 22.0f, x2 = (float) output.getRight() + 18.0f;
    g.drawVerticalLine ((int) x1, (float) bottomPanel.getY() + 18.0f, (float) bottomPanel.getBottom() - 18.0f);
    g.drawVerticalLine ((int) x2, (float) bottomPanel.getY() + 18.0f, (float) bottomPanel.getBottom() - 18.0f);
    g.setFont (laf.boldFont (9.0f).withExtraKerningFactor (0.24f));
    g.setColour (colours::textFaint);
    g.drawText ("DRIVE CHARACTER", Rectangle<int> (driveType.getX(), bottomPanel.getY() + 16, driveType.getWidth(), 14), Justification::centredLeft);
}

void NajiDistortionEditor::resized()
{
    auto b = getLocalBounds();
    auto top = b.removeFromTop (66).reduced (16, 0);
    top.removeFromLeft (290);
    top.removeFromRight (100);
    presetBar.setBounds (top.reduced (0, 10));

    bottomPanel = b.removeFromBottom (126).reduced (16, 8);
    b.reduce (16, 6);

    selector.setBounds (b.removeFromLeft (220));
    b.removeFromLeft (12);
    right.setBounds (b.removeFromRight (318));
    b.removeFromRight (12);
    auto centre = b;
    wave.setBounds (centre.removeFromBottom (116));
    centre.removeFromBottom (10);
    stage.setBounds (centre);

    // Browser overlays the centre and right columns
    browser.setBounds (stage.getX() - 6, stage.getY() - 6, right.getRight() - stage.getX() + 12, wave.getBottom() - stage.getY() + 12);

    auto bot = bottomPanel.reduced (20, 8);
    drive.setBounds (bot.removeFromLeft (110));
    bot.removeFromLeft (16);
    auto chars = bot.removeFromLeft (250);
    driveType.setBounds (chars.withTrimmedTop (30).withHeight (30));
    unhinged.setBounds (chars.withTrimmedTop (70).withHeight (30).withWidth (150));
    bot.removeFromLeft (44);
    mix.setBounds (bot.removeFromLeft (100).withTrimmedTop (8));
    output.setBounds (bot.removeFromLeft (100).withTrimmedTop (8));
    bot.removeFromLeft (36);
    meters.setBounds (bot.removeFromLeft (150));
}
