#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>

namespace humvocal
{

struct PresetData
{
    juce::String name;
    juce::String category;   // "Factory" or "User"
    juce::XmlElement* stateXml = nullptr;
    std::unique_ptr<juce::XmlElement> ownedXml;

    PresetData() = default;
    PresetData (const juce::String& n, const juce::String& cat)
        : name (n), category (cat) {}
};

class PresetManager
{
public:
    PresetManager (juce::AudioProcessorValueTreeState& apvts)
        : valueTreeState (apvts)
    {
        userPresetsDir = juce::File::getSpecialLocation(
            juce::File::userApplicationDataDirectory)
            .getChildFile("HumHouse")
            .getChildFile("HumHouse Vocals")
            .getChildFile("Presets");

        if (!userPresetsDir.exists())
            userPresetsDir.createDirectory();

        buildFactoryPresets();
        scanUserPresets();
    }

    // -----------------------------------------------------------------------
    // Preset access
    // -----------------------------------------------------------------------
    int getNumPresets() const { return static_cast<int>(allPresets.size()); }

    juce::String getPresetName (int index) const
    {
        if (index >= 0 && index < static_cast<int>(allPresets.size()))
            return allPresets[static_cast<size_t>(index)].name;
        return {};
    }

    juce::String getPresetCategory (int index) const
    {
        if (index >= 0 && index < static_cast<int>(allPresets.size()))
            return allPresets[static_cast<size_t>(index)].category;
        return {};
    }

    juce::StringArray getPresetNames() const
    {
        juce::StringArray names;
        for (auto& p : allPresets)
            names.add(p.name);
        return names;
    }

    int getCurrentPresetIndex() const { return currentPresetIndex; }

    // -----------------------------------------------------------------------
    // Load preset
    // -----------------------------------------------------------------------
    void loadPreset (int index)
    {
        if (index < 0 || index >= static_cast<int>(allPresets.size()))
            return;

        auto& preset = allPresets[static_cast<size_t>(index)];

        if (preset.category == "Factory")
        {
            applyFactoryPreset(preset.name);
        }
        else if (preset.ownedXml)
        {
            auto state = juce::ValueTree::fromXml(*preset.ownedXml);
            if (state.isValid())
                valueTreeState.replaceState(state);
        }

        currentPresetIndex = index;
    }

    // -----------------------------------------------------------------------
    // Save user preset
    // -----------------------------------------------------------------------
    void savePreset (const juce::String& name)
    {
        auto state = valueTreeState.copyState();
        auto xml = state.createXml();

        auto file = userPresetsDir.getChildFile(
            name.replaceCharacters(" /\\:", "____") + ".xml");

        xml->writeTo(file);

        // Check if this name already exists in user presets
        bool found = false;
        for (size_t i = 0; i < allPresets.size(); ++i)
        {
            if (allPresets[i].name == name && allPresets[i].category == "User")
            {
                allPresets[i].ownedXml = std::move(xml);
                currentPresetIndex = static_cast<int>(i);
                found = true;
                break;
            }
        }

        if (!found)
        {
            PresetData pd (name, "User");
            pd.ownedXml = std::move(xml);
            allPresets.push_back(std::move(pd));
            currentPresetIndex = static_cast<int>(allPresets.size()) - 1;
        }
    }

    // -----------------------------------------------------------------------
    // Delete user preset
    // -----------------------------------------------------------------------
    bool deletePreset (int index)
    {
        if (index < 0 || index >= static_cast<int>(allPresets.size()))
            return false;
        if (allPresets[static_cast<size_t>(index)].category == "Factory")
            return false;

        auto name = allPresets[static_cast<size_t>(index)].name;
        auto file = userPresetsDir.getChildFile(
            name.replaceCharacters(" /\\:", "____") + ".xml");
        file.deleteFile();

        allPresets.erase(allPresets.begin() + index);
        if (currentPresetIndex >= static_cast<int>(allPresets.size()))
            currentPresetIndex = static_cast<int>(allPresets.size()) - 1;
        return true;
    }

    // -----------------------------------------------------------------------
    // Rename user preset
    // -----------------------------------------------------------------------
    bool renamePreset (int index, const juce::String& newName)
    {
        if (index < 0 || index >= static_cast<int>(allPresets.size()))
            return false;
        if (allPresets[static_cast<size_t>(index)].category == "Factory")
            return false;

        auto& preset = allPresets[static_cast<size_t>(index)];
        auto oldFile = userPresetsDir.getChildFile(
            preset.name.replaceCharacters(" /\\:", "____") + ".xml");
        auto newFile = userPresetsDir.getChildFile(
            newName.replaceCharacters(" /\\:", "____") + ".xml");

        oldFile.moveFileTo(newFile);
        preset.name = newName;
        return true;
    }

    // -----------------------------------------------------------------------
    // Get user presets directory (for browsing)
    // -----------------------------------------------------------------------
    juce::File getUserPresetsDir() const { return userPresetsDir; }

private:
    juce::AudioProcessorValueTreeState& valueTreeState;
    juce::File userPresetsDir;
    std::vector<PresetData> allPresets;
    int currentPresetIndex = -1;

    // -----------------------------------------------------------------------
    // Scan user presets from disk
    // -----------------------------------------------------------------------
    void scanUserPresets()
    {
        auto files = userPresetsDir.findChildFiles(
            juce::File::findFiles, false, "*.xml");

        files.sort();

        for (auto& file : files)
        {
            auto xml = juce::XmlDocument::parse(file);
            if (xml)
            {
                PresetData pd (file.getFileNameWithoutExtension()
                    .replaceCharacters("____", " /\\:"), "User");
                pd.ownedXml = std::move(xml);
                allPresets.push_back(std::move(pd));
            }
        }
    }

    // -----------------------------------------------------------------------
    // Factory presets — based on Gamma Vocal Suite & genre research
    // -----------------------------------------------------------------------
    void buildFactoryPresets()
    {
        // Default / Init
        addFactoryPreset("Default (Init)");

        // --- TRAP / HIP-HOP (8 presets) ---
        addFactoryPreset("Trap Hard AutoTune");
        addFactoryPreset("Trap Melodic");
        addFactoryPreset("Trap Dark & Wet");
        addFactoryPreset("Trap Adlib Bright");
        addFactoryPreset("Trap Mumble Smooth");
        addFactoryPreset("Drill Raw Vocal");
        addFactoryPreset("Rage Beat Vocal");
        addFactoryPreset("808 Bass Vocal");

        // --- R&B / SOUL (6 presets) ---
        addFactoryPreset("Smooth R&B");
        addFactoryPreset("R&B Warm Intimate");
        addFactoryPreset("Neo Soul Vintage");
        addFactoryPreset("R&B Falsetto Air");
        addFactoryPreset("90s R&B Classic");
        addFactoryPreset("Bedroom R&B");

        // --- POP (5 presets) ---
        addFactoryPreset("Pop Radio Ready");
        addFactoryPreset("Pop Bright & Airy");
        addFactoryPreset("Pop Ballad Lush");
        addFactoryPreset("K-Pop Crystal");
        addFactoryPreset("Pop Punk Grit");

        // --- ROCK / METAL (4 presets) ---
        addFactoryPreset("Rock Aggressive");
        addFactoryPreset("Rock Warm Analog");
        addFactoryPreset("Metal Scream");
        addFactoryPreset("Indie Folk Natural");

        // --- LO-FI / CHARACTER (5 presets) ---
        addFactoryPreset("Lo-Fi Tape Vocal");
        addFactoryPreset("Telephone / Radio");
        addFactoryPreset("Vintage Saturated");
        addFactoryPreset("Vinyl Crackle Vox");
        addFactoryPreset("Bitcrushed Glitch");

        // --- CREATIVE / FX (7 presets) ---
        addFactoryPreset("Robotic AutoTune");
        addFactoryPreset("Ethereal Wide");
        addFactoryPreset("Doubled & Thick");
        addFactoryPreset("Reverb Wash");
        addFactoryPreset("Slapback Echo");
        addFactoryPreset("Underwater Dream");
        addFactoryPreset("Cathedral Choir");

        // --- GENRE-SPECIFIC (5 presets) ---
        addFactoryPreset("Gospel Powerful");
        addFactoryPreset("Country Twang");
        addFactoryPreset("Latin Reggaeton");
        addFactoryPreset("Afrobeats Vocal");
        addFactoryPreset("EDM Festival Drop");

        // --- SIGNATURE / ARTIST (7 presets) ---
        addFactoryPreset("Drocett Smooth Melodies");
        addFactoryPreset("Drocett Trap Soul");
        addFactoryPreset("Drocett Late Night");
        addFactoryPreset("Drocett Falsetto Vibe");
        addFactoryPreset("Nu Rock Dry Scream");
        addFactoryPreset("Nu Rock Raw Edge");
        addFactoryPreset("Nu Rock Grit & Growl");

        // --- MIX-READY / UTILITY (5 presets) ---
        addFactoryPreset("Clean Vocal Chain");
        addFactoryPreset("Broadcast / Podcast");
        addFactoryPreset("Voiceover Warmth");
        addFactoryPreset("Live Performance");
        addFactoryPreset("Mastered Vocal Bus");
    }

    void addFactoryPreset (const juce::String& name)
    {
        allPresets.push_back(PresetData(name, "Factory"));
    }

    // -----------------------------------------------------------------------
    // Apply factory preset values to the APVTS
    // -----------------------------------------------------------------------
    void applyFactoryPreset (const juce::String& name)
    {
        // Helper to set a parameter value
        std::function<void(const juce::String&, float)> set =
            [this](const juce::String& paramId, float value) {
                if (auto* param = valueTreeState.getParameter(paramId))
                    param->setValueNotifyingHost(param->convertTo0to1(value));
            };

        std::function<void(const juce::String&, bool)> setBool =
            [&set](const juce::String& paramId, bool value) {
                set(paramId, value ? 1.0f : 0.0f);
            };

        // Reset everything to defaults first
        resetToDefaults(set, setBool);

        // --- FACTORY PRESET DEFINITIONS ---

        if (name == "Default (Init)")
        {
            // Already reset to defaults
        }
        else if (name == "Trap Hard AutoTune")
        {
            // Hard pitch correction, aggressive compression, bright EQ, short delay
            set("retuneSpeed", 0.95f);       // near-instant = robotic
            set("humanize", 0.05f);           // minimal humanize
            set("snapAmount", 1.0f);          // full snap
            set("pitchSustain", 0.3f);
            set("scaleType", 1.0f);           // minor
            setBool("noteStabilizer", true);

            set("eqHP", 120.0f);              // cut rumble
            set("eqBand2G", 2.0f);            // presence boost
            set("eqBand3G", 3.0f);            // high-mid bite
            set("eqBand4G", 2.5f);            // air

            set("compThreshold", -22.0f);     // aggressive compression
            set("compRatio", 6.0f);           // high ratio
            set("compAttack", 3.0f);          // fast attack
            set("compRelease", 40.0f);
            set("compKnee", 3.0f);            // hard knee
            set("thdMode", 2.0f);             // hard THD (transistor edge)

            setBool("satActive", true);
            set("satDrive", 0.25f);
            set("satMode", 0.0f);             // tube warmth
            set("satMix", 0.4f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            setBool("delayActive", true);
            set("delayTime", 180.0f);
            set("delayFeedback", 0.25f);
            set("delayMix", 0.15f);
            set("delayDuck", 0.6f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
            set("reverbLongMix", 0.08f);
            set("reverbDuck", 0.7f);

            set("limiterCeiling", -0.3f);
        }
        else if (name == "Trap Melodic")
        {
            // Melodic trap — moderate autotune, warm saturation, medium reverb
            set("retuneSpeed", 0.75f);
            set("humanize", 0.15f);
            set("snapAmount", 0.9f);
            set("scaleType", 1.0f);           // minor

            set("eqHP", 100.0f);
            set("eqBand2G", 1.5f);
            set("eqBand3G", 2.0f);
            set("eqBand4G", 3.0f);            // more air

            set("compThreshold", -20.0f);
            set("compRatio", 4.0f);
            set("compAttack", 5.0f);
            set("compRelease", 50.0f);
            set("thdMode", 1.0f);             // soft THD

            setBool("satActive", true);
            set("satDrive", 0.2f);
            set("satMode", 0.0f);
            set("satMix", 0.3f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.15f);
            set("reverbLongMix", 0.12f);
            set("reverbDuck", 0.5f);

            setBool("delayActive", true);
            set("delayTime", 330.0f);         // longer delay
            set("delayFeedback", 0.3f);
            set("delayMix", 0.18f);
            set("delayDuck", 0.5f);

            setBool("doublerActive", true);
            set("doublerMix", 0.2f);
            set("doublerDetune", 8.0f);
        }
        else if (name == "Trap Dark & Wet")
        {
            // Future/Young Thug style — dark, washed, heavy reverb
            set("retuneSpeed", 0.85f);
            set("humanize", 0.1f);
            set("snapAmount", 0.95f);
            set("scaleType", 1.0f);

            set("eqHP", 80.0f);
            set("eqLP", 14000.0f);            // roll off highs for dark tone
            set("eqBand1G", 2.0f);            // low warmth
            set("eqBand3G", -1.5f);           // scoop high-mids

            set("compThreshold", -24.0f);
            set("compRatio", 5.0f);
            set("compAttack", 3.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.35f);
            set("satMode", 1.0f);             // tape saturation
            set("satMix", 0.4f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.2f);
            set("reverbLongMix", 0.25f);      // heavy long verb
            set("reverbDuck", 0.4f);
            set("reverbPostEQ", 5000.0f);     // dark reverb tail

            setBool("delayActive", true);
            set("delayTime", 400.0f);
            set("delayFeedback", 0.4f);
            set("delayMix", 0.2f);
            set("delayFilter", 4000.0f);
        }
        else if (name == "Smooth R&B")
        {
            // SZA / Chris Brown style — warm, smooth, subtle correction
            set("retuneSpeed", 0.4f);         // natural correction
            set("humanize", 0.35f);
            set("snapAmount", 0.7f);
            set("scaleType", 0.0f);           // major

            set("eqHP", 70.0f);              // keep chest resonance
            set("eqBand1G", 1.5f);            // warmth
            set("eqBand2G", -1.0f);           // clean low-mids
            set("eqBand3G", 1.5f);            // presence
            set("eqBand4G", 2.0f);            // silk/air

            set("compThreshold", -16.0f);
            set("compRatio", 3.0f);
            set("compAttack", 8.0f);          // slower attack = natural
            set("compRelease", 80.0f);
            set("compKnee", 10.0f);           // soft knee
            set("thdMode", 1.0f);             // soft warmth

            setBool("satActive", true);
            set("satDrive", 0.15f);
            set("satMode", 0.0f);             // tube
            set("satMix", 0.25f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.18f);
            set("reverbLongMix", 0.1f);
            set("reverbDuck", 0.5f);

            setBool("widthActive", true);
            set("widthAmount", 1.2f);
            set("widthMode", 0.0f);           // M/S
        }
        else if (name == "R&B Warm Intimate")
        {
            // Close-mic intimate feel — Brent Faiyaz style
            set("retuneSpeed", 0.3f);
            set("humanize", 0.4f);
            set("snapAmount", 0.6f);

            set("eqHP", 60.0f);
            set("eqBand1G", 2.5f);            // chest warmth
            set("eqBand2G", -1.5f);           // mud cut
            set("eqBand3G", 1.0f);
            set("eqBand4G", 1.5f);

            set("compThreshold", -14.0f);
            set("compRatio", 2.5f);
            set("compAttack", 10.0f);
            set("compRelease", 100.0f);
            set("compKnee", 12.0f);

            setBool("satActive", true);
            set("satDrive", 0.2f);
            set("satMode", 0.0f);
            set("satMix", 0.3f);

            setBool("tapeActive", true);
            set("tapeFlutter", 0.15f);
            set("tapeDrive", 0.15f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.1f);
            set("reverbLongMix", 0.05f);
        }
        else if (name == "Neo Soul Vintage")
        {
            // Vintage warmth — Erykah Badu / D'Angelo
            set("retuneSpeed", 0.2f);
            set("humanize", 0.5f);
            set("snapAmount", 0.5f);

            set("eqHP", 80.0f);
            set("eqBand1G", 3.0f);
            set("eqBand2G", -2.0f);
            set("eqBand4G", -1.0f);           // roll off air for vintage

            set("compThreshold", -18.0f);
            set("compRatio", 3.0f);
            set("compAttack", 12.0f);
            set("compKnee", 10.0f);

            setBool("satActive", true);
            set("satDrive", 0.4f);
            set("satMode", 1.0f);             // tape saturation
            set("satMix", 0.5f);

            setBool("tapeActive", true);
            set("tapeSpeed", 15.0f);          // 15 IPS for more coloration
            set("tapeFlutter", 0.4f);
            set("tapeDrive", 0.3f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.2f);
            set("reverbLongMix", 0.08f);
        }
        else if (name == "Pop Radio Ready")
        {
            // Polished pop — Taylor Swift / Dua Lipa
            set("retuneSpeed", 0.6f);
            set("humanize", 0.2f);
            set("snapAmount", 0.85f);
            set("scaleType", 0.0f);

            set("eqHP", 100.0f);
            set("eqBand2G", -1.0f);           // clean mids
            set("eqBand3G", 2.5f);            // presence for clarity
            set("eqBand4G", 3.0f);            // sparkle/air

            set("compThreshold", -20.0f);
            set("compRatio", 4.0f);
            set("compAttack", 5.0f);
            set("compRelease", 50.0f);
            set("compKnee", 6.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);
            set("deEsserReduce", -10.0f);

            setBool("satActive", true);
            set("satDrive", 0.1f);
            set("satMode", 0.0f);
            set("satMix", 0.2f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.15f);
            set("reverbLongMix", 0.08f);
            set("reverbDuck", 0.6f);

            setBool("delayActive", true);
            set("delayTime", 250.0f);
            set("delayFeedback", 0.2f);
            set("delayMix", 0.1f);

            setBool("widthActive", true);
            set("widthAmount", 1.15f);
        }
        else if (name == "Pop Bright & Airy")
        {
            set("retuneSpeed", 0.55f);
            set("humanize", 0.25f);
            set("snapAmount", 0.8f);

            set("eqHP", 110.0f);
            set("eqBand3G", 3.0f);
            set("eqBand4G", 4.0f);            // lots of air

            set("compThreshold", -18.0f);
            set("compRatio", 3.5f);
            set("compAttack", 5.0f);
            set("compKnee", 8.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.2f);
            set("reverbLongMix", 0.15f);

            setBool("widthActive", true);
            set("widthAmount", 1.3f);

            setBool("doublerActive", true);
            set("doublerMix", 0.15f);
            set("doublerDetune", 6.0f);
        }
        else if (name == "Rock Aggressive")
        {
            // Screaming / aggressive rock vocal
            set("retuneSpeed", 0.3f);
            set("humanize", 0.3f);
            set("snapAmount", 0.6f);

            set("eqHP", 150.0f);              // tight low cut
            set("eqBand2G", 2.0f);            // mid grit
            set("eqBand3G", 3.0f);            // bite
            set("eqBand4G", 1.0f);

            set("compThreshold", -24.0f);
            set("compRatio", 8.0f);           // heavy compression
            set("compAttack", 2.0f);
            set("compRelease", 30.0f);
            set("compKnee", 2.0f);
            set("thdMode", 2.0f);             // hard THD

            setBool("satActive", true);
            set("satDrive", 0.5f);
            set("satMode", 2.0f);             // transformer
            set("satMix", 0.5f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -14.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.2f);
            set("reverbLongMix", 0.05f);

            setBool("delayActive", true);
            set("delayTime", 120.0f);         // slapback
            set("delayFeedback", 0.1f);
            set("delayMix", 0.12f);
        }
        else if (name == "Rock Warm Analog")
        {
            set("retuneSpeed", 0.25f);
            set("humanize", 0.4f);
            set("snapAmount", 0.5f);

            set("eqHP", 100.0f);
            set("eqBand1G", 2.0f);
            set("eqBand2G", 1.0f);
            set("eqBand3G", 1.5f);

            set("compThreshold", -18.0f);
            set("compRatio", 4.0f);
            set("compAttack", 8.0f);
            set("compKnee", 8.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.3f);
            set("satMode", 1.0f);             // tape
            set("satMix", 0.4f);

            setBool("tapeActive", true);
            set("tapeFlutter", 0.2f);
            set("tapeDrive", 0.25f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.2f);
            set("reverbLongMix", 0.1f);
        }
        else if (name == "Lo-Fi Tape Vocal")
        {
            set("retuneSpeed", 0.2f);
            set("humanize", 0.5f);

            set("eqHP", 200.0f);
            set("eqLP", 8000.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 4.0f);

            setBool("satActive", true);
            set("satDrive", 0.45f);
            set("satMode", 1.0f);
            set("satMix", 0.6f);

            setBool("tapeActive", true);
            set("tapeSpeed", 15.0f);
            set("tapeFlutter", 0.6f);
            set("tapeDrive", 0.4f);

            setBool("lofiActive", true);
            set("lofiHP", 300.0f);
            set("lofiLP", 4000.0f);
            set("lofiBits", 12.0f);
            set("lofiDS", 3.0f);
        }
        else if (name == "Telephone / Radio")
        {
            setBool("pitchActive", false);

            set("eqHP", 400.0f);
            set("eqLP", 3500.0f);

            set("compThreshold", -18.0f);
            set("compRatio", 6.0f);
            set("compAttack", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.3f);
            set("satMode", 2.0f);
            set("satMix", 0.5f);

            setBool("lofiActive", true);
            set("lofiHP", 400.0f);
            set("lofiLP", 3500.0f);
            set("lofiBits", 10.0f);
            set("lofiDS", 4.0f);
        }
        else if (name == "Vintage Saturated")
        {
            set("retuneSpeed", 0.15f);
            set("humanize", 0.5f);

            set("eqHP", 80.0f);
            set("eqLP", 12000.0f);
            set("eqBand1G", 3.0f);

            set("compThreshold", -16.0f);
            set("compRatio", 3.0f);
            set("compAttack", 10.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.55f);
            set("satMode", 0.0f);             // tube
            set("satMix", 0.6f);

            setBool("tapeActive", true);
            set("tapeSpeed", 15.0f);
            set("tapeFlutter", 0.35f);
            set("tapeDrive", 0.35f);
        }
        else if (name == "Robotic AutoTune")
        {
            set("retuneSpeed", 1.0f);         // maximum = full robotic
            set("humanize", 0.0f);
            set("snapAmount", 1.0f);
            set("pitchSustain", 0.1f);
            setBool("noteStabilizer", true);

            set("compThreshold", -22.0f);
            set("compRatio", 5.0f);
            set("compAttack", 3.0f);

            setBool("satActive", true);
            set("satDrive", 0.2f);
            set("satMode", 0.0f);
            set("satMix", 0.3f);
        }
        else if (name == "Ethereal Wide")
        {
            set("retuneSpeed", 0.4f);
            set("humanize", 0.3f);

            set("eqBand4G", 3.0f);

            set("compThreshold", -16.0f);
            set("compRatio", 2.5f);
            set("compKnee", 12.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.25f);
            set("reverbLongMix", 0.3f);
            set("reverbDuck", 0.3f);

            setBool("widthActive", true);
            set("widthAmount", 1.8f);
            set("widthMode", 2.0f);           // freq spread

            setBool("doublerActive", true);
            set("doublerMix", 0.25f);
            set("doublerDetune", 12.0f);
            set("doublerDelay", 30.0f);

            setBool("delayActive", true);
            set("delayTime", 500.0f);
            set("delayFeedback", 0.35f);
            set("delayMix", 0.15f);
        }
        else if (name == "Doubled & Thick")
        {
            set("retuneSpeed", 0.5f);
            set("humanize", 0.2f);

            set("compThreshold", -20.0f);
            set("compRatio", 4.0f);

            setBool("satActive", true);
            set("satDrive", 0.2f);
            set("satMix", 0.3f);

            setBool("doublerActive", true);
            set("doublerMix", 0.45f);
            set("doublerDetune", 15.0f);
            set("doublerDelay", 25.0f);

            setBool("widthActive", true);
            set("widthAmount", 1.4f);
        }
        else if (name == "Reverb Wash")
        {
            set("retuneSpeed", 0.35f);
            set("humanize", 0.3f);

            set("compThreshold", -16.0f);
            set("compRatio", 3.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.3f);
            set("reverbLongMix", 0.4f);       // very wet
            set("reverbLongDamp", 0.3f);       // less damping = longer tail
            set("reverbDuck", 0.2f);
            set("reverbPostEQ", 10000.0f);

            setBool("widthActive", true);
            set("widthAmount", 1.5f);
        }
        else if (name == "Slapback Echo")
        {
            set("retuneSpeed", 0.4f);
            set("humanize", 0.25f);

            set("compThreshold", -18.0f);
            set("compRatio", 3.5f);

            setBool("delayActive", true);
            set("delayTime", 80.0f);          // slapback (50-100ms)
            set("delayFeedback", 0.1f);
            set("delayMix", 0.25f);
            set("delayDuck", 0.3f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
        }
        else if (name == "Clean Vocal Chain")
        {
            // No effects, just EQ + comp + de-esser + limiter
            set("retuneSpeed", 0.35f);
            set("humanize", 0.3f);
            set("snapAmount", 0.7f);

            set("eqHP", 80.0f);
            set("eqBand2G", -1.0f);
            set("eqBand3G", 1.5f);
            set("eqBand4G", 2.0f);

            set("compThreshold", -18.0f);
            set("compRatio", 3.0f);
            set("compAttack", 8.0f);
            set("compRelease", 60.0f);
            set("compKnee", 8.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);
        }
        else if (name == "Broadcast / Podcast")
        {
            setBool("pitchActive", false);

            set("eqHP", 100.0f);
            set("eqLP", 16000.0f);
            set("eqBand2G", -2.0f);
            set("eqBand3G", 2.0f);

            set("compThreshold", -22.0f);
            set("compRatio", 5.0f);
            set("compAttack", 5.0f);
            set("compRelease", 40.0f);
            set("compKnee", 4.0f);
            setBool("compAutoGain", true);
            setBool("autoLevel", true);
            set("autoLevelTarget", -16.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -16.0f);

            set("limiterCeiling", -1.0f);
        }
        // === NEW TRAP PRESETS ===
        else if (name == "Trap Adlib Bright")
        {
            set("retuneSpeed", 0.9f);
            set("humanize", 0.05f);
            set("snapAmount", 0.95f);
            set("scaleType", 1.0f);

            set("eqHP", 200.0f);
            set("eqBand3G", 4.0f);
            set("eqBand4G", 5.0f);

            set("compThreshold", -26.0f);
            set("compRatio", 8.0f);
            set("compAttack", 1.0f);
            set("compRelease", 25.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.35f);
            set("satMode", 2.0f);
            set("satMix", 0.4f);

            setBool("delayActive", true);
            set("delayTime", 100.0f);
            set("delayFeedback", 0.15f);
            set("delayMix", 0.2f);
            set("delayDuck", 0.8f);
        }
        else if (name == "Trap Mumble Smooth")
        {
            set("retuneSpeed", 0.7f);
            set("humanize", 0.2f);
            set("snapAmount", 0.85f);
            set("scaleType", 1.0f);

            set("eqHP", 80.0f);
            set("eqLP", 15000.0f);
            set("eqBand1G", 2.5f);
            set("eqBand3G", 1.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 5.0f);
            set("compAttack", 4.0f);
            set("compKnee", 8.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.2f);
            set("satMode", 0.0f);
            set("satMix", 0.35f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.18f);
            set("reverbLongMix", 0.1f);
            set("reverbDuck", 0.6f);

            setBool("widthActive", true);
            set("widthAmount", 1.15f);
        }
        else if (name == "Drill Raw Vocal")
        {
            set("retuneSpeed", 0.6f);
            set("humanize", 0.15f);
            set("snapAmount", 0.75f);
            set("scaleType", 1.0f);

            set("eqHP", 150.0f);
            set("eqBand2G", 2.5f);
            set("eqBand3G", 3.5f);

            set("compThreshold", -28.0f);
            set("compRatio", 10.0f);
            set("compAttack", 1.0f);
            set("compRelease", 20.0f);
            set("compKnee", 1.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.45f);
            set("satMode", 2.0f);
            set("satMix", 0.5f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -14.0f);

            setBool("delayActive", true);
            set("delayTime", 150.0f);
            set("delayFeedback", 0.2f);
            set("delayMix", 0.12f);
        }
        else if (name == "Rage Beat Vocal")
        {
            set("retuneSpeed", 0.85f);
            set("humanize", 0.05f);
            set("snapAmount", 1.0f);
            set("scaleType", 1.0f);

            set("eqHP", 180.0f);
            set("eqBand2G", 3.0f);
            set("eqBand3G", 4.5f);
            set("eqBand4G", 2.0f);

            set("compThreshold", -30.0f);
            set("compRatio", 12.0f);
            set("compAttack", 0.5f);
            set("compRelease", 15.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.6f);
            set("satMode", 2.0f);
            set("satMix", 0.55f);

            setBool("widthActive", true);
            set("widthAmount", 1.5f);
            set("widthMode", 1.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.1f);
            set("reverbDuck", 0.8f);
        }
        else if (name == "808 Bass Vocal")
        {
            set("retuneSpeed", 0.8f);
            set("humanize", 0.1f);
            set("snapAmount", 0.9f);
            set("scaleType", 1.0f);

            set("eqHP", 60.0f);
            set("eqBand1G", 4.0f);
            set("eqBand2G", 1.0f);
            set("eqBand3G", -1.0f);
            set("eqLP", 12000.0f);

            set("compThreshold", -22.0f);
            set("compRatio", 6.0f);
            set("compAttack", 3.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.4f);
            set("satMode", 1.0f);
            set("satMix", 0.5f);

            setBool("tapeActive", true);
            set("tapeFlutter", 0.1f);
            set("tapeDrive", 0.3f);
        }
        // === NEW R&B PRESETS ===
        else if (name == "R&B Falsetto Air")
        {
            set("retuneSpeed", 0.35f);
            set("humanize", 0.35f);
            set("snapAmount", 0.7f);

            set("eqHP", 120.0f);
            set("eqBand3G", 2.0f);
            set("eqBand4G", 4.5f);

            set("compThreshold", -14.0f);
            set("compRatio", 2.5f);
            set("compAttack", 12.0f);
            set("compKnee", 14.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.2f);
            set("reverbLongMix", 0.15f);
            set("reverbDuck", 0.4f);

            setBool("widthActive", true);
            set("widthAmount", 1.4f);
            set("widthMode", 2.0f);

            setBool("doublerActive", true);
            set("doublerMix", 0.15f);
            set("doublerDetune", 5.0f);
        }
        else if (name == "90s R&B Classic")
        {
            set("retuneSpeed", 0.25f);
            set("humanize", 0.4f);
            set("snapAmount", 0.6f);

            set("eqHP", 60.0f);
            set("eqBand1G", 3.0f);
            set("eqBand2G", -1.5f);
            set("eqBand3G", 2.0f);
            set("eqLP", 14000.0f);

            set("compThreshold", -16.0f);
            set("compRatio", 3.0f);
            set("compAttack", 10.0f);
            set("compKnee", 10.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.25f);
            set("satMode", 0.0f);
            set("satMix", 0.35f);

            setBool("tapeActive", true);
            set("tapeSpeed", 30.0f);
            set("tapeFlutter", 0.2f);
            set("tapeDrive", 0.15f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.22f);
            set("reverbLongMix", 0.12f);

            setBool("delayActive", true);
            set("delayTime", 280.0f);
            set("delayFeedback", 0.2f);
            set("delayMix", 0.1f);
        }
        else if (name == "Bedroom R&B")
        {
            set("retuneSpeed", 0.45f);
            set("humanize", 0.3f);
            set("snapAmount", 0.75f);

            set("eqHP", 70.0f);
            set("eqBand1G", 2.0f);
            set("eqBand4G", 2.5f);

            set("compThreshold", -15.0f);
            set("compRatio", 2.5f);
            set("compAttack", 8.0f);
            set("compKnee", 10.0f);

            setBool("satActive", true);
            set("satDrive", 0.18f);
            set("satMode", 0.0f);
            set("satMix", 0.25f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.15f);
            set("reverbLongMix", 0.2f);
            set("reverbDuck", 0.35f);

            setBool("widthActive", true);
            set("widthAmount", 1.2f);

            setBool("doublerActive", true);
            set("doublerMix", 0.12f);
            set("doublerDetune", 6.0f);
        }
        // === NEW POP PRESETS ===
        else if (name == "Pop Ballad Lush")
        {
            set("retuneSpeed", 0.4f);
            set("humanize", 0.3f);
            set("snapAmount", 0.75f);

            set("eqHP", 80.0f);
            set("eqBand1G", 1.5f);
            set("eqBand3G", 2.0f);
            set("eqBand4G", 2.5f);

            set("compThreshold", -16.0f);
            set("compRatio", 3.0f);
            set("compAttack", 8.0f);
            set("compKnee", 10.0f);
            set("thdMode", 1.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.25f);
            set("reverbLongMix", 0.2f);
            set("reverbLongDamp", 0.35f);
            set("reverbDuck", 0.4f);

            setBool("delayActive", true);
            set("delayTime", 400.0f);
            set("delayFeedback", 0.25f);
            set("delayMix", 0.1f);

            setBool("widthActive", true);
            set("widthAmount", 1.3f);
        }
        else if (name == "K-Pop Crystal")
        {
            set("retuneSpeed", 0.7f);
            set("humanize", 0.1f);
            set("snapAmount", 0.9f);

            set("eqHP", 120.0f);
            set("eqBand3G", 3.5f);
            set("eqBand4G", 5.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 4.5f);
            set("compAttack", 3.0f);
            set("compRelease", 35.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -14.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.18f);
            set("reverbLongMix", 0.1f);

            setBool("widthActive", true);
            set("widthAmount", 1.25f);
            set("widthMode", 2.0f);

            setBool("doublerActive", true);
            set("doublerMix", 0.18f);
            set("doublerDetune", 8.0f);
        }
        else if (name == "Pop Punk Grit")
        {
            set("retuneSpeed", 0.35f);
            set("humanize", 0.25f);
            set("snapAmount", 0.65f);

            set("eqHP", 130.0f);
            set("eqBand2G", 2.5f);
            set("eqBand3G", 3.0f);
            set("eqBand4G", 1.5f);

            set("compThreshold", -24.0f);
            set("compRatio", 7.0f);
            set("compAttack", 2.0f);
            set("compRelease", 30.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.4f);
            set("satMode", 2.0f);
            set("satMix", 0.45f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -15.0f);

            setBool("delayActive", true);
            set("delayTime", 110.0f);
            set("delayFeedback", 0.12f);
            set("delayMix", 0.15f);
        }
        // === NEW ROCK PRESETS ===
        else if (name == "Metal Scream")
        {
            set("retuneSpeed", 0.15f);
            set("humanize", 0.5f);
            set("snapAmount", 0.4f);

            set("eqHP", 200.0f);
            set("eqBand2G", 4.0f);
            set("eqBand3G", 5.0f);
            set("eqBand4G", -1.0f);

            set("compThreshold", -30.0f);
            set("compRatio", 15.0f);
            set("compAttack", 0.5f);
            set("compRelease", 15.0f);
            set("compKnee", 1.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.7f);
            set("satMode", 2.0f);
            set("satMix", 0.65f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -12.0f);
            set("deEsserReduce", -16.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.15f);
            set("reverbDuck", 0.7f);
        }
        else if (name == "Indie Folk Natural")
        {
            set("retuneSpeed", 0.15f);
            set("humanize", 0.6f);
            set("snapAmount", 0.4f);

            set("eqHP", 80.0f);
            set("eqBand1G", 1.5f);
            set("eqBand3G", 1.0f);
            set("eqBand4G", 1.5f);

            set("compThreshold", -14.0f);
            set("compRatio", 2.0f);
            set("compAttack", 15.0f);
            set("compKnee", 14.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.2f);
            set("reverbLongMix", 0.1f);
        }
        // === NEW LO-FI PRESETS ===
        else if (name == "Vinyl Crackle Vox")
        {
            set("retuneSpeed", 0.2f);
            set("humanize", 0.5f);

            set("eqHP", 150.0f);
            set("eqLP", 10000.0f);
            set("eqBand1G", 3.0f);
            set("eqBand4G", -2.0f);

            set("compThreshold", -18.0f);
            set("compRatio", 3.5f);

            setBool("satActive", true);
            set("satDrive", 0.5f);
            set("satMode", 1.0f);
            set("satMix", 0.55f);

            setBool("tapeActive", true);
            set("tapeSpeed", 15.0f);
            set("tapeFlutter", 0.7f);
            set("tapeDrive", 0.45f);

            setBool("lofiActive", true);
            set("lofiHP", 250.0f);
            set("lofiLP", 5000.0f);
            set("lofiBits", 14.0f);
            set("lofiDS", 2.0f);
        }
        else if (name == "Bitcrushed Glitch")
        {
            set("retuneSpeed", 0.9f);
            set("humanize", 0.0f);
            set("snapAmount", 1.0f);

            set("eqHP", 200.0f);
            set("eqBand3G", 2.0f);

            set("compThreshold", -24.0f);
            set("compRatio", 8.0f);
            set("compAttack", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.5f);
            set("satMode", 2.0f);
            set("satMix", 0.6f);

            setBool("lofiActive", true);
            set("lofiHP", 350.0f);
            set("lofiLP", 4500.0f);
            set("lofiBits", 6.0f);
            set("lofiDS", 8.0f);

            setBool("delayActive", true);
            set("delayTime", 170.0f);
            set("delayFeedback", 0.45f);
            set("delayMix", 0.2f);
            setBool("delayPingPong", true);
        }
        // === NEW CREATIVE PRESETS ===
        else if (name == "Underwater Dream")
        {
            set("retuneSpeed", 0.5f);
            set("humanize", 0.25f);
            set("snapAmount", 0.7f);

            set("eqHP", 60.0f);
            set("eqLP", 8000.0f);
            set("eqBand1G", 3.0f);
            set("eqBand3G", -2.0f);
            set("eqBand4G", -3.0f);

            set("compThreshold", -16.0f);
            set("compRatio", 2.5f);
            set("compKnee", 12.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.35f);
            set("reverbLongMix", 0.4f);
            set("reverbLongDamp", 0.2f);
            set("reverbDuck", 0.15f);
            set("reverbPostEQ", 5000.0f);

            setBool("delayActive", true);
            set("delayTime", 600.0f);
            set("delayFeedback", 0.5f);
            set("delayMix", 0.2f);
            set("delayFilter", 3000.0f);

            setBool("widthActive", true);
            set("widthAmount", 1.8f);
            set("widthMode", 2.0f);

            set("dryWet", 0.85f);
        }
        else if (name == "Cathedral Choir")
        {
            set("retuneSpeed", 0.3f);
            set("humanize", 0.35f);
            set("snapAmount", 0.8f);

            set("eqBand1G", 2.0f);
            set("eqBand4G", 2.5f);

            set("compThreshold", -14.0f);
            set("compRatio", 2.0f);
            set("compKnee", 14.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.3f);
            set("reverbLongSize", 0.9f);
            set("reverbLongMix", 0.45f);
            set("reverbLongDamp", 0.2f);
            set("reverbDuck", 0.15f);

            setBool("doublerActive", true);
            set("doublerMix", 0.35f);
            set("doublerDetune", 10.0f);
            set("doublerDelay", 35.0f);

            setBool("widthActive", true);
            set("widthAmount", 1.6f);
            set("widthMode", 0.0f);

            setBool("delayActive", true);
            set("delayTime", 800.0f);
            set("delayFeedback", 0.3f);
            set("delayMix", 0.1f);
        }
        // === GENRE-SPECIFIC PRESETS ===
        else if (name == "Gospel Powerful")
        {
            set("retuneSpeed", 0.3f);
            set("humanize", 0.35f);
            set("snapAmount", 0.7f);
            set("scaleType", 0.0f);

            set("eqHP", 80.0f);
            set("eqBand1G", 2.5f);
            set("eqBand2G", -1.0f);
            set("eqBand3G", 3.0f);
            set("eqBand4G", 2.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 4.0f);
            set("compAttack", 5.0f);
            set("compRelease", 60.0f);
            set("compKnee", 6.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.15f);
            set("satMode", 0.0f);
            set("satMix", 0.25f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.25f);
            set("reverbLongMix", 0.2f);
            set("reverbDuck", 0.4f);

            setBool("widthActive", true);
            set("widthAmount", 1.3f);

            setBool("doublerActive", true);
            set("doublerMix", 0.2f);
            set("doublerDetune", 8.0f);
        }
        else if (name == "Country Twang")
        {
            set("retuneSpeed", 0.2f);
            set("humanize", 0.45f);
            set("snapAmount", 0.55f);
            set("scaleType", 0.0f);

            set("eqHP", 100.0f);
            set("eqBand1G", 1.0f);
            set("eqBand2G", 2.0f);
            set("eqBand3G", 3.0f);
            set("eqBand4G", 1.5f);

            set("compThreshold", -16.0f);
            set("compRatio", 3.0f);
            set("compAttack", 10.0f);
            set("compKnee", 8.0f);

            setBool("satActive", true);
            set("satDrive", 0.2f);
            set("satMode", 0.0f);
            set("satMix", 0.3f);

            setBool("tapeActive", true);
            set("tapeFlutter", 0.15f);
            set("tapeDrive", 0.15f);

            setBool("delayActive", true);
            set("delayTime", 200.0f);
            set("delayFeedback", 0.15f);
            set("delayMix", 0.12f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.18f);
            set("reverbLongMix", 0.08f);
        }
        else if (name == "Latin Reggaeton")
        {
            set("retuneSpeed", 0.65f);
            set("humanize", 0.15f);
            set("snapAmount", 0.85f);
            set("scaleType", 1.0f);

            set("eqHP", 100.0f);
            set("eqBand1G", 2.0f);
            set("eqBand2G", 1.0f);
            set("eqBand3G", 2.5f);
            set("eqBand4G", 2.0f);

            set("compThreshold", -20.0f);
            set("compRatio", 4.5f);
            set("compAttack", 4.0f);
            set("compRelease", 40.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.2f);
            set("satMode", 0.0f);
            set("satMix", 0.3f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.15f);
            set("reverbLongMix", 0.08f);
            set("reverbDuck", 0.6f);

            setBool("delayActive", true);
            set("delayTime", 330.0f);
            set("delayFeedback", 0.25f);
            set("delayMix", 0.15f);

            setBool("widthActive", true);
            set("widthAmount", 1.2f);
        }
        else if (name == "Afrobeats Vocal")
        {
            set("retuneSpeed", 0.5f);
            set("humanize", 0.25f);
            set("snapAmount", 0.75f);
            set("scaleType", 0.0f);

            set("eqHP", 90.0f);
            set("eqBand1G", 1.5f);
            set("eqBand3G", 2.5f);
            set("eqBand4G", 3.0f);

            set("compThreshold", -18.0f);
            set("compRatio", 3.5f);
            set("compAttack", 6.0f);
            set("compRelease", 55.0f);
            set("compKnee", 8.0f);

            setBool("satActive", true);
            set("satDrive", 0.15f);
            set("satMode", 0.0f);
            set("satMix", 0.2f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.2f);
            set("reverbLongMix", 0.12f);
            set("reverbDuck", 0.5f);

            setBool("delayActive", true);
            set("delayTime", 375.0f);
            set("delayFeedback", 0.2f);
            set("delayMix", 0.12f);

            setBool("doublerActive", true);
            set("doublerMix", 0.15f);
            set("doublerDetune", 7.0f);
        }
        else if (name == "EDM Festival Drop")
        {
            set("retuneSpeed", 0.85f);
            set("humanize", 0.05f);
            set("snapAmount", 0.95f);

            set("eqHP", 150.0f);
            set("eqBand3G", 3.0f);
            set("eqBand4G", 4.0f);

            set("compThreshold", -26.0f);
            set("compRatio", 8.0f);
            set("compAttack", 1.0f);
            set("compRelease", 20.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.4f);
            set("satMode", 2.0f);
            set("satMix", 0.45f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.2f);
            set("reverbLongMix", 0.15f);
            set("reverbDuck", 0.7f);

            setBool("delayActive", true);
            set("delayTime", 375.0f);
            set("delayFeedback", 0.35f);
            set("delayMix", 0.18f);
            setBool("delayPingPong", true);

            setBool("widthActive", true);
            set("widthAmount", 1.7f);
            set("widthMode", 1.0f);

            setBool("doublerActive", true);
            set("doublerMix", 0.3f);
            set("doublerDetune", 15.0f);

            set("limiterCeiling", -0.1f);
        }
        // === NEW UTILITY PRESETS ===
        else if (name == "Voiceover Warmth")
        {
            setBool("pitchActive", false);

            set("eqHP", 80.0f);
            set("eqLP", 14000.0f);
            set("eqBand1G", 2.5f);
            set("eqBand2G", -2.0f);
            set("eqBand3G", 1.5f);

            set("compThreshold", -18.0f);
            set("compRatio", 3.5f);
            set("compAttack", 8.0f);
            set("compRelease", 80.0f);
            set("compKnee", 8.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.15f);
            set("satMode", 0.0f);
            set("satMix", 0.25f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -18.0f);

            set("limiterCeiling", -1.0f);
        }
        else if (name == "Live Performance")
        {
            set("retuneSpeed", 0.6f);
            set("humanize", 0.2f);
            set("snapAmount", 0.8f);

            set("eqHP", 120.0f);
            set("eqBand2G", -1.5f);
            set("eqBand3G", 2.5f);
            set("eqBand4G", 1.5f);

            set("compThreshold", -22.0f);
            set("compRatio", 5.0f);
            set("compAttack", 3.0f);
            set("compRelease", 35.0f);
            set("compKnee", 4.0f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -15.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.12f);
            set("reverbDuck", 0.7f);

            set("limiterCeiling", -0.5f);
        }
        else if (name == "Mastered Vocal Bus")
        {
            set("retuneSpeed", 0.4f);
            set("humanize", 0.25f);
            set("snapAmount", 0.75f);

            set("eqHP", 80.0f);
            set("eqBand1G", 1.0f);
            set("eqBand2G", -1.0f);
            set("eqBand3G", 1.5f);
            set("eqBand4G", 2.0f);

            set("compThreshold", -18.0f);
            set("compRatio", 3.0f);
            set("compAttack", 5.0f);
            set("compRelease", 50.0f);
            set("compKnee", 6.0f);
            setBool("compAutoGain", true);
            setBool("autoLevel", true);
            set("autoLevelTarget", -14.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.1f);
            set("satMode", 0.0f);
            set("satMix", 0.2f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -17.0f);

            setBool("widthActive", true);
            set("widthAmount", 1.1f);

            set("limiterCeiling", -0.3f);
        }
        // === SIGNATURE / ARTIST PRESETS ===
        else if (name == "Drocett Smooth Melodies")
        {
            // Smooth melodic autotune — silky R&B/trap fusion, warm + wide
            set("retuneSpeed", 0.6f);
            set("humanize", 0.2f);
            set("snapAmount", 0.85f);
            set("scaleType", 1.0f);           // minor for melodic feel

            set("eqHP", 80.0f);
            set("eqBand1G", 2.0f);            // chest warmth
            set("eqBand2G", -0.5f);
            set("eqBand3G", 2.0f);            // vocal presence
            set("eqBand4G", 3.0f);            // shimmer/air

            set("compThreshold", -18.0f);
            set("compRatio", 3.5f);
            set("compAttack", 6.0f);
            set("compRelease", 55.0f);
            set("compKnee", 8.0f);
            set("thdMode", 1.0f);             // soft warmth

            setBool("satActive", true);
            set("satDrive", 0.18f);
            set("satMode", 0.0f);             // tube
            set("satMix", 0.3f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.18f);
            set("reverbLongMix", 0.12f);
            set("reverbDuck", 0.5f);

            setBool("widthActive", true);
            set("widthAmount", 1.25f);
            set("widthMode", 0.0f);

            setBool("delayActive", true);
            set("delayTime", 300.0f);
            set("delayFeedback", 0.2f);
            set("delayMix", 0.1f);
            set("delayDuck", 0.6f);
        }
        else if (name == "Drocett Trap Soul")
        {
            // Heavier autotune, trap soul bounce — thicker compression, tape warmth
            set("retuneSpeed", 0.75f);
            set("humanize", 0.12f);
            set("snapAmount", 0.92f);
            set("scaleType", 1.0f);

            set("eqHP", 100.0f);
            set("eqBand1G", 1.5f);
            set("eqBand3G", 2.5f);
            set("eqBand4G", 2.0f);

            set("compThreshold", -22.0f);
            set("compRatio", 5.0f);
            set("compAttack", 4.0f);
            set("compRelease", 40.0f);
            set("thdMode", 1.0f);

            setBool("satActive", true);
            set("satDrive", 0.25f);
            set("satMode", 0.0f);
            set("satMix", 0.35f);

            setBool("tapeActive", true);
            set("tapeFlutter", 0.1f);
            set("tapeDrive", 0.15f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.15f);
            set("reverbLongMix", 0.1f);
            set("reverbDuck", 0.55f);

            setBool("doublerActive", true);
            set("doublerMix", 0.12f);
            set("doublerDetune", 7.0f);

            setBool("delayActive", true);
            set("delayTime", 220.0f);
            set("delayFeedback", 0.18f);
            set("delayMix", 0.12f);
        }
        else if (name == "Drocett Late Night")
        {
            // Dark, intimate late-night vibe — subdued highs, lush reverb, subtle autotune
            set("retuneSpeed", 0.45f);
            set("humanize", 0.3f);
            set("snapAmount", 0.7f);
            set("scaleType", 1.0f);

            set("eqHP", 70.0f);
            set("eqLP", 13000.0f);            // roll off for dark vibe
            set("eqBand1G", 2.5f);            // warmth
            set("eqBand2G", -1.0f);
            set("eqBand3G", 1.0f);

            set("compThreshold", -16.0f);
            set("compRatio", 3.0f);
            set("compAttack", 10.0f);
            set("compKnee", 10.0f);

            setBool("satActive", true);
            set("satDrive", 0.2f);
            set("satMode", 1.0f);             // tape warmth
            set("satMix", 0.35f);

            setBool("tapeActive", true);
            set("tapeFlutter", 0.2f);
            set("tapeDrive", 0.2f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.2f);
            set("reverbLongMix", 0.2f);
            set("reverbDuck", 0.35f);
            set("reverbPostEQ", 6000.0f);     // dark reverb tail

            setBool("widthActive", true);
            set("widthAmount", 1.3f);
        }
        else if (name == "Drocett Falsetto Vibe")
        {
            // Airy falsetto — open highs, crystal reverb, gentle correction
            set("retuneSpeed", 0.5f);
            set("humanize", 0.25f);
            set("snapAmount", 0.8f);

            set("eqHP", 130.0f);
            set("eqBand3G", 2.0f);
            set("eqBand4G", 4.5f);            // max air for falsetto

            set("compThreshold", -14.0f);
            set("compRatio", 2.5f);
            set("compAttack", 12.0f);
            set("compKnee", 12.0f);

            setBool("reverbActive", true);
            set("reverbShortMix", 0.22f);
            set("reverbLongMix", 0.18f);
            set("reverbDuck", 0.4f);

            setBool("widthActive", true);
            set("widthAmount", 1.5f);
            set("widthMode", 2.0f);           // freq spread

            setBool("doublerActive", true);
            set("doublerMix", 0.18f);
            set("doublerDetune", 6.0f);
            set("doublerDelay", 20.0f);

            setBool("delayActive", true);
            set("delayTime", 450.0f);
            set("delayFeedback", 0.25f);
            set("delayMix", 0.12f);
        }
        else if (name == "Nu Rock Dry Scream")
        {
            // Bone-dry aggressive rock scream — no reverb, no delay, pure grit
            set("retuneSpeed", 0.1f);         // minimal correction
            set("humanize", 0.6f);            // raw human feel
            set("snapAmount", 0.3f);

            set("eqHP", 180.0f);              // tight low cut
            set("eqLP", 16000.0f);
            set("eqBand2G", 3.5f);            // mid grit
            set("eqBand3G", 5.0f);            // aggressive bite
            set("eqBand4G", -0.5f);           // tame fizz

            set("compThreshold", -28.0f);
            set("compRatio", 12.0f);          // brick wall
            set("compAttack", 0.5f);
            set("compRelease", 15.0f);
            set("compKnee", 1.0f);
            set("thdMode", 2.0f);             // hard THD

            setBool("satActive", true);
            set("satDrive", 0.65f);
            set("satMode", 2.0f);             // transformer crunch
            set("satMix", 0.6f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -12.0f);
            set("deEsserReduce", -16.0f);

            // No reverb, no delay — bone dry
        }
        else if (name == "Nu Rock Raw Edge")
        {
            // Raw rock with just a touch of room — post-punk energy
            set("retuneSpeed", 0.15f);
            set("humanize", 0.5f);
            set("snapAmount", 0.35f);

            set("eqHP", 160.0f);
            set("eqBand2G", 2.5f);
            set("eqBand3G", 4.0f);
            set("eqBand4G", 1.0f);

            set("compThreshold", -26.0f);
            set("compRatio", 8.0f);
            set("compAttack", 1.0f);
            set("compRelease", 20.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.5f);
            set("satMode", 2.0f);
            set("satMix", 0.5f);

            setBool("deEsserActive", true);
            set("deEsserThresh", -13.0f);

            // Tiny room only — dry feel
            setBool("reverbActive", true);
            set("reverbShortSize", 0.15f);
            set("reverbShortMix", 0.08f);
            set("reverbDuck", 0.8f);
        }
        else if (name == "Nu Rock Grit & Growl")
        {
            // Maximum saturation growl — tape + transformer stacked
            set("retuneSpeed", 0.12f);
            set("humanize", 0.55f);
            set("snapAmount", 0.3f);

            set("eqHP", 200.0f);
            set("eqBand1G", -1.0f);
            set("eqBand2G", 4.0f);            // mid growl
            set("eqBand3G", 3.5f);

            set("compThreshold", -30.0f);
            set("compRatio", 15.0f);
            set("compAttack", 0.5f);
            set("compRelease", 12.0f);
            set("compKnee", 1.0f);
            set("thdMode", 2.0f);

            setBool("satActive", true);
            set("satDrive", 0.75f);           // heavy drive
            set("satMode", 2.0f);
            set("satMix", 0.7f);

            setBool("tapeActive", true);
            set("tapeSpeed", 15.0f);
            set("tapeFlutter", 0.1f);
            set("tapeDrive", 0.5f);           // tape cranked

            setBool("deEsserActive", true);
            set("deEsserThresh", -10.0f);
            set("deEsserReduce", -18.0f);

            // Slapback only
            setBool("delayActive", true);
            set("delayTime", 90.0f);
            set("delayFeedback", 0.08f);
            set("delayMix", 0.1f);
        }
    }

    // -----------------------------------------------------------------------
    // Reset all params to safe defaults
    // -----------------------------------------------------------------------
    void resetToDefaults (const std::function<void(const juce::String&, float)>& set,
                          const std::function<void(const juce::String&, bool)>& setBool)
    {
        // Pitch
        setBool("pitchActive", true);
        set("retuneSpeed", 0.5f);
        set("humanize", 0.2f);
        set("snapAmount", 0.8f);
        set("pitchSustain", 0.5f);
        set("detune", 440.0f);
        set("rootNote", 0.0f);
        set("scaleType", 0.0f);
        setBool("noteStabilizer", true);
        setBool("formantPreserve", true);

        // EQ
        setBool("eqActive", true);
        set("eqHP", 80.0f);
        set("eqLP", 18000.0f);
        set("eqBand1F", 200.0f); set("eqBand1G", 0.0f);
        set("eqBand2F", 800.0f); set("eqBand2G", 0.0f);
        set("eqBand3F", 3000.0f); set("eqBand3G", 0.0f);
        set("eqBand4F", 10000.0f); set("eqBand4G", 0.0f);

        // Comp
        setBool("compActive", true);
        set("compThreshold", -18.0f);
        set("compRatio", 4.0f);
        set("compAttack", 5.0f);
        set("compRelease", 50.0f);
        set("compMakeup", 0.0f);
        set("compKnee", 6.0f);
        setBool("compAutoGain", true);
        setBool("autoLevel", false);
        set("autoLevelTarget", -14.0f);
        set("thdMode", 0.0f);

        // De-Esser
        setBool("deEsserActive", true);
        set("deEsserFreq", 7000.0f);
        set("deEsserThresh", -20.0f);
        set("deEsserReduce", -12.0f);

        // Saturation
        setBool("satActive", false);
        set("satDrive", 0.3f);
        set("satMode", 0.0f);
        set("satMix", 0.5f);

        // Tape
        setBool("tapeActive", false);
        set("tapeSpeed", 30.0f);
        set("tapeFlutter", 0.3f);
        set("tapeDrive", 0.2f);

        // Width
        setBool("widthActive", false);
        set("widthAmount", 1.0f);
        set("widthMode", 0.0f);

        // Doubler
        setBool("doublerActive", false);
        set("doublerMix", 0.5f);
        set("doublerDetune", 10.0f);
        set("doublerDelay", 20.0f);

        // Reverb
        setBool("reverbActive", false);
        set("reverbShortSize", 0.3f);
        set("reverbShortDamp", 0.5f);
        set("reverbShortMix", 0.2f);
        set("reverbLongSize", 0.7f);
        set("reverbLongDamp", 0.4f);
        set("reverbLongMix", 0.15f);
        set("reverbDuck", 0.5f);
        set("reverbPostEQ", 8000.0f);

        // Delay
        setBool("delayActive", false);
        set("delayTime", 250.0f);
        set("delayFeedback", 0.3f);
        set("delayMix", 0.2f);
        set("delayDuck", 0.5f);
        set("delayFilter", 6000.0f);
        setBool("delayPingPong", false);

        // Lo-Fi
        setBool("lofiActive", false);
        set("lofiHP", 400.0f);
        set("lofiLP", 3500.0f);
        set("lofiBits", 32.0f);
        set("lofiDS", 1.0f);

        // Limiter
        setBool("limiterActive", true);
        set("limiterCeiling", -0.3f);
        set("limiterRelease", 50.0f);

        // Master
        set("inputGain", 0.0f);
        set("outputGain", 0.0f);
        set("dryWet", 1.0f);
    }
};

} // namespace humvocal
