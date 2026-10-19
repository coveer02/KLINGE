#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace klinge
{
// ---------------------------------------------------------------------------
// Parameter IDs. IDs of version 1.0 are never renamed, so old sessions load.
// ---------------------------------------------------------------------------
namespace pid
{
    // v1.0
    inline constexpr const char* inGain      = "in_gain";
    inline constexpr const char* drive       = "drive";
    inline constexpr const char* routing     = "routing";
    inline constexpr const char* xoverLo     = "xover_lo";      // crossover 1
    inline constexpr const char* xoverHi     = "xover_hi";      // crossover 2
    inline constexpr const char* preHP       = "pre_hp";
    inline constexpr const char* preLP       = "pre_lp";
    inline constexpr const char* focus       = "focus";
    inline constexpr const char* dynDrive    = "dyn_drive";
    inline constexpr const char* punch       = "punch";
    inline constexpr const char* punchSpeed  = "punch_speed";
    inline constexpr const char* toneLock    = "tone_lock";
    inline constexpr const char* postHP      = "post_hp";
    inline constexpr const char* postLP      = "post_lp";
    inline constexpr const char* mix         = "mix";
    inline constexpr const char* outGain     = "out_gain";
    inline constexpr const char* autoGain    = "auto_gain";
    inline constexpr const char* oversample  = "oversampling";
    inline constexpr const char* osQuality   = "os_quality";
    inline constexpr const char* delta       = "delta";
    inline constexpr const char* clipOut     = "clip_out";
    inline constexpr const char* harmLock    = "harm_lock";

    // v2.0
    inline constexpr const char* xover3      = "xover_3";
    inline constexpr const char* xover4      = "xover_4";
    inline constexpr const char* numBands    = "num_bands";
    inline constexpr const char* fbAmount    = "fb_amount";
    inline constexpr const char* fbTime      = "fb_time";
    inline constexpr const char* fbTone      = "fb_tone";
    inline constexpr const char* analog      = "analog";
    inline constexpr const char* renderOs    = "render_os";
    inline constexpr const char* shpAmount   = "shp_amount";
    inline constexpr const char* shpRate     = "shp_rate";
    inline constexpr const char* shpSmooth   = "shp_smooth";
    inline constexpr const char* shpPhase    = "shp_phase";
    inline constexpr const char* shpRetrig   = "shp_retrig";
    inline constexpr const char* capOn       = "cap_on";
    inline constexpr const char* capPos      = "cap_pos";
    inline constexpr const char* capIn       = "cap_in";
    inline constexpr const char* capOut      = "cap_out";
    inline constexpr const char* capMix      = "cap_mix";
    inline constexpr const char* capStereo   = "cap_stereo";

    inline juce::String harm (int n)                 { return "h" + juce::String (n); }       // n = 2..8
    inline juce::String band (int b, const char* s)  { return "b" + juce::String (b + 1) + "_" + s; }
    inline juce::String lfo (int l, const char* s)   { return "lfo" + juce::String (l + 1) + "_" + s; }
    inline juce::String env (int e, const char* s)   { return "env" + juce::String (e + 1) + "_" + s; }

    // band suffixes
    inline constexpr const char* bType   = "type";
    inline constexpr const char* bDrive  = "drive";
    inline constexpr const char* bShape  = "shape";
    inline constexpr const char* bBias   = "bias";
    inline constexpr const char* bHarm   = "harm";
    inline constexpr const char* bMix    = "mix";
    inline constexpr const char* bLevel  = "level";
    inline constexpr const char* bOn     = "on";
    inline constexpr const char* bSolo   = "solo";
    inline constexpr const char* bMute   = "mute";
    inline constexpr const char* bSag    = "sag";      // v2.0
    inline constexpr const char* bStereo = "stereo";   // v2.0
    inline constexpr const char* bDyn    = "dyn";      // v2.0
    inline constexpr const char* bThresh = "thresh";   // v2.0
    inline constexpr const char* bDynPos = "dyn_pos";  // v2.0

    // LFO suffixes
    inline constexpr const char* lShape  = "shape";
    inline constexpr const char* lRate   = "rate";
    inline constexpr const char* lSync   = "sync";
    inline constexpr const char* lDiv    = "div";
    inline constexpr const char* lPhase  = "phase";
    inline constexpr const char* lSmooth = "smooth";
    inline constexpr const char* lRetrig = "retrig";

    // envelope follower suffixes
    inline constexpr const char* eSrc    = "src";
    inline constexpr const char* eAtt    = "att";
    inline constexpr const char* eRel    = "rel";
    inline constexpr const char* eGain   = "gain";
}

inline constexpr int numBands        = 5;
inline constexpr int numLfos         = 2;
inline constexpr int numEnvs         = 2;
inline constexpr int firstHarmonic   = 2;
inline constexpr int lastHarmonic    = 8;
inline constexpr int numHarmonics    = lastHarmonic - firstHarmonic + 1;
inline constexpr float maxTotalDrive = 60.0f;

enum class Routing { Single = 0, Multiband, MidSide };
enum class BandStereo { Stereo = 0, Mid, Side };

inline const juce::StringArray& routingNames()
{
    static const juce::StringArray n { "Single", "Multiband", "Mid / Side" };
    return n;
}

inline const juce::StringArray& oversamplingNames()
{
    static const juce::StringArray n { "Off", "2x", "4x", "8x", "16x" };
    return n;
}

// Tempo divisions shared by LFOs and the shaper (length in quarter notes)
struct Division { const char* name; double beats; };
inline const std::vector<Division>& divisions()
{
    static const std::vector<Division> d {
        { "8 bars", 32.0 }, { "4 bars", 16.0 }, { "2 bars", 8.0 }, { "1 bar", 4.0 },
        { "1/2", 2.0 }, { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/16", 0.25 }, { "1/32", 0.125 }, { "1/64", 0.0625 },
        { "1/2 T", 4.0 / 3.0 }, { "1/4 T", 2.0 / 3.0 }, { "1/8 T", 1.0 / 3.0 }, { "1/16 T", 1.0 / 6.0 },
        { "1/2 D", 3.0 }, { "1/4 D", 1.5 }, { "1/8 D", 0.75 }, { "1/16 D", 0.375 } };
    return d;
}
inline juce::StringArray divisionNames()
{
    juce::StringArray s;
    for (auto& d : divisions()) s.add (d.name);
    return s;
}

inline const juce::StringArray& lfoShapeNames()
{
    static const juce::StringArray n { "Sine", "Triangle", "Saw up", "Saw down", "Square", "Random step", "Random smooth" };
    return n;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// ---------------------------------------------------------------------------
// Index table: maps every parameter to its position in a flat value array.
// The engine reads parameters from that array, which is what makes generic
// modulation of any continuous parameter possible.
// ---------------------------------------------------------------------------
struct ParamIndex
{
    int inGain, drive, routing, xover[4], bandCount, preHP, preLP, focus, dynDrive, punch, punchSpeed, toneLock,
        postHP, postLP, mix, outGain, autoGain, oversample, osQuality, delta, clipOut, harmLock, harm[numHarmonics],
        fbAmount, fbTime, fbTone, analog, renderOs, shpAmount, shpRate, shpSmooth, shpPhase, shpRetrig,
        capOn, capPos, capIn, capOut, capMix, capStereo;
    struct Band { int type, drive, shape, bias, harm, mix, level, on, solo, mute, sag, stereo, dyn, thresh, dynPos; } band[numBands];
    struct Lfo  { int shape, rate, sync, div, phase, smooth, retrig; } lfo[numLfos];
    struct Env  { int src, att, rel, gain; } env[numEnvs];

    void build (const juce::Array<juce::AudioProcessorParameter*>& params);
};

} // namespace klinge
