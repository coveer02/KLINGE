#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Params.h"
#include "Band.h"
#include "Modulation.h"
#include "Capture.h"

namespace klinge
{
struct EngineParams
{
    float inGainDb = 0, driveDb = 12, preHP = 20, preLP = 20000, focusDb = 0;
    float dynDrive = 0, punch = 0, punchSpeedMs = 40, toneLock = 0, postHP = 20, postLP = 20000;
    float mix = 1, outDb = 0;
    std::array<float, 4> xover { 200, 3000, 6000, 10000 };
    int bandCount = 3;
    float fbAmount = 0, fbTimeMs = 3, fbTone = 6000, analog = 0;
    Routing routing = Routing::Single;
    bool autoGain = true, delta = false, clipOut = false;
    bool bypass = false, nonRealtime = false;
    int osIndex = 2, osQuality = 0, renderOs = 0;
    HarmonicRecipe recipe;
    std::array<BandParams, numBands> bands;
    CaptureSettings capture;
};

// Builds engine parameters from the flat array of effective parameter values
void buildEngineParams (const float* v, const ParamIndex& ix, EngineParams& p) noexcept;

// Live values for the UI (written by the audio thread only)
struct EngineMeters
{
    std::atomic<float> inPeak { 0 }, outPeak { 0 }, punchDb { 0 }, autoGainDb { 0 }, lockLowDb { 0 }, lockHighDb { 0 };
    std::array<std::atomic<float>, numBands> bandPeak {};
    std::atomic<float> shaperGain { 1.0f };
};

// Single-producer / single-consumer sample FIFO for the analyser and scope
class ScopeFifo
{
public:
    explicit ScopeFifo (int size = 32768) : fifo (size), data ((size_t) size, 0.0f) {}
    void push (const float* x, int n) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        if (n1 > 0) std::copy (x, x + n1, data.begin() + s1);
        if (n2 > 0) std::copy (x + n1, x + n1 + n2, data.begin() + s2);
        fifo.finishedWrite (n1 + n2);
    }
    int ready() const noexcept { return fifo.getNumReady(); }
    int pull (float* dest, int maxN) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (maxN, s1, n1, s2, n2);
        if (n1 > 0) std::copy (data.begin() + s1, data.begin() + s1 + n1, dest);
        if (n2 > 0) std::copy (data.begin() + s2, data.begin() + s2 + n2, dest + n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }
private:
    juce::AbstractFifo fifo;
    std::vector<float> data;
};

// ---------------------------------------------------------------------------
// Full signal path, processed in chunks of up to kChunk samples
// ---------------------------------------------------------------------------
class Engine
{
public:
    static constexpr int kChunk = 32;

    Engine();
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setCapture (CaptureStage* c) noexcept { capture = c; }

    void beginBlock() noexcept;
    void processChunk (float* const* io, int nc, int n, const EngineParams& p, const ShaperRun& shp) noexcept;
    void endBlock() noexcept;

    static int renderIndex (int osIndex, int renderMode) noexcept;
    int latencyOf (int osIndex, int osQuality) const;
    int latencyFor (int osIndex, int osQuality, int renderMode) const;   // reported latency (max of live and render)
    int getLatencySamples() const noexcept { return latency.load(); }

    EngineMeters meters;
    ScopeFifo scopeIn, scopeOut;
    std::atomic<bool> scopeEnabled { false };

private:
    struct Structure
    {
        int os = -1, q = -1, pad = 0, bands = 0;
        Routing routing = Routing::Single;
        bool capPost = false;
        bool operator!= (const Structure& o) const
        {
            return os != o.os || q != o.q || pad != o.pad || bands != o.bands || routing != o.routing || capPost != o.capPost;
        }
    };
    Structure wantedStructure (const EngineParams& p, int nc) const noexcept;
    void applyStructure (const Structure& s) noexcept;
    void handleStructure (const EngineParams& p, int nc) noexcept;
    void updateControl (const EngineParams& p, int n) noexcept;
    juce::dsp::Oversampling<float>* wetOS() noexcept { return osStages[(size_t) cur.q][(size_t) cur.os].get(); }
    juce::dsp::Oversampling<float>* dryOS() noexcept { return dryStages[(size_t) cur.q][(size_t) cur.os].get(); }
    int activeBands() const noexcept { return cur.routing == Routing::Single ? 1 : (cur.routing == Routing::MidSide ? 2 : cur.bands); }

    double fs = 44100.0;
    int numCh = 2;
    Structure cur;
    int osFactor = 1, osExtra = 0;
    std::atomic<int> latency { 0 };
    bool firstChunk = true, lastNonRealtime = false;
    CaptureStage* capture = nullptr;

    std::array<std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 5>, 2> osStages, dryStages;
    std::array<BandProcessor, numBands> bands;

    // multiband split (Linkwitz-Riley 4th order from TPT state-variable filters)
    struct Splitter
    {
        std::array<Svf, 4> a, lp2, hp2;                   // per split: shared first stage, second LP, second HP
        std::array<std::array<Svf, 4>, 4> ap;             // ap[band][split]: phase compensation for lower bands
        void reset() { for (auto& s : a) s.reset(); for (auto& s : lp2) s.reset(); for (auto& s : hp2) s.reset(); for (auto& r : ap) for (auto& s : r) s.reset(); }
    };

    struct Chan
    {
        CutFilter preHP, preLP, postHP, postLP;
        CutFilter refHP, refLP;
        Biquad lockLow, lockHigh;
        OnePole emph, deEmph, analogLP;
        Splitter split;
        std::array<float, 128> osRing {};
        int osRingPos = 0;
        // feedback (oversampled rate)
        std::vector<float> fbLine;
        int fbPos = 0;
        float fbState = 0.0f;
        OnePole fbLP, fbHP;
        EnvFollower fbEnv;            // feedback only rings while there is input
        std::array<float, 512> padRing {};
        int padPos = 0;
    };
    std::array<Chan, 2> ch;

    struct DryChan
    {
        std::array<Svf, 4> ap;
        std::array<float, 128> ring {};
        int pos = 0;
        void reset() { for (auto& s : ap) s.reset(); ring.fill (0.0f); pos = 0; }
    };
    std::array<DryChan, 2> dryCh;

    struct Analysis
    {
        Biquad lo1, lo2, hi1, hi2, mHP, mLP;
        PowerFollower pLo, pMid, pHi;
        void reset() { for (auto* b : { &lo1, &lo2, &hi1, &hi2, &mHP, &mLP }) b->reset(); pLo.reset (1e-8); pMid.reset (1e-8); pHi.reset (1e-8); }
    } anDry, anWet;
    float lockLowDb = 0, lockHighDb = 0;

    EnvFollower cFastD, cSlowD, cFastW, cSlowW;
    OnePole punchSmooth;
    float lastPunchSpeed = -1;

    PowerFollower agDry, agWet, agDryFast, agWetFast;
    float agHold = 1.0f, agLast = 1.0f;
    double agUp = 0.0, agDown = 0.0;

    float focusMakeup = 1.0f, deEmphMakeup = 1.0f, deEmphFocus = -1000.0f, curFocus = 0.0f;
    std::array<float, 16> focusHist {};
    unsigned chunkCounter = 0;
    std::array<float, 4> curXover { -1, -1, -1, -1 };
    float curFbTone = -1.0f;
    double shaperGain = 1.0;

    Ramp inGain, outGain, mixRamp, deltaRamp, clipRamp, fade, fbGain, fbDelay, analogAmt;
    int holdRemaining = 0;
    juce::AudioBuffer<float> work, dryBuf;
    std::array<float, kChunk> scopeTmp {};

    float inPk = 0, outPk = 0;
    double punchAcc = 0;
    int punchCount = 0;
};

} // namespace klinge
