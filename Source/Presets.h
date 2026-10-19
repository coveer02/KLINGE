#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace klinge
{
struct FactoryMod { int source; const char* target; float depth; };

struct FactoryPreset
{
    const char* name;
    const char* category;
    const char* tags;                                     // comma separated
    std::vector<std::pair<juce::String, float>> values;   // parameter id -> value in real units
    std::vector<FactoryMod> mods {};
    int shaperPreset = -1;                                // index into ShaperCurve::preset, -1 = default
};

const std::vector<FactoryPreset>& factoryPresets();

struct PresetEntry
{
    juce::String name, category;
    juce::StringArray tags;
    bool factory = true;
    int factoryIndex = -1;
    juce::File file;
};

class PresetManager
{
public:
    static juce::File userFolder();
    static juce::File userPresetFolder() { return userFolder().getChildFile ("Presets"); }

    std::vector<PresetEntry> list() const;                // factory first, then user presets
    juce::StringArray getAllNames() const;
    juce::ValueTree soundFor (const PresetEntry& e) const; // KLINGESOUND tree
    bool saveUser (const juce::String& name, const juce::String& tags, const juce::ValueTree& sound);

    juce::String getCurrentName() const          { const juce::SpinLock::ScopedLockType l (nameLock); return currentName; }
    void setCurrentName (const juce::String& n)  { const juce::SpinLock::ScopedLockType l (nameLock); currentName = n; }

    // favourites (stored in Documents/KLINGE/favorites.txt)
    bool isFavourite (const juce::String& name) const;
    void setFavourite (const juce::String& name, bool fav);

private:
    void loadFavourites() const;
    mutable juce::SpinLock nameLock;
    juce::String currentName { "Init" };
    mutable juce::StringArray favourites;
    mutable bool favouritesLoaded = false;
};

} // namespace klinge
