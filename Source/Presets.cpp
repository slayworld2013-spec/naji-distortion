#include "Presets.h"

namespace naji
{
    // Slot values: 0 Off, 1 Glitch, 2 Melt, 3 Octave, 4 Formant, 5 Destroy, 6 Warp, 7 Robot,
    //              8 Vinyl, 9 Ring, 10 Fuzz, 11 Granular, 12 Spectral, 13 Freq Shift, 14 Reverse
    // Drive type:  0 Soft, 1 Hard, 2 Tube, 3 Fold
    // Gate rate:   0 1/4, 1 1/8, 2 1/8T, 3 1/16, 4 1/16T, 5 1/32
    const std::vector<FactoryPreset>& factoryPresets()
    {
        static const std::vector<FactoryPreset> presets = {
            { "Payday 2", "Aggressive", "Industrial metal: FM ring mod into a destroyed, comb-resonant clipper",
              { { "slotA", 9 }, { "ring_freq", 66 }, { "ring_shape", 60 }, { "ring_track", 15 }, { "ring_fm", 45 }, { "ring_spread", 40 }, { "ring_mix", 70 },
                { "slotB", 5 }, { "destroy_crunch", 55 }, { "destroy_fold", 40 }, { "destroy_bits", 20 }, { "destroy_rect", 0 }, { "destroy_sputter", 10 }, { "destroy_mix", 85 },
                { "drive", 70 }, { "type", 1 }, { "tone", 62 }, { "comb", 35 }, { "combFreq", 60 }, { "width", 120 } } },

            { "Dollar Store Cappucino Assassino", "Lo-Fi", "Melting, drifting tape into a crushed, crackling record",
              { { "slotA", 2 }, { "melt_wobble", 70 }, { "melt_speed", 55 }, { "melt_drift", 70 }, { "melt_smear", 40 }, { "melt_sag", 60 }, { "melt_mix", 100 },
                { "slotB", 8 }, { "vinyl_wow", 20 }, { "vinyl_flutter", 50 }, { "vinyl_crackle", 60 }, { "vinyl_noise", 40 }, { "vinyl_age", 70 }, { "vinyl_mix", 100 },
                { "drive", 45 }, { "type", 3 }, { "crush", 45 }, { "downsample", 30 }, { "tone", 50 } } },

            { "Gummy808s", "Bass", "Sub and octave-down voices driven through a stacked fuzz",
              { { "slotA", 3 }, { "octave_down", 60 }, { "octave_up", 10 }, { "octave_sub", 70 }, { "octave_harm", 55 }, { "octave_grain", 70 }, { "octave_blend", 65 },
                { "slotB", 10 }, { "fuzz_fuzz", 45 }, { "fuzz_bias", 20 }, { "fuzz_gate", 0 }, { "fuzz_stack", 40 }, { "fuzz_scoop", 25 }, { "fuzz_mix", 60 },
                { "drive", 65 }, { "type", 2 }, { "tone", 40 } } },

            { "Jacob", "Signature", "Talking vowels sprayed into an octave-jumping grain cloud",
              { { "slotA", 4 }, { "formant_vowel", 30 }, { "formant_shift", 60 }, { "formant_res", 60 }, { "formant_mod", 45 }, { "formant_move", 50 }, { "formant_amount", 90 },
                { "slotB", 11 }, { "gran_size", 40 }, { "gran_density", 60 }, { "gran_pos", 10 }, { "gran_spray", 35 }, { "gran_pitch", 70 }, { "gran_mix", 45 },
                { "drive", 45 }, { "type", 0 }, { "width", 150 }, { "tone", 55 } } },

            { "Nineslay", "Aggressive", "Folded, bit-smashed destruction chopped into 1/32 stutters",
              { { "slotA", 5 }, { "destroy_crunch", 85 }, { "destroy_fold", 55 }, { "destroy_bits", 55 }, { "destroy_rect", 30 }, { "destroy_sputter", 0 }, { "destroy_mix", 100 },
                { "slotB", 1 }, { "glitch_slice", 80 }, { "glitch_chaos", 40 }, { "glitch_repeat", 50 }, { "glitch_pitch", 0 }, { "glitch_chop", 35 }, { "glitch_mix", 100 },
                { "drive", 60 }, { "type", 3 }, { "alias", 45 }, { "tone", 70 } } },

            { "Raheem", "Experimental", "An octave down, random pitch steps, self-FM and a half-frozen spectrum",
              { { "slotA", 6 }, { "warp_bend", 0 }, { "warp_sweep", 15 }, { "warp_rate", 30 }, { "warp_shape", 100 }, { "warp_fm", 35 }, { "warp_mix", 100 },
                { "slotB", 12 }, { "spec_freeze", 40 }, { "spec_shift", 45 }, { "spec_stretch", 40 }, { "spec_blur", 30 }, { "spec_thin", 0 }, { "spec_mix", 60 },
                { "drive", 60 }, { "type", 2 }, { "tone", 35 } } },

            { "Recqn", "Glitch", "Unpredictable pitched stutters into rhythmic reverse flips",
              { { "slotA", 1 }, { "glitch_slice", 60 }, { "glitch_chaos", 85 }, { "glitch_repeat", 60 }, { "glitch_pitch", 70 }, { "glitch_chop", 20 }, { "glitch_mix", 100 },
                { "slotB", 14 }, { "rev_len", 20 }, { "rev_chance", 50 }, { "rev_fb", 25 }, { "rev_fade", 15 }, { "rev_speed", 50 }, { "rev_mix", 50 },
                { "drive", 40 }, { "type", 1 } } },

            { "Liam", "Atmospheric", "Smooth, wide spectral smear with a gentle vowel colour",
              { { "slotA", 12 }, { "spec_freeze", 65 }, { "spec_shift", 50 }, { "spec_stretch", 55 }, { "spec_blur", 45 }, { "spec_thin", 0 }, { "spec_mix", 70 },
                { "slotB", 4 }, { "formant_vowel", 60 }, { "formant_shift", 45 }, { "formant_res", 35 }, { "formant_mod", 25 }, { "formant_move", 0 }, { "formant_amount", 50 },
                { "drive", 40 }, { "type", 0 }, { "tone", 45 }, { "width", 160 } } },

            { "Housprty", "Rhythmic", "Swirling frequency shifter, chopped stutters and a 1/16 gate",
              { { "slotA", 13 }, { "fshift_shift", 62 }, { "fshift_fb", 30 }, { "fshift_mod", 40 }, { "fshift_rate", 75 }, { "fshift_stereo", 70 }, { "fshift_mix", 60 },
                { "slotB", 1 }, { "glitch_slice", 40 }, { "glitch_chaos", 60 }, { "glitch_repeat", 30 }, { "glitch_pitch", 30 }, { "glitch_chop", 50 }, { "glitch_mix", 100 },
                { "drive", 55 }, { "type", 1 }, { "gate", 80 }, { "gateRate", 3 }, { "tone", 60 } } },

            { "Raheem. Yshaouzi", "Experimental", "Dense grain clouds layered with a slow, tracking ring modulator and comb",
              { { "slotA", 11 }, { "gran_size", 70 }, { "gran_density", 75 }, { "gran_pos", 40 }, { "gran_spray", 70 }, { "gran_pitch", 40 }, { "gran_mix", 70 },
                { "slotB", 9 }, { "ring_freq", 45 }, { "ring_shape", 0 }, { "ring_track", 40 }, { "ring_fm", 30 }, { "ring_spread", 60 }, { "ring_mix", 50 },
                { "drive", 30 }, { "type", 0 }, { "comb", 30 }, { "combFreq", 30 }, { "width", 170 } } },

            { "Danzie", "Modulated", "Bright vibrato sweeps into a spiralling stereo frequency shifter",
              { { "slotA", 6 }, { "warp_bend", 50 }, { "warp_sweep", 25 }, { "warp_rate", 60 }, { "warp_shape", 25 }, { "warp_fm", 10 }, { "warp_mix", 100 },
                { "slotB", 13 }, { "fshift_shift", 60 }, { "fshift_fb", 40 }, { "fshift_mod", 60 }, { "fshift_rate", 50 }, { "fshift_stereo", 80 }, { "fshift_mix", 60 },
                { "drive", 35 }, { "type", 0 }, { "tone", 80 }, { "width", 150 } } },

            { "Yusuf", "Atmospheric", "Long reversed swells feeding a frozen, blurred spectrum",
              { { "slotA", 14 }, { "rev_len", 60 }, { "rev_chance", 100 }, { "rev_fb", 45 }, { "rev_fade", 80 }, { "rev_speed", 50 }, { "rev_mix", 70 },
                { "slotB", 12 }, { "spec_freeze", 80 }, { "spec_shift", 45 }, { "spec_stretch", 45 }, { "spec_blur", 60 }, { "spec_thin", 20 }, { "spec_mix", 70 },
                { "drive", 25 }, { "type", 0 }, { "tone", 25 }, { "width", 160 } } },

            { "Fuquad", "Experimental", "Downward frequency-shift feedback through a robot comb, unhinged",
              { { "slotA", 13 }, { "fshift_shift", 21 }, { "fshift_fb", 70 }, { "fshift_mod", 50 }, { "fshift_rate", 30 }, { "fshift_stereo", 100 }, { "fshift_mix", 100 },
                { "slotB", 7 }, { "robot_pitch", 60 }, { "robot_robot", 70 }, { "robot_metal", 60 }, { "robot_res", 70 }, { "robot_ring", 40 }, { "robot_mix", 80 },
                { "drive", 40 }, { "type", 3 }, { "unhinged", 1 }, { "alias", 30 } } },

            { "Anime Gamer", "Synthetic", "Chipmunk +12 pitch shift with bright, moving 'ee' formants",
              { { "slotA", 6 }, { "warp_bend", 100 }, { "warp_sweep", 0 }, { "warp_rate", 50 }, { "warp_shape", 0 }, { "warp_fm", 0 }, { "warp_mix", 100 },
                { "slotB", 4 }, { "formant_vowel", 50 }, { "formant_shift", 70 }, { "formant_res", 50 }, { "formant_mod", 30 }, { "formant_move", 40 }, { "formant_amount", 60 },
                { "drive", 30 }, { "type", 0 }, { "tone", 80 }, { "width", 140 } } },

            { "Dicks Sporting Goods", "Aggressive", "Sputtering biased fuzz chopped by a square-wave ring mod",
              { { "slotA", 10 }, { "fuzz_fuzz", 80 }, { "fuzz_bias", 70 }, { "fuzz_gate", 30 }, { "fuzz_stack", 70 }, { "fuzz_scoop", 70 }, { "fuzz_mix", 100 },
                { "slotB", 9 }, { "ring_freq", 25 }, { "ring_shape", 80 }, { "ring_track", 0 }, { "ring_fm", 0 }, { "ring_spread", 20 }, { "ring_mix", 60 },
                { "drive", 50 }, { "type", 1 }, { "crush", 25 } } },

            { "Fortnite", "Glitch", "Digital stutter storm into bit-crushed destruction",
              { { "slotA", 1 }, { "glitch_slice", 60 }, { "glitch_chaos", 70 }, { "glitch_repeat", 70 }, { "glitch_pitch", 50 }, { "glitch_chop", 40 }, { "glitch_mix", 100 },
                { "slotB", 5 }, { "destroy_crunch", 70 }, { "destroy_fold", 20 }, { "destroy_bits", 70 }, { "destroy_rect", 0 }, { "destroy_sputter", 30 }, { "destroy_mix", 80 },
                { "drive", 55 }, { "type", 1 }, { "alias", 30 }, { "downsample", 25 } } },

            { "Pirate Shop Token", "Chaos", "Random pitch jumps and self-FM into a fully stacked fuzz, unhinged",
              { { "slotA", 6 }, { "warp_bend", 50 }, { "warp_sweep", 60 }, { "warp_rate", 70 }, { "warp_shape", 100 }, { "warp_fm", 50 }, { "warp_mix", 100 },
                { "slotB", 10 }, { "fuzz_fuzz", 70 }, { "fuzz_bias", 40 }, { "fuzz_gate", 10 }, { "fuzz_stack", 90 }, { "fuzz_scoop", 50 }, { "fuzz_mix", 100 },
                { "drive", 40 }, { "type", 2 }, { "unhinged", 1 } } },

            { "Boolymon", "Modulated", "Maximum tape melt into a fast-moving, high-Q vowel filter",
              { { "slotA", 2 }, { "melt_wobble", 100 }, { "melt_speed", 70 }, { "melt_drift", 80 }, { "melt_smear", 80 }, { "melt_sag", 40 }, { "melt_mix", 100 },
                { "slotB", 4 }, { "formant_vowel", 80 }, { "formant_shift", 30 }, { "formant_res", 80 }, { "formant_mod", 80 }, { "formant_move", 60 }, { "formant_amount", 80 },
                { "drive", 45 }, { "type", 0 }, { "comb", 25 }, { "combFreq", 55 }, { "width", 130 } } },
        };
        return presets;
    }

    //==========================================================================
    PresetManager::PresetManager (juce::AudioProcessor& p) : proc (p)
    {
        rescan();
    }

    juce::File PresetManager::userFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("NAJI DISTORTION").getChildFile ("Presets");
    }

    void PresetManager::rescan()
    {
        entries.clear();
        const auto& f = factoryPresets();
        for (int i = 0; i < (int) f.size(); ++i)
            entries.add ({ f[(size_t) i].name, f[(size_t) i].category, f[(size_t) i].description, i, {} });

        const auto folder = userFolder();
        if (folder.isDirectory())
        {
            auto files = folder.findChildFiles (juce::File::findFiles, false, "*.najipreset");
            files.sort();
            for (auto& file : files)
            {
                juce::String name = file.getFileNameWithoutExtension(), category = "User", desc;
                if (auto xml = juce::XmlDocument::parse (file))
                {
                    name = xml->getStringAttribute ("name", name);
                    desc = xml->getStringAttribute ("description");
                }
                entries.add ({ name, category, desc, -1, file });
            }
        }
    }

    void PresetManager::applyValues (const std::vector<std::pair<juce::String, float>>& values)
    {
        for (auto* p : proc.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (rp == nullptr) continue;
            float norm = rp->getDefaultValue();
            for (auto& [id, v] : values)
                if (id == rp->getParameterID()) { norm = rp->convertTo0to1 (v); break; }
            rp->setValueNotifyingHost (norm);
        }
    }

    void PresetManager::loadFactory (int index)
    {
        const auto& f = factoryPresets();
        if (index < 0 || index >= (int) f.size()) return;
        std::vector<std::pair<juce::String, float>> values;
        for (auto& [id, v] : f[(size_t) index].values) values.emplace_back (id, v);
        applyValues (values);
        setCurrent (f[(size_t) index].name, f[(size_t) index].category, index);
    }

    bool PresetManager::loadEntry (int entryIndex)
    {
        if (! juce::isPositiveAndBelow (entryIndex, entries.size())) return false;
        const auto& e = entries.getReference (entryIndex);
        if (e.factoryIndex >= 0) { loadFactory (e.factoryIndex); return true; }
        return loadFile (e.file);
    }

    bool PresetManager::loadFile (const juce::File& file)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName ("NAJI_PRESET")) return false;

        std::vector<std::pair<juce::String, float>> values;
        for (auto* param : xml->getChildWithTagNameIterator ("PARAM"))
            values.emplace_back (param->getStringAttribute ("id"), (float) param->getDoubleAttribute ("value"));
        applyValues (values);

        int idx = -1;
        for (int i = 0; i < entries.size(); ++i)
            if (entries.getReference (i).file == file) idx = i;
        setCurrent (xml->getStringAttribute ("name", file.getFileNameWithoutExtension()), "User", idx);
        return true;
    }

    bool PresetManager::saveToFile (const juce::File& file, const juce::String& name, const juce::String& category)
    {
        juce::XmlElement xml ("NAJI_PRESET");
        xml.setAttribute ("name", name);
        xml.setAttribute ("category", category);
        xml.setAttribute ("version", 3);
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                auto* e = xml.createNewChildElement ("PARAM");
                e->setAttribute ("id", rp->getParameterID());
                e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
            }
        file.getParentDirectory().createDirectory();
        if (! xml.writeTo (file)) return false;
        rescan();
        int idx = -1;
        for (int i = 0; i < entries.size(); ++i)
            if (entries.getReference (i).file == file) idx = i;
        setCurrent (name, "User", idx);
        return true;
    }

    int PresetManager::getCurrentFactoryIndex() const
    {
        if (juce::isPositiveAndBelow (currentEntry, entries.size()))
            return entries.getReference (currentEntry).factoryIndex;
        return -1;
    }

    void PresetManager::step (int delta)
    {
        if (entries.isEmpty()) return;
        const int n = entries.size();
        const int start = juce::isPositiveAndBelow (currentEntry, n) ? currentEntry : 0;
        loadEntry (((start + delta) % n + n) % n);
    }

    void PresetManager::setCurrent (const juce::String& name, const juce::String& category, int entry)
    {
        currentName = name;
        currentCategory = category;
        currentEntry = entry;
        sendChangeMessage();
    }

    void PresetManager::writeTo (juce::XmlElement& xml) const
    {
        xml.setAttribute ("presetName", currentName);
        xml.setAttribute ("presetCategory", currentCategory);
        xml.setAttribute ("presetEntry", currentEntry);
    }

    void PresetManager::readFrom (const juce::XmlElement& xml)
    {
        currentName = xml.getStringAttribute ("presetName", currentName);
        currentCategory = xml.getStringAttribute ("presetCategory", currentCategory);
        currentEntry = xml.getIntAttribute ("presetEntry", currentEntry);
        sendChangeMessage();
    }
}
