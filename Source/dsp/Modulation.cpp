#include "Modulation.h"

namespace klinge
{
// ===========================================================================
// Shaper curve
// ===========================================================================
ShaperCurve::ShaperCurve()
{
    for (auto& t : table) t.store (1.0f, std::memory_order_relaxed);
    setPoints (preset (1));
}

std::vector<ShaperPoint> ShaperCurve::getPoints() const
{
    const juce::SpinLock::ScopedLockType l (lock);
    return points;
}

void ShaperCurve::setPoints (std::vector<ShaperPoint> pts)
{
    for (auto& p : pts)
    {
        p.x = clampf (p.x, 0.0f, 1.0f);
        p.y = clampf (p.y, 0.0f, 1.0f);
        p.curve = clampf (p.curve, -1.0f, 1.0f);
    }
    std::stable_sort (pts.begin(), pts.end(), [] (const ShaperPoint& a, const ShaperPoint& b) { return a.x < b.x; });
    if (pts.empty()) pts.push_back ({ 0.0f, 1.0f, 0.0f });
    if (pts.front().x > 0.0f) pts.insert (pts.begin(), { 0.0f, pts.front().y, 0.0f });
    if (pts.back().x < 1.0f) pts.push_back ({ 1.0f, pts.front().y, 0.0f });
    if (pts.size() > 64) pts.resize (64);
    {
        const juce::SpinLock::ScopedLockType l (lock);
        points = pts;
    }
    rebuild (pts);
    ++version;
}

float ShaperCurve::evaluate (const std::vector<ShaperPoint>& pts, float x) noexcept
{
    if (pts.empty()) return 1.0f;
    if (x <= pts.front().x) return pts.front().y;
    for (size_t i = 0; i + 1 < pts.size(); ++i)
    {
        const auto& a = pts[i];
        const auto& b = pts[i + 1];
        if (x <= b.x)
        {
            const float w = b.x - a.x;
            if (w <= 1.0e-6f) return b.y;                      // vertical step
            float t = (x - a.x) / w;
            const float c = a.curve;
            if (c > 0.0f)      t = std::pow (t, 1.0f + 4.0f * c);
            else if (c < 0.0f) t = 1.0f - std::pow (1.0f - t, 1.0f - 4.0f * c);
            return a.y + (b.y - a.y) * t;
        }
    }
    return pts.back().y;
}

void ShaperCurve::rebuild (const std::vector<ShaperPoint>& pts)
{
    for (int i = 0; i <= shaperTableSize; ++i)
        table[(size_t) i].store (evaluate (pts, (float) i / (float) shaperTableSize), std::memory_order_relaxed);
}

float ShaperCurve::lookup (double phase) const noexcept
{
    phase -= std::floor (phase);
    const double pos = phase * shaperTableSize;
    const int i = std::min (shaperTableSize - 1, (int) pos);
    const float f = (float) (pos - i);
    const float a = table[(size_t) i].load (std::memory_order_relaxed);
    const float b = table[(size_t) i + 1].load (std::memory_order_relaxed);
    return a + f * (b - a);
}

juce::ValueTree ShaperCurve::toTree() const
{
    juce::ValueTree t ("SHAPER");
    for (auto& p : getPoints())
    {
        juce::ValueTree pt ("P");
        pt.setProperty ("x", p.x, nullptr);
        pt.setProperty ("y", p.y, nullptr);
        pt.setProperty ("c", p.curve, nullptr);
        t.appendChild (pt, nullptr);
    }
    return t;
}

void ShaperCurve::fromTree (const juce::ValueTree& t)
{
    if (! t.isValid() || t.getNumChildren() == 0) return;
    std::vector<ShaperPoint> pts;
    for (auto c : t)
        pts.push_back ({ (float) c.getProperty ("x", 0.0), (float) c.getProperty ("y", 1.0), (float) c.getProperty ("c", 0.0) });
    setPoints (pts);
}

juce::StringArray ShaperCurve::presetNames()
{
    return { "Flat", "Pump", "Duck hard", "Gate 1/2", "Gate 3/4", "Ramp up", "Ramp down", "Swell", "Stutter", "Off-beat" };
}

std::vector<ShaperPoint> ShaperCurve::preset (int index)
{
    switch (index)
    {
        case 1:  return { { 0, 0, 0 }, { 0.02f, 0, -0.6f }, { 0.55f, 1, 0 }, { 1, 1, 0 } };                                   // Pump
        case 2:  return { { 0, 0, 0 }, { 0.15f, 0, -0.8f }, { 0.35f, 1, 0 }, { 1, 1, 0 } };                                   // Duck hard
        case 3:  return { { 0, 1, 0 }, { 0.5f, 1, 0 }, { 0.5f, 0, 0 }, { 1, 0, 0 } };                                        // Gate 1/2
        case 4:  return { { 0, 1, 0 }, { 0.75f, 1, 0 }, { 0.75f, 0, 0 }, { 1, 0, 0 } };                                      // Gate 3/4
        case 5:  return { { 0, 0, 0.3f }, { 1, 1, 0 } };                                                                     // Ramp up
        case 6:  return { { 0, 1, -0.3f }, { 1, 0, 0 } };                                                                    // Ramp down
        case 7:  return { { 0, 0.15f, 0.5f }, { 0.85f, 1, -0.5f }, { 1, 0.15f, 0 } };                                       // Swell
        case 8:  return { { 0, 1, 0 }, { 0.2f, 1, 0 }, { 0.2f, 0, 0 }, { 0.25f, 0, 0 }, { 0.25f, 1, 0 }, { 0.45f, 1, 0 },
                          { 0.45f, 0, 0 }, { 0.5f, 0, 0 }, { 0.5f, 1, 0 }, { 0.95f, 1, 0 }, { 0.95f, 0, 0 }, { 1, 0, 0 } };   // Stutter
        case 9:  return { { 0, 0.1f, 0 }, { 0.4f, 0.1f, 0.4f }, { 0.5f, 1, -0.4f }, { 0.75f, 1, 0 }, { 1, 0.1f, 0 } };        // Off-beat
        default: return { { 0, 1, 0 }, { 1, 1, 0 } };                                                                        // Flat
    }
}

// ===========================================================================
// Mod matrix
// ===========================================================================
ModSlotData ModMatrix::getSlot (int i) const
{
    const juce::SpinLock::ScopedLockType l (lock);
    return i >= 0 && i < numModSlots ? data[(size_t) i] : ModSlotData();
}

void ModMatrix::publish (int i)
{
    const auto& d = data[(size_t) i];
    const int idx = d.isEmpty() || ! resolve ? -1 : resolve (d.target);
    auto& L = live[(size_t) i];
    L.depth.store (clampf (d.depth, -1.0f, 1.0f));
    L.target.store (idx);
    L.source.store (idx < 0 ? 0 : d.source);
}

void ModMatrix::setSlot (int i, const ModSlotData& d)
{
    if (i < 0 || i >= numModSlots) return;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        data[(size_t) i] = d;
        data[(size_t) i].depth = clampf (d.depth, -1.0f, 1.0f);
        publish (i);
    }
    ++version;
}

int ModMatrix::assign (int source, const juce::String& target, float depth)
{
    int slot = -1;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        for (int i = 0; i < numModSlots && slot < 0; ++i)
            if (data[(size_t) i].source == source && data[(size_t) i].target == target) slot = i;
        for (int i = 0; i < numModSlots && slot < 0; ++i)
            if (data[(size_t) i].isEmpty()) slot = i;
        if (slot >= 0)
        {
            data[(size_t) slot] = { source, target, clampf (depth, -1.0f, 1.0f) };
            publish (slot);
        }
    }
    if (slot >= 0) ++version;
    return slot;
}

void ModMatrix::removeTarget (const juce::String& target)
{
    {
        const juce::SpinLock::ScopedLockType l (lock);
        for (int i = 0; i < numModSlots; ++i)
            if (data[(size_t) i].target == target) { data[(size_t) i] = {}; publish (i); }
    }
    ++version;
}

void ModMatrix::clear()
{
    {
        const juce::SpinLock::ScopedLockType l (lock);
        for (int i = 0; i < numModSlots; ++i) { data[(size_t) i] = {}; publish (i); }
    }
    ++version;
}

std::vector<ModSlotData> ModMatrix::slotsForTarget (const juce::String& target) const
{
    std::vector<ModSlotData> r;
    const juce::SpinLock::ScopedLockType l (lock);
    for (auto& d : data)
        if (! d.isEmpty() && d.target == target) r.push_back (d);
    return r;
}

juce::ValueTree ModMatrix::toTree() const
{
    juce::ValueTree t ("MOD");
    const juce::SpinLock::ScopedLockType l (lock);
    for (int i = 0; i < numModSlots; ++i)
    {
        const auto& d = data[(size_t) i];
        if (d.isEmpty()) continue;
        juce::ValueTree s ("SLOT");
        s.setProperty ("i", i, nullptr);
        s.setProperty ("src", modSourceNames()[d.source], nullptr);
        s.setProperty ("target", d.target, nullptr);
        s.setProperty ("depth", d.depth, nullptr);
        t.appendChild (s, nullptr);
    }
    return t;
}

void ModMatrix::fromTree (const juce::ValueTree& t)
{
    {
        const juce::SpinLock::ScopedLockType l (lock);
        for (auto& d : data) d = {};
        if (t.isValid())
            for (auto s : t)
            {
                const int i = s.getProperty ("i", -1);
                const int src = modSourceNames().indexOf (s.getProperty ("src").toString());
                if (i < 0 || i >= numModSlots || src <= 0) continue;
                data[(size_t) i] = { src, s.getProperty ("target").toString(), clampf ((float) s.getProperty ("depth", 0.0), -1.0f, 1.0f) };
            }
        for (int i = 0; i < numModSlots; ++i) publish (i);
    }
    ++version;
}

// ===========================================================================
// Modulation engine
// ===========================================================================
void ModEngine::prepare (double sampleRate)
{
    fs = sampleRate > 0 ? sampleRate : 48000.0;
    reset();
}

void ModEngine::reset()
{
    for (auto& l : lfos) l = Lfo();
    envs.fill (0.0);
    shaperPhase = 0.0;
    values.fill (0.0f);
    for (auto& d : display) d.store (0.0f);
}

void ModEngine::noteOn (float vel) noexcept
{
    velocity = vel;
    for (auto& l : lfos) l.retrigPending = true;
    shaperRetrig = true;
}

float ModEngine::random() noexcept
{
    rng = rng * 1664525u + 1013904223u;
    return (float) (rng >> 8) / 8388608.0f * 2.0f - 1.0f;
}

float ModEngine::lfoShape (Lfo& l, int shape, double ph, bool wrapped) noexcept
{
    const float p = (float) (ph - std::floor (ph));
    switch (shape)
    {
        case 0:  return std::sin (2.0f * kPi * p);
        case 1:  return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
        case 2:  return 2.0f * p - 1.0f;
        case 3:  return 1.0f - 2.0f * p;
        case 4:  return p < 0.5f ? 1.0f : -1.0f;
        case 5:  if (wrapped) l.randNext = random(); return l.randNext;
        case 6:
        {
            if (wrapped) { l.randPrev = l.randNext; l.randNext = random(); }
            const float t = 0.5f - 0.5f * std::cos (kPi * p);
            return l.randPrev + t * (l.randNext - l.randPrev);
        }
        default: return 0.0f;
    }
}

void ModEngine::processChunk (const float* const* in, int nc, const float* const* sc, int scCh, int n,
                              const Transport& tr, double latencySamples, const float* v, const ParamIndex& ix) noexcept
{
    const double bpm = tr.bpm > 1.0 ? tr.bpm : 120.0;
    const double beatsPerSample = bpm / 60.0 / fs;
    const double centre = 0.5 * n;
    auto& divs = divisions();
    auto divBeats = [&divs] (float idx) { return divs[(size_t) juce::jlimit (0, (int) divs.size() - 1, (int) idx)].beats; };

    // ---- LFOs (value at the chunk centre)
    for (int l = 0; l < numLfos; ++l)
    {
        auto& L = lfos[(size_t) l];
        const auto& P = ix.lfo[l];
        const bool sync = v[P.sync] > 0.5f;
        const bool retrig = v[P.retrig] > 0.5f;
        const double phaseOff = v[P.phase] / 360.0;
        const double beats = divBeats (v[P.div]);
        const double hz = sync ? bpm / 60.0 / beats : (double) v[P.rate];

        if (retrig && L.retrigPending) L.phase = 0.0;
        L.retrigPending = false;

        double ph;
        const double before = L.phase;
        if (sync && tr.playing && ! retrig)
        {
            ph = (tr.ppq + centre * beatsPerSample) / beats;
            L.phase = ph - std::floor (ph);
        }
        else
        {
            L.phase += hz * n / fs;
            L.phase -= std::floor (L.phase);
            ph = L.phase;
        }
        const bool wrapped = L.phase < before || std::abs (L.phase - before) > 0.5;
        float out = lfoShape (L, (int) v[P.shape], ph + phaseOff, wrapped);

        const double smoothMs = v[P.smooth] * 1.0;      // 0..100 % -> 0..100 ms
        if (smoothMs > 0.05)
        {
            const float k = (float) std::exp (-n / (fs * smoothMs * 0.001));
            L.smoothed = out + k * (L.smoothed - out);
            out = L.smoothed;
        }
        else L.smoothed = out;
        values[(size_t) ModSource::Lfo1 + (size_t) l] = out;
        lfoPhaseDisplay[(size_t) l].store ((float) (ph + phaseOff - std::floor (ph + phaseOff)), std::memory_order_relaxed);
    }

    // ---- envelope followers
    for (int e = 0; e < numEnvs; ++e)
    {
        const auto& P = ix.env[e];
        const bool useSc = v[P.src] > 0.5f && sc != nullptr && scCh > 0;
        const float* const* src = useSc ? sc : in;
        const int chans = useSc ? scCh : nc;
        const double att = EnvFollower::coeff (fs, v[P.att]);
        const double rel = EnvFollower::coeff (fs, v[P.rel]);
        double env = envs[(size_t) e];
        for (int i = 0; i < n; ++i)
        {
            float a = 0.0f;
            for (int c = 0; c < std::min (chans, 2); ++c) a = std::max (a, std::abs (src[c][i]));
            const double k = a > env ? att : rel;
            env = undenorm (a + k * (env - a));
        }
        envs[(size_t) e] = env;
        values[(size_t) ModSource::Env1 + (size_t) e] = clampf ((float) env * dbToGain (v[P.gain]), 0.0f, 1.0f);
    }

    // ---- shaper (phase aligned to what the listener hears: output is late by the latency)
    {
        const double beats = divBeats (v[ix.shpRate]);
        const bool retrig = v[ix.shpRetrig] > 0.5f;
        const double inc = beatsPerSample / beats;
        const double phaseOff = v[ix.shpPhase] / 360.0;
        if (retrig && shaperRetrig) shaperPhase = 0.0;
        shaperRetrig = false;
        double start;
        if (tr.playing && ! retrig)
            start = (tr.ppq - latencySamples * beatsPerSample) / beats;
        else
            start = shaperPhase;
        start -= std::floor (start);
        shaperPhase = start + inc * n;
        shaperPhase -= std::floor (shaperPhase);

        shaperRun.phase0 = start + phaseOff;
        shaperRun.inc = inc;
        shaperRun.amount = clampf (v[ix.shpAmount] * 0.01f, 0.0f, 1.0f);
        const double ms = std::max (0.01, (double) v[ix.shpSmooth]);
        shaperRun.smoothCoef = (float) std::exp (-1.0 / (fs * ms * 0.001));
        shaperRun.curve = curve;
        values[(size_t) ModSource::Shaper] = curve != nullptr ? curve->lookup (start + phaseOff + inc * centre) : 0.0f;
        const double shown = start + phaseOff;
        shaperPhaseDisplay.store ((float) (shown - std::floor (shown)), std::memory_order_relaxed);
    }

    values[(size_t) ModSource::Velocity] = velocity;
    values[(size_t) ModSource::ModWheel] = wheel;
    for (size_t s = 0; s < values.size(); ++s) display[s].store (values[s], std::memory_order_relaxed);
}

} // namespace klinge
