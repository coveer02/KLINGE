#include "Params.h"
#include "Shapers.h"

namespace klinge
{
namespace
{
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;

juce::NormalisableRange<float> logRange (float lo, float hi, float centre)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}

// Text <-> value conversions. Every formatter decides sign and precision on the rounded value, and every
// parser accepts what the formatter writes, so value -> text -> value -> text is stable (hosts and the
// CLAP validator rely on that).
float parseNumber (const juce::String& text)
{
    return text.trim().initialSectionContainingOnly ("+-0123456789.").getFloatValue();
}

juce::AudioParameterFloatAttributes dbAttr()
{
    return juce::AudioParameterFloatAttributes().withLabel ("dB")
        .withStringFromValueFunction ([] (float v, int)
        {
            const float r = std::round (v * 10.0f) / 10.0f;
            return (r > 0.0f ? "+" : "") + juce::String (std::abs (r) < 0.05f ? 0.0f : r, 1) + " dB";
        })
        .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); });
}
juce::AudioParameterFloatAttributes pctAttr()
{
    return juce::AudioParameterFloatAttributes().withLabel ("%")
        .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })
        .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); });
}
juce::AudioParameterFloatAttributes bipolarPctAttr()
{
    return juce::AudioParameterFloatAttributes().withLabel ("%")
        .withStringFromValueFunction ([] (float v, int) { const int i = juce::roundToInt (v); return (i > 0 ? "+" : "") + juce::String (i) + " %"; })
        .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); });
}
juce::AudioParameterFloatAttributes hzAttr (float offBelow = -1.0f, float offAbove = 1.0e9f)
{
    return juce::AudioParameterFloatAttributes().withLabel ("Hz")
        .withStringFromValueFunction ([offBelow, offAbove] (float v, int)
        {
            if (v <= offBelow || v >= offAbove) return juce::String ("Off");
            if (v < 9.995f)  return juce::String (v, 2) + " Hz";
            if (v < 999.5f)  return juce::String (juce::roundToInt (v)) + " Hz";
            const float k = v / 1000.0f;
            return juce::String (k, k < 9.995f ? 2 : 1) + " kHz";
        })
        .withValueFromStringFunction ([offBelow, offAbove] (const juce::String& t)
        {
            if (t.trim().startsWithIgnoreCase ("off")) return offBelow > 0.0f ? offBelow - 0.5f : offAbove + 100.0f;
            const float x = parseNumber (t);
            return t.containsIgnoreCase ("k") ? x * 1000.0f : x;
        });
}
juce::AudioParameterFloatAttributes msAttr (int decimals = 0)
{
    return juce::AudioParameterFloatAttributes().withLabel ("ms")
        .withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals) + " ms"; })
        .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); });
}
juce::AudioParameterFloatAttributes degAttr()
{
    return juce::AudioParameterFloatAttributes().withLabel ("deg")
        .withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
        })
        .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); });
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto v1 = [] (const juce::String& s) { return juce::ParameterID { s, 1 }; };
    auto v2 = [] (const juce::String& s) { return juce::ParameterID { s, 2 }; };

    // ---------------- v1.0 globals (unchanged IDs; display names updated)
    layout.add (std::make_unique<APF> (v1 (pid::inGain), "Input", juce::NormalisableRange<float> (-24, 24, 0.01f), 0.0f, dbAttr()));
    layout.add (std::make_unique<APF> (v1 (pid::drive), "Drive", juce::NormalisableRange<float> (0, 48, 0.01f), 12.0f, dbAttr()));
    layout.add (std::make_unique<APC> (v1 (pid::routing), "Routing", routingNames(), 0));
    layout.add (std::make_unique<APF> (v1 (pid::xoverLo), "Crossover 1", logRange (30, 16000, 600), 200.0f, hzAttr()));
    layout.add (std::make_unique<APF> (v1 (pid::xoverHi), "Crossover 2", logRange (30, 16000, 600), 3000.0f, hzAttr()));
    layout.add (std::make_unique<APF> (v1 (pid::preHP), "Pre Low Cut", logRange (20, 1000, 120), 20.0f, hzAttr (20.5f)));
    layout.add (std::make_unique<APF> (v1 (pid::preLP), "Pre High Cut", logRange (1000, 20000, 5000), 20000.0f, hzAttr (-1, 19900)));
    layout.add (std::make_unique<APF> (v1 (pid::focus), "Focus", juce::NormalisableRange<float> (-12, 12, 0.01f), 0.0f, dbAttr()));
    layout.add (std::make_unique<APF> (v1 (pid::dynDrive), "Dynamics", juce::NormalisableRange<float> (-100, 100, 0.1f), 0.0f, bipolarPctAttr()));
    layout.add (std::make_unique<APF> (v1 (pid::punch), "Punch", juce::NormalisableRange<float> (0, 150, 0.1f), 0.0f, pctAttr()));
    layout.add (std::make_unique<APF> (v1 (pid::punchSpeed), "Punch Speed", logRange (5, 200, 40), 40.0f, msAttr()));
    layout.add (std::make_unique<APF> (v1 (pid::toneLock), "Faithful", juce::NormalisableRange<float> (0, 100, 0.1f), 0.0f, pctAttr()));
    layout.add (std::make_unique<APF> (v1 (pid::postHP), "Post Low Cut", logRange (20, 1000, 120), 20.0f, hzAttr (20.5f)));
    layout.add (std::make_unique<APF> (v1 (pid::postLP), "Post High Cut", logRange (1000, 20000, 5000), 20000.0f, hzAttr (-1, 19900)));
    layout.add (std::make_unique<APF> (v1 (pid::mix), "Mix", juce::NormalisableRange<float> (0, 100, 0.1f), 100.0f, pctAttr()));
    layout.add (std::make_unique<APF> (v1 (pid::outGain), "Output", juce::NormalisableRange<float> (-24, 24, 0.01f), 0.0f, dbAttr()));
    layout.add (std::make_unique<APB> (v1 (pid::autoGain), "Auto Gain", true));
    layout.add (std::make_unique<APC> (v1 (pid::oversample), "Oversampling", oversamplingNames(), 2));
    layout.add (std::make_unique<APC> (v1 (pid::osQuality), "OS Filter", juce::StringArray { "Linear Phase", "Low Latency" }, 0));
    layout.add (std::make_unique<APB> (v1 (pid::delta), "Delta", false));
    layout.add (std::make_unique<APB> (v1 (pid::clipOut), "Output Clip", false));
    layout.add (std::make_unique<APF> (v1 (pid::harmLock), "Harmonic Lock", juce::NormalisableRange<float> (0, 100, 0.1f), 100.0f, pctAttr()));
    for (int n = firstHarmonic; n <= lastHarmonic; ++n)
        layout.add (std::make_unique<APF> (v1 (pid::harm (n)), "Harmonic " + juce::String (n),
                                           juce::NormalisableRange<float> (-100, 100, 0.1f), 0.0f, bipolarPctAttr()));

    // ---------------- v2.0 globals
    layout.add (std::make_unique<APF> (v2 (pid::xover3), "Crossover 3", logRange (30, 16000, 600), 6000.0f, hzAttr()));
    layout.add (std::make_unique<APF> (v2 (pid::xover4), "Crossover 4", logRange (30, 16000, 600), 10000.0f, hzAttr()));
    layout.add (std::make_unique<APC> (v2 (pid::numBands), "Band Count", juce::StringArray { "2", "3", "4", "5" }, 1));
    layout.add (std::make_unique<APF> (v2 (pid::fbAmount), "Feedback", juce::NormalisableRange<float> (0, 95, 0.1f), 0.0f, pctAttr()));
    layout.add (std::make_unique<APF> (v2 (pid::fbTime), "Feedback Time", logRange (0.05f, 30.0f, 2.0f), 3.0f, msAttr (2)));
    layout.add (std::make_unique<APF> (v2 (pid::fbTone), "Feedback Tone", logRange (300, 20000, 3000), 6000.0f, hzAttr()));
    layout.add (std::make_unique<APF> (v2 (pid::analog), "Analog", juce::NormalisableRange<float> (0, 100, 0.1f), 0.0f, pctAttr()));
    layout.add (std::make_unique<APC> (v2 (pid::renderOs), "Render Quality",
                                       juce::StringArray { "Same as live", "One step higher", "Two steps higher", "Maximum (16x)" }, 0));
    layout.add (std::make_unique<APF> (v2 (pid::shpAmount), "Shaper Amount", juce::NormalisableRange<float> (0, 100, 0.1f), 0.0f, pctAttr()));
    layout.add (std::make_unique<APC> (v2 (pid::shpRate), "Shaper Rate", divisionNames(), 5));
    layout.add (std::make_unique<APF> (v2 (pid::shpSmooth), "Shaper Smooth", juce::NormalisableRange<float> (0, 20, 0.01f), 2.0f, msAttr (1)));
    layout.add (std::make_unique<APF> (v2 (pid::shpPhase), "Shaper Phase", juce::NormalisableRange<float> (0, 360, 0.1f), 0.0f, degAttr()));
    layout.add (std::make_unique<APB> (v2 (pid::shpRetrig), "Shaper MIDI Retrigger", false));
    layout.add (std::make_unique<APB> (v2 (pid::capOn), "Capture On", false));
    layout.add (std::make_unique<APC> (v2 (pid::capPos), "Capture Position", juce::StringArray { "Before bands", "After bands" }, 0));
    layout.add (std::make_unique<APF> (v2 (pid::capIn), "Capture Input", juce::NormalisableRange<float> (-24, 24, 0.01f), 0.0f, dbAttr()));
    layout.add (std::make_unique<APF> (v2 (pid::capOut), "Capture Output", juce::NormalisableRange<float> (-24, 24, 0.01f), 0.0f, dbAttr()));
    layout.add (std::make_unique<APF> (v2 (pid::capMix), "Capture Mix", juce::NormalisableRange<float> (0, 100, 0.1f), 100.0f, pctAttr()));
    layout.add (std::make_unique<APB> (v2 (pid::capStereo), "Capture Stereo", false));

    for (int l = 0; l < numLfos; ++l)
    {
        const juce::String n = "LFO " + juce::String (l + 1) + " ";
        layout.add (std::make_unique<APC> (v2 (pid::lfo (l, pid::lShape)), n + "Shape", lfoShapeNames(), 0));
        layout.add (std::make_unique<APF> (v2 (pid::lfo (l, pid::lRate)), n + "Rate", logRange (0.01f, 40.0f, 1.0f), 1.0f, hzAttr()));
        layout.add (std::make_unique<APB> (v2 (pid::lfo (l, pid::lSync)), n + "Sync", true));
        layout.add (std::make_unique<APC> (v2 (pid::lfo (l, pid::lDiv)), n + "Division", divisionNames(), l == 0 ? 5 : 3));
        layout.add (std::make_unique<APF> (v2 (pid::lfo (l, pid::lPhase)), n + "Phase", juce::NormalisableRange<float> (0, 360, 0.1f), 0.0f, degAttr()));
        layout.add (std::make_unique<APF> (v2 (pid::lfo (l, pid::lSmooth)), n + "Smooth", juce::NormalisableRange<float> (0, 100, 0.1f), 0.0f, pctAttr()));
        layout.add (std::make_unique<APB> (v2 (pid::lfo (l, pid::lRetrig)), n + "MIDI Retrigger", false));
    }
    for (int e = 0; e < numEnvs; ++e)
    {
        const juce::String n = "Env " + juce::String (e + 1) + " ";
        layout.add (std::make_unique<APC> (v2 (pid::env (e, pid::eSrc)), n + "Source", juce::StringArray { "Input", "Sidechain" }, 0));
        layout.add (std::make_unique<APF> (v2 (pid::env (e, pid::eAtt)), n + "Attack", logRange (0.1f, 200.0f, 5.0f), 2.0f, msAttr (1)));
        layout.add (std::make_unique<APF> (v2 (pid::env (e, pid::eRel)), n + "Release", logRange (5.0f, 2000.0f, 120.0f), 120.0f, msAttr()));
        layout.add (std::make_unique<APF> (v2 (pid::env (e, pid::eGain)), n + "Sensitivity", juce::NormalisableRange<float> (-24, 36, 0.01f), 6.0f, dbAttr()));
    }

    // ---------------- bands
    const int defaultTypes[numBands] = { (int) Algo::Tube, (int) Algo::Detail, (int) Algo::Silk, (int) Algo::Tape, (int) Algo::Silk };
    for (int b = 0; b < numBands; ++b)
    {
        auto id = [b, &v1, &v2] (const char* suffix, bool v2Param) { return v2Param || b >= 3 ? v2 (pid::band (b, suffix)) : v1 (pid::band (b, suffix)); };
        const juce::String bn = "Band " + juce::String (b + 1);
        layout.add (std::make_unique<APC> (id (pid::bType, false), bn + " Type", algoNames(), defaultTypes[b]));
        layout.add (std::make_unique<APF> (id (pid::bDrive, false), bn + " Drive", juce::NormalisableRange<float> (-24, 24, 0.01f), 0.0f, dbAttr()));
        layout.add (std::make_unique<APF> (id (pid::bShape, false), bn + " Shape", juce::NormalisableRange<float> (0, 100, 0.1f), 50.0f, pctAttr()));
        layout.add (std::make_unique<APF> (id (pid::bBias, false), bn + " Bias", juce::NormalisableRange<float> (-100, 100, 0.1f), 0.0f, bipolarPctAttr()));
        layout.add (std::make_unique<APF> (id (pid::bHarm, false), bn + " Harmonics", juce::NormalisableRange<float> (0, 100, 0.1f), 100.0f, pctAttr()));
        layout.add (std::make_unique<APF> (id (pid::bMix, false), bn + " Mix", juce::NormalisableRange<float> (0, 100, 0.1f), 100.0f, pctAttr()));
        layout.add (std::make_unique<APF> (id (pid::bLevel, false), bn + " Level", juce::NormalisableRange<float> (-24, 12, 0.01f), 0.0f, dbAttr()));
        layout.add (std::make_unique<APB> (id (pid::bOn, false), bn + " On", true));
        layout.add (std::make_unique<APB> (id (pid::bSolo, false), bn + " Solo", false));
        layout.add (std::make_unique<APB> (id (pid::bMute, false), bn + " Mute", false));
        layout.add (std::make_unique<APF> (id (pid::bSag, true), bn + " Sag", juce::NormalisableRange<float> (0, 100, 0.1f), 0.0f, pctAttr()));
        layout.add (std::make_unique<APC> (id (pid::bStereo, true), bn + " Stereo", juce::StringArray { "Stereo", "Mid", "Side" }, 0));
        layout.add (std::make_unique<APF> (id (pid::bDyn, true), bn + " Dynamics", juce::NormalisableRange<float> (-100, 100, 0.1f), 0.0f, bipolarPctAttr()));
        layout.add (std::make_unique<APF> (id (pid::bThresh, true), bn + " Threshold", juce::NormalisableRange<float> (-60, 0, 0.01f), -24.0f, dbAttr()));
        layout.add (std::make_unique<APC> (id (pid::bDynPos, true), bn + " Dynamics Position", juce::StringArray { "Before", "After" }, 0));
    }
    return layout;
}

// ---------------------------------------------------------------------------
void ParamIndex::build (const juce::Array<juce::AudioProcessorParameter*>& params)
{
    std::map<juce::String, int> idx;
    for (int i = 0; i < params.size(); ++i)
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (params[i]))
            idx[p->getParameterID()] = i;
    auto get = [&idx] (const juce::String& id)
    {
        auto it = idx.find (id);
        jassert (it != idx.end());
        return it == idx.end() ? 0 : it->second;
    };

    inGain = get (pid::inGain); drive = get (pid::drive); routing = get (pid::routing);
    xover[0] = get (pid::xoverLo); xover[1] = get (pid::xoverHi); xover[2] = get (pid::xover3); xover[3] = get (pid::xover4);
    bandCount = get (pid::numBands); preHP = get (pid::preHP); preLP = get (pid::preLP); focus = get (pid::focus);
    dynDrive = get (pid::dynDrive); punch = get (pid::punch); punchSpeed = get (pid::punchSpeed); toneLock = get (pid::toneLock);
    postHP = get (pid::postHP); postLP = get (pid::postLP); mix = get (pid::mix); outGain = get (pid::outGain);
    autoGain = get (pid::autoGain); oversample = get (pid::oversample); osQuality = get (pid::osQuality);
    delta = get (pid::delta); clipOut = get (pid::clipOut); harmLock = get (pid::harmLock);
    for (int n = 0; n < numHarmonics; ++n) harm[n] = get (pid::harm (n + firstHarmonic));
    fbAmount = get (pid::fbAmount); fbTime = get (pid::fbTime); fbTone = get (pid::fbTone); analog = get (pid::analog);
    renderOs = get (pid::renderOs); shpAmount = get (pid::shpAmount); shpRate = get (pid::shpRate);
    shpSmooth = get (pid::shpSmooth); shpPhase = get (pid::shpPhase); shpRetrig = get (pid::shpRetrig);
    capOn = get (pid::capOn); capPos = get (pid::capPos); capIn = get (pid::capIn); capOut = get (pid::capOut);
    capMix = get (pid::capMix); capStereo = get (pid::capStereo);
    for (int b = 0; b < klinge::numBands; ++b)
    {
        auto& B = band[b];
        B.type = get (pid::band (b, pid::bType)); B.drive = get (pid::band (b, pid::bDrive));
        B.shape = get (pid::band (b, pid::bShape)); B.bias = get (pid::band (b, pid::bBias));
        B.harm = get (pid::band (b, pid::bHarm)); B.mix = get (pid::band (b, pid::bMix));
        B.level = get (pid::band (b, pid::bLevel)); B.on = get (pid::band (b, pid::bOn));
        B.solo = get (pid::band (b, pid::bSolo)); B.mute = get (pid::band (b, pid::bMute));
        B.sag = get (pid::band (b, pid::bSag)); B.stereo = get (pid::band (b, pid::bStereo));
        B.dyn = get (pid::band (b, pid::bDyn)); B.thresh = get (pid::band (b, pid::bThresh));
        B.dynPos = get (pid::band (b, pid::bDynPos));
    }
    for (int l = 0; l < numLfos; ++l)
    {
        auto& L = lfo[l];
        L.shape = get (pid::lfo (l, pid::lShape)); L.rate = get (pid::lfo (l, pid::lRate)); L.sync = get (pid::lfo (l, pid::lSync));
        L.div = get (pid::lfo (l, pid::lDiv)); L.phase = get (pid::lfo (l, pid::lPhase));
        L.smooth = get (pid::lfo (l, pid::lSmooth)); L.retrig = get (pid::lfo (l, pid::lRetrig));
    }
    for (int e = 0; e < numEnvs; ++e)
    {
        auto& E = env[e];
        E.src = get (pid::env (e, pid::eSrc)); E.att = get (pid::env (e, pid::eAtt));
        E.rel = get (pid::env (e, pid::eRel)); E.gain = get (pid::env (e, pid::eGain));
    }
}

} // namespace klinge
