#include "Presets.h"

namespace klinge
{
// Algorithms: 0 Silk, 1 Tube, 2 Tape, 3 Detail, 4 Amp, 5 Grind, 6 Fuzz, 7 Fold, 8 Rectify, 9 Crush, 10 Slew,
//             11 Triode, 12 Diode, 13 Hysteresis, 14 Iron, 15 Stack
// Routing: 0 Single, 1 Multiband, 2 Mid/Side.  Mod sources: 1 LFO 1, 2 LFO 2, 3 Env 1, 4 Env 2, 5 Shaper, 6 Velocity, 7 Mod wheel
// Divisions: 3 = 1 bar, 4 = 1/2, 5 = 1/4, 6 = 1/8, 7 = 1/16
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> list {
        { "Init", "Basic", "", {} },

        // ---------------- Techno / Schranz
        { "Schranz Perc Crunch", "Techno", "schranz,perc,hard",
          { { "b1_type", 4 }, { "drive", 30 }, { "b1_shape", 35 }, { "focus", 4 }, { "dyn_drive", -30 },
            { "punch", 80 }, { "punch_speed", 25 }, { "tone_lock", 30 }, { "post_lp", 9000 } } },
        { "Woosh Restore", "Techno", "schranz,perc,reverb",
          { { "b1_type", 4 }, { "drive", 36 }, { "b1_shape", 50 }, { "pre_hp", 150 }, { "dyn_drive", -50 },
            { "punch", 110 }, { "punch_speed", 18 }, { "tone_lock", 40 }, { "post_lp", 8000 }, { "mix", 75 } } },
        { "Woosh Shaper", "Techno", "schranz,rhythmic,reverb,shaper",
          { { "b1_type", 15 }, { "drive", 30 }, { "b1_shape", 35 }, { "pre_hp", 150 }, { "dyn_drive", -40 },
            { "punch", 60 }, { "punch_speed", 20 }, { "tone_lock", 30 }, { "post_lp", 9000 },
            { "shp_amount", 85 }, { "shp_rate", 5 }, { "shp_smooth", 2 } }, {}, 1 },
        { "Schranz Stack", "Techno", "schranz,amp,hard",
          { { "b1_type", 15 }, { "drive", 34 }, { "b1_shape", 30 }, { "focus", 3 }, { "dyn_drive", -30 },
            { "punch", 70 }, { "punch_speed", 25 }, { "tone_lock", 25 } } },
        { "Kick Drive Low Only", "Techno", "kick,multiband",
          { { "routing", 1 }, { "xover_lo", 180 }, { "xover_hi", 2500 }, { "drive", 6 },
            { "b1_type", 1 }, { "b1_drive", 14 }, { "b1_shape", 60 },
            { "b2_type", 3 }, { "b2_drive", 2 }, { "b2_shape", 70 },
            { "b3_type", 0 }, { "b3_mix", 0 }, { "punch", 40 } } },
        { "Kick Duck Sidechain", "Techno", "sidechain,ducking,kick",
          { { "b1_type", 4 }, { "drive", 24 }, { "punch", 40 },
            { "env1_src", 1 }, { "env1_att", 1 }, { "env1_rel", 160 }, { "env1_gain", 12 } },
          { { 3, "mix", -0.9f } } },
        { "Rumble Grind", "Techno", "rumble,bass,multiband",
          { { "routing", 1 }, { "xover_lo", 120 }, { "xover_hi", 2800 }, { "drive", 12 },
            { "b1_type", 5 }, { "b1_drive", 12 }, { "b1_shape", 30 },
            { "b2_type", 4 }, { "b2_drive", 6 }, { "b2_shape", 40 },
            { "b3_type", 0 }, { "b3_mix", 50 }, { "punch", 50 }, { "tone_lock", 25 } } },
        { "Hats Grit Side", "Techno", "hats,stereo",
          { { "routing", 2 }, { "drive", 6 }, { "b1_type", 0 }, { "b1_drive", -6 }, { "b1_mix", 50 },
            { "b2_type", 5 }, { "b2_drive", 12 }, { "b2_shape", 50 }, { "post_lp", 14000 } } },
        { "Gated Stutter", "Techno", "rhythmic,shaper,gate",
          { { "b1_type", 3 }, { "drive", 18 }, { "b1_shape", 70 }, { "shp_amount", 100 }, { "shp_rate", 3 }, { "shp_smooth", 1.5f } },
          {}, 8 },
        { "Fuzz Sputter", "Techno", "fuzz,gate",
          { { "b1_type", 6 }, { "drive", 28 }, { "b1_shape", 60 }, { "mix", 80 }, { "post_lp", 11000 } } },

        // ---------------- Mixing
        { "Warm Bus Glue", "Mixing", "bus,subtle",
          { { "b1_type", 0 }, { "drive", 6 }, { "b1_shape", 40 }, { "tone_lock", 60 } } },
        { "Analog Warmth", "Mixing", "bus,subtle,analog",
          { { "b1_type", 0 }, { "drive", 8 }, { "b1_shape", 50 }, { "analog", 60 }, { "tone_lock", 40 } } },
        { "Tape Kiss", "Mixing", "tape,subtle",
          { { "b1_type", 2 }, { "drive", 10 }, { "b1_shape", 35 }, { "tone_lock", 40 } } },
        { "Tape Hysteresis Glue", "Mixing", "tape,circuit,bus",
          { { "b1_type", 13 }, { "drive", 10 }, { "b1_shape", 40 }, { "tone_lock", 50 } } },
        { "Iron Bass Weight", "Mixing", "transformer,bass,circuit",
          { { "b1_type", 14 }, { "drive", 14 }, { "b1_shape", 30 }, { "tone_lock", 20 } } },
        { "Tube Bloom", "Mixing", "tube,drums",
          { { "b1_type", 1 }, { "drive", 20 }, { "b1_shape", 70 }, { "b1_bias", 20 }, { "dyn_drive", 40 } } },
        { "Triode Bloom Drums", "Mixing", "tube,circuit,drums",
          { { "b1_type", 11 }, { "drive", 24 }, { "b1_shape", 70 }, { "punch", 40 } } },
        { "Detail Clipper Loud", "Mixing", "clipper,master,loud",
          { { "b1_type", 3 }, { "drive", 14 }, { "b1_shape", 80 }, { "auto_gain", 0 }, { "out_gain", -1 },
            { "clip_out", 1 } } },
        { "Vocal Presence", "Mixing", "vocal,multiband",
          { { "routing", 1 }, { "xover_lo", 300 }, { "xover_hi", 4000 }, { "drive", 8 },
            { "b1_type", 0 }, { "b1_mix", 0 }, { "b2_type", 1 }, { "b2_shape", 40 },
            { "b3_type", 2 }, { "b3_drive", -2 }, { "tone_lock", 50 } } },
        { "Five Band Master", "Mixing", "master,multiband,subtle",
          { { "routing", 1 }, { "num_bands", 3 }, { "xover_lo", 90 }, { "xover_hi", 400 }, { "xover_3", 2500 }, { "xover_4", 9000 },
            { "drive", 4 }, { "b1_type", 14 }, { "b2_type", 13 }, { "b3_type", 0 }, { "b4_type", 2 }, { "b5_type", 0 },
            { "b5_mix", 50 }, { "analog", 30 }, { "tone_lock", 50 } } },
        { "Mid Grit Side Air", "Mixing", "stereo,multiband",
          { { "routing", 1 }, { "xover_lo", 200 }, { "xover_hi", 4000 }, { "drive", 8 },
            { "b1_type", 0 }, { "b1_mix", 0 }, { "b2_type", 3 }, { "b2_stereo", 1 }, { "b2_drive", 8 },
            { "b3_type", 0 }, { "b3_stereo", 2 }, { "b3_drive", 4 } } },

        // ---------------- Harmonics
        { "Harmonic Gold", "Harmonics", "harmonics,subtle",
          { { "b1_type", 0 }, { "drive", 0 }, { "h2", 10 }, { "h3", 6 }, { "h4", 2 }, { "harm_lock", 100 } } },
        { "Odd Steel", "Harmonics", "harmonics,bright",
          { { "b1_type", 3 }, { "drive", 6 }, { "h3", 14 }, { "h5", 9 }, { "h7", 5 }, { "harm_lock", 80 } } },
        { "Analog Even", "Harmonics", "harmonics,warm",
          { { "b1_type", 0 }, { "drive", 4 }, { "h2", 40 }, { "h4", 12 }, { "harm_lock", 0 } } },

        // ---------------- Sound design
        { "Diode Pedal Grit", "Design", "pedal,circuit",
          { { "b1_type", 12 }, { "drive", 30 }, { "b1_shape", 40 }, { "post_lp", 12000 } } },
        { "LFO Fold Wobble", "Design", "lfo,fold,movement",
          { { "b1_type", 7 }, { "drive", 14 }, { "b1_shape", 40 }, { "lfo1_div", 6 }, { "lfo1_shape", 0 } },
          { { 1, "b1_shape", 0.4f }, { 1, "drive", 0.12f } } },
        { "Metal Feedback", "Design", "feedback,metallic",
          { { "b1_type", 5 }, { "drive", 20 }, { "b1_shape", 40 }, { "fb_amount", 70 }, { "fb_time", 1.5f }, { "fb_tone", 5000 },
            { "mix", 70 } } },
        { "Velocity Drive", "Design", "midi,dynamic",
          { { "b1_type", 4 }, { "drive", 12 } },
          { { 6, "drive", 0.4f } } },
        { "Fold Bass Growl", "Design", "fold,bass,multiband",
          { { "routing", 1 }, { "xover_lo", 250 }, { "drive", 8 }, { "b1_type", 7 }, { "b1_drive", 14 }, { "b1_shape", 30 },
            { "b2_type", 0 }, { "b3_type", 0 }, { "b3_mix", 60 } } },
        { "Lo-Fi Crush", "Design", "lofi,crush",
          { { "b1_type", 9 }, { "drive", 30 }, { "b1_shape", 50 }, { "post_lp", 12000 }, { "mix", 60 } } },
        { "Octave Rectify", "Design", "octave",
          { { "b1_type", 8 }, { "drive", 12 }, { "b1_shape", 100 }, { "mix", 40 } } },
        { "Slew Smear", "Design", "smear",
          { { "b1_type", 10 }, { "drive", 18 }, { "b1_shape", 70 } } },
    };
    return list;
}

// ===========================================================================
juce::File PresetManager::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("KLINGE");
}

std::vector<PresetEntry> PresetManager::list() const
{
    std::vector<PresetEntry> r;
    const auto& fac = factoryPresets();
    for (size_t i = 0; i < fac.size(); ++i)
    {
        PresetEntry e;
        e.name = fac[i].name;
        e.category = fac[i].category;
        e.tags = juce::StringArray::fromTokens (fac[i].tags, ",", "");
        e.tags.removeEmptyStrings();
        e.factory = true;
        e.factoryIndex = (int) i;
        r.push_back (e);
    }
    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.klinge");
    files.sort();
    for (auto& f : files)
    {
        PresetEntry e;
        e.name = f.getFileNameWithoutExtension();
        e.category = "User";
        e.factory = false;
        e.file = f;
        if (auto xml = juce::XmlDocument::parse (f))
        {
            e.tags = juce::StringArray::fromTokens (xml->getStringAttribute ("tags"), ",", "");
            e.tags.trim();
            e.tags.removeEmptyStrings();
        }
        r.push_back (e);
    }
    return r;
}

juce::StringArray PresetManager::getAllNames() const
{
    juce::StringArray names;
    for (auto& e : list()) names.add (e.name);
    return names;
}

juce::ValueTree PresetManager::soundFor (const PresetEntry& e) const
{
    juce::ValueTree sound ("KLINGESOUND");
    if (e.factory)
    {
        const auto& fac = factoryPresets();
        if (e.factoryIndex < 0 || e.factoryIndex >= (int) fac.size()) return {};
        const auto& p = fac[(size_t) e.factoryIndex];
        for (auto& [id, v] : p.values)
        {
            juce::ValueTree pt ("PARAM");
            pt.setProperty ("id", id, nullptr);
            pt.setProperty ("value", v, nullptr);
            sound.appendChild (pt, nullptr);
        }
        juce::ValueTree mod ("MOD");
        int slot = 0;
        for (auto& m : p.mods)
        {
            juce::ValueTree s ("SLOT");
            s.setProperty ("i", slot++, nullptr);
            static const juce::StringArray srcNames { "None", "LFO 1", "LFO 2", "Env 1", "Env 2", "Shaper", "Velocity", "Mod wheel" };
            s.setProperty ("src", srcNames[m.source], nullptr);
            s.setProperty ("target", juce::String (m.target), nullptr);
            s.setProperty ("depth", m.depth, nullptr);
            mod.appendChild (s, nullptr);
        }
        sound.appendChild (mod, nullptr);
        sound.setProperty ("shaperPreset", p.shaperPreset, nullptr);
        return sound;
    }

    if (auto xml = juce::XmlDocument::parse (e.file))
    {
        auto t = juce::ValueTree::fromXml (*xml);
        // v1 user presets are a plain list of PARAM children; v2 add MOD / SHAPER / CAPTURE
        for (auto c : t)
            sound.appendChild (c.createCopy(), nullptr);
        return sound;
    }
    return {};
}

bool PresetManager::saveUser (const juce::String& rawName, const juce::String& tags, const juce::ValueTree& sound)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return false;
    juce::ValueTree t ("KLINGEPRESET");
    t.setProperty ("version", 2, nullptr);
    t.setProperty ("tags", tags, nullptr);
    for (auto c : sound) t.appendChild (c.createCopy(), nullptr);
    auto dir = userPresetFolder();
    dir.createDirectory();
    if (auto xml = t.createXml())
        if (xml->writeTo (dir.getChildFile (name + ".klinge")))
        {
            setCurrentName (name);
            return true;
        }
    return false;
}

void PresetManager::loadFavourites() const
{
    if (favouritesLoaded) return;
    favouritesLoaded = true;
    favourites.clear();
    const auto f = userFolder().getChildFile ("favorites.txt");
    if (f.existsAsFile()) f.readLines (favourites);
    favourites.removeEmptyStrings();
}

bool PresetManager::isFavourite (const juce::String& name) const
{
    loadFavourites();
    return favourites.contains (name);
}

void PresetManager::setFavourite (const juce::String& name, bool fav)
{
    loadFavourites();
    if (fav) favourites.addIfNotAlreadyThere (name); else favourites.removeString (name);
    userFolder().createDirectory();
    userFolder().getChildFile ("favorites.txt").replaceWithText (favourites.joinIntoString ("\n"));
}

} // namespace klinge
