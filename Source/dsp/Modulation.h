#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "Primitives.h"

namespace klinge
{
// ---------------------------------------------------------------------------
// Sources
// ---------------------------------------------------------------------------
enum class ModSource { None = 0, Lfo1, Lfo2, Env1, Env2, Shaper, Velocity, ModWheel, Count };
inline constexpr int numModSources = (int) ModSource::Count;
inline constexpr int numModSlots = 16;
inline constexpr int shaperTableSize = 1024;

inline const juce::StringArray& modSourceNames()
{
    static const juce::StringArray n { "None", "LFO 1", "LFO 2", "Env 1", "Env 2", "Shaper", "Velocity", "Mod wheel" };
    return n;
}
inline bool isBipolar (ModSource s) noexcept { return s == ModSource::Lfo1 || s == ModSource::Lfo2; }

// ---------------------------------------------------------------------------
// Drawable shaper curve. Edited on the message thread, read on the audio thread
// through a table of relaxed atomics (no locks, no tearing worth worrying about).
// ---------------------------------------------------------------------------
struct ShaperPoint
{
    float x = 0, y = 1, curve = 0;      // curve: tension of the segment to the next point (-1..1)
    bool operator== (const ShaperPoint& o) const { return same (x, o.x) && same (y, o.y) && same (curve, o.curve); }
};

class ShaperCurve
{
public:
    ShaperCurve();
    std::vector<ShaperPoint> getPoints() const;
    void setPoints (std::vector<ShaperPoint> pts);
    float lookup (double phase) const noexcept;        // phase 0..1, audio-thread safe
    static float evaluate (const std::vector<ShaperPoint>& pts, float x) noexcept;

    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree& t);

    static juce::StringArray presetNames();
    static std::vector<ShaperPoint> preset (int index);

    std::atomic<int> version { 0 };

private:
    void rebuild (const std::vector<ShaperPoint>& pts);
    mutable juce::SpinLock lock;
    std::vector<ShaperPoint> points;
    std::array<std::atomic<float>, shaperTableSize + 1> table;
};

// ---------------------------------------------------------------------------
// Modulation matrix. Targets are stored by parameter ID (stable across versions);
// the audio thread sees resolved indices through atomics.
// ---------------------------------------------------------------------------
struct ModSlotData
{
    int source = 0;
    juce::String target;
    float depth = 0.0f;                  // in normalised parameter units, -1..1
    bool isEmpty() const { return source == 0 || target.isEmpty(); }
};

class ModMatrix
{
public:
    std::function<int (const juce::String&)> resolve;      // parameter ID -> index (set by the processor)

    ModSlotData getSlot (int i) const;
    void setSlot (int i, const ModSlotData& d);
    int assign (int source, const juce::String& target, float depth);   // add or update; -1 when full
    void removeTarget (const juce::String& target);
    void clear();
    std::vector<ModSlotData> slotsForTarget (const juce::String& target) const;

    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree& t);

    struct Live { std::atomic<int> source { 0 }, target { -1 }; std::atomic<float> depth { 0.0f }; };
    std::array<Live, numModSlots> live;
    std::atomic<int> version { 0 };

private:
    void publish (int i);
    mutable juce::SpinLock lock;
    std::array<ModSlotData, numModSlots> data;
};

// ---------------------------------------------------------------------------
// Audio-thread modulation engine
// ---------------------------------------------------------------------------
struct Transport
{
    double bpm = 120.0, ppq = 0.0;      // ppq at the start of the current chunk
    bool playing = false;
};

struct ShaperRun
{
    double phase0 = 0.0, inc = 0.0;     // per sample
    float amount = 0.0f;
    float smoothCoef = 0.0f;
    const ShaperCurve* curve = nullptr;
};

class ModEngine
{
public:
    void prepare (double sampleRate);
    void reset();
    void noteOn (float velocity) noexcept;
    void modWheel (float value) noexcept { wheel = value; }

    // Advances all sources by one chunk. 'vals' are the effective parameter values of the
    // previous chunk (so sources can themselves be modulated with one chunk of delay).
    void processChunk (const float* const* in, int nc, const float* const* sc, int scCh, int n,
                       const Transport& tr, double latencySamples, const float* vals, const ParamIndex& ix) noexcept;

    float value (ModSource s) const noexcept { return values[(size_t) s]; }
    const ShaperRun& shaper() const noexcept { return shaperRun; }

    // live source values for the UI
    std::array<std::atomic<float>, numModSources> display {};
    std::atomic<float> shaperPhaseDisplay { 0.0f };
    std::array<std::atomic<float>, numLfos> lfoPhaseDisplay {};

private:
    struct Lfo
    {
        double phase = 0.0;
        float out = 0.0f, smoothed = 0.0f;
        float randPrev = 0.0f, randNext = 0.0f;
        bool retrigPending = false;
    };
    float lfoShape (Lfo& l, int shape, double phase, bool wrapped) noexcept;
    float random() noexcept;

    double fs = 48000.0;
    std::array<Lfo, numLfos> lfos;
    std::array<double, numEnvs> envs {};
    double shaperPhase = 0.0;
    bool shaperRetrig = false;
    float velocity = 0.0f, wheel = 0.0f;
    uint32_t rng = 0x1234567u;
    std::array<float, numModSources> values {};
    ShaperRun shaperRun;

public:
    const ShaperCurve* curve = nullptr;    // set by the processor
};

} // namespace klinge
