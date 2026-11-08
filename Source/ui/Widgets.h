#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"

namespace naji::ui
{
    // Shared editor state: which effect slot the left/right panels are editing.
    struct EditorState
    {
        int focusedSlot = 0;
        std::function<void()> onChange;
        void setFocus (int s) { if (s != focusedSlot) { focusedSlot = s; if (onChange) onChange(); } }
    };

    //==========================================================================
    // Rotary knob with name + value; the drawn position glides to the real value.
    class Knob final : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& label,
              LookAndFeel& lnf, int size = 64);
        void paint (juce::Graphics&) override;
        void resized() override;
        void tick();
        void setLabel (const juce::String& l) { label = l; repaint(); }

    private:
        LookAndFeel& laf;
        juce::String label;
        int knobSize;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        float disp = -1.0f;
    };

    //==========================================================================
    class EffectSelector final : public juce::Component
    {
    public:
        EffectSelector (NajiDistortionProcessor& p, EditorState& s, LookAndFeel& lnf);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void tick (float level);

    private:
        int effectAt (juce::Point<float>) const;
        int slotValue (int s) const;
        void setSlot (int s, int effect);

        NajiDistortionProcessor& proc;
        EditorState& state;
        LookAndFeel& laf;
        std::array<juce::Rectangle<float>, fx::numEffects + 1> tiles;  // [0] = off
        std::array<juce::Rectangle<float>, 2> slotPills;
        std::array<juce::Rectangle<float>, 4> headers;
        juce::Rectangle<float> highlight;
        int hovered = -1, lastA = -1, lastB = -1;
        float level = 0.0f, time = 0.0f;
    };

    //==========================================================================
    // Six knobs for the focused slot's effect (all 84 effect knobs exist; only
    // the active effect's six are visible).
    class EffectPanel final : public juce::Component
    {
    public:
        EffectPanel (NajiDistortionProcessor& p, EditorState& s, LookAndFeel& lnf);
        void paint (juce::Graphics&) override;
        void resized() override;
        void tick();

    private:
        void showEffect (int effect, int slot);

        NajiDistortionProcessor& proc;
        EditorState& state;
        LookAndFeel& laf;
        std::vector<std::unique_ptr<Knob>> knobs;   // [effect-1][param]
        int shownEffect = -1, shownSlot = -1;
        float fade = 1.0f;
    };

    //==========================================================================
    class LabPanel final : public juce::Component
    {
    public:
        LabPanel (NajiDistortionProcessor& p, LookAndFeel& lnf);
        void paint (juce::Graphics&) override;
        void resized() override;
        void tick();

    private:
        LookAndFeel& laf;
        std::vector<std::unique_ptr<Knob>> knobs;
    };

    //==========================================================================
    class RightPanel final : public juce::Component
    {
    public:
        RightPanel (NajiDistortionProcessor& p, EditorState& s, LookAndFeel& lnf);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void tick();
        void setTab (int t);

    private:
        LookAndFeel& laf;
        EffectPanel effect;
        LabPanel lab;
        int tab = 0;
        float tabAnim = 0.0f;
        juce::Rectangle<float> tabBounds[2];
    };

    //==========================================================================
    // Segmented choice (drive type) and the UNHINGED switch.
    class Segmented final : public juce::Component
    {
    public:
        Segmented (juce::RangedAudioParameter& param, juce::StringArray names, LookAndFeel& lnf);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void tick();

    private:
        LookAndFeel& laf;
        juce::StringArray names;
        int selected = 0;
        float anim = 0.0f;
        juce::ParameterAttachment attachment;
    };

    class UnhingedSwitch final : public juce::Component
    {
    public:
        UnhingedSwitch (juce::RangedAudioParameter& param, LookAndFeel& lnf);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void tick (float level);

    private:
        LookAndFeel& laf;
        bool on = false;
        float anim = 0.0f, level = 0.0f;
        juce::ParameterAttachment attachment;
    };

    class Meters final : public juce::Component
    {
    public:
        explicit Meters (NajiDistortionProcessor& p, LookAndFeel& lnf) : proc (p), laf (lnf) {}
        void paint (juce::Graphics&) override;
        void tick();
        float getOutLevel() const noexcept { return juce::jmax (lv[2], lv[3]); }

    private:
        NajiDistortionProcessor& proc;
        LookAndFeel& laf;
        float lv[4] {}, hold[4] {}, holdTime[4] {};
        float autoGain = 0.0f, lastSum = -1.0f;
    };

    //==========================================================================
    class PresetBrowser;

    class PresetBar final : public juce::Component, private juce::ChangeListener
    {
    public:
        PresetBar (NajiDistortionProcessor& p, LookAndFeel& lnf);
        ~PresetBar() override;
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void tick();

        std::function<void()> onBrowse;
        juce::TextButton browseButton { "browse" }, prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" }, loadButton { "LOAD" };

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void showMenu();
        void savePreset();
        void loadPreset();

        NajiDistortionProcessor& proc;
        LookAndFeel& laf;
        juce::Rectangle<float> nameBox;
        juce::String shownName, shownCategory, oldName;
        float anim = 1.0f, flash = 0.0f;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    // Searchable preset browser that slides over the centre and right panels.
    class PresetBrowser final : public juce::Component, private juce::ChangeListener
    {
    public:
        PresetBrowser (NajiDistortionProcessor& p, LookAndFeel& lnf);
        ~PresetBrowser() override;
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;

        void open();
        void close();
        bool isOpen() const noexcept { return target > 0.5f; }
        void tick();

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void refilter();
        int rowAt (juce::Point<float>) const;
        juce::String chainFor (int factoryIndex) const;

        NajiDistortionProcessor& proc;
        LookAndFeel& laf;
        juce::TextEditor search;
        juce::TextButton closeButton { "x" };
        juce::StringArray categories;
        juce::String category = "All";
        std::vector<juce::Rectangle<float>> chipBounds;
        juce::Array<int> filtered;
        juce::Rectangle<float> listArea;
        float scroll = 0.0f, scrollTarget = 0.0f, anim = 0.0f, target = 0.0f;
        int hovered = -1;
        static constexpr float rowH = 46.0f;
    };
}
