#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

/*
    Factory presets + preset management (load / save / browse / next / prev).

    A preset stores every parameter. Values are plain parameter values:
    percent knobs 0..100, OUTPUT in dB, WIDTH 0..200, choices as indices.
    Anything a preset does not list is reset to its default, so a preset
    always recalls the complete sound.
*/
namespace naji
{
    struct FactoryPreset
    {
        const char* name;
        const char* category;
        const char* description;
        std::vector<std::pair<const char*, float>> values;
    };

    const std::vector<FactoryPreset>& factoryPresets();

    class PresetManager final : public juce::ChangeBroadcaster
    {
    public:
        struct Entry
        {
            juce::String name, category, description;
            int factoryIndex = -1;     // >= 0 for factory presets
            juce::File file;           // user presets
        };

        explicit PresetManager (juce::AudioProcessor& processor);

        static juce::File userFolder();

        void rescan();
        const juce::Array<Entry>& getEntries() const noexcept { return entries; }

        void loadFactory (int index);
        bool loadEntry (int entryIndex);
        bool loadFile (const juce::File& f);
        bool saveToFile (const juce::File& f, const juce::String& name, const juce::String& category);

        void next()     { step (1); }
        void previous() { step (-1); }

        juce::String getCurrentName() const     { return currentName; }
        juce::String getCurrentCategory() const { return currentCategory; }
        int getCurrentEntry() const noexcept    { return currentEntry; }
        int getCurrentFactoryIndex() const;

        // Persist which preset is loaded inside the plugin state.
        void writeTo (juce::XmlElement& xml) const;
        void readFrom (const juce::XmlElement& xml);

        // Sets every parameter (defaults first, then the given plain values).
        void applyValues (const std::vector<std::pair<juce::String, float>>& values);

    private:
        void step (int delta);
        void setCurrent (const juce::String& name, const juce::String& category, int entry);

        juce::AudioProcessor& proc;
        juce::Array<Entry> entries;
        juce::String currentName, currentCategory;
        int currentEntry = 0;
    };
}
