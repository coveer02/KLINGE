#pragma once

#include "Shapers.h"
#include "Circuits.h"
#include "Params.h"

namespace klinge
{
struct BandParams
{
    Algo type = Algo::Tube;
    float driveDb = 0.0f, shape = 0.5f, bias = 0.0f, harm = 0.0f, mix = 1.0f, levelDb = 0.0f;
    float sag = 0.0f;                    // 0..1
    float dyn = 0.0f, threshDb = -24.0f; // dyn: -1 = compress, +1 = expand
    bool dynPost = false;
    BandStereo stereo = BandStereo::Stereo;
    bool on = true, solo = false, mute = false;
};

// Level compensation for any algorithm (memoryless curves analytically, circuits from tables)
float compensationFor (Algo a, float driveGain, float driveDb, float shape, float bias) noexcept;

// ---------------------------------------------------------------------------
// One distortion band. Runs at the oversampled rate and holds two channel states.
// ---------------------------------------------------------------------------
class BandProcessor
{
public:
    void prepare (double fsOversampled, int osFactor);
    void reset();
    void setParams (const BandParams& p, float globalDriveDb, float dynAmount, const HarmonicRecipe& recipe,
                    float analog, int rampSamples, int fadeSamples);
    void tick() noexcept;                              // advance all ramps by one frame
    float processSample (float x, int stateIndex) noexcept;
    float passGain() const noexcept { return level.current * gate.current; }   // for the undistorted half in Mid/Side mode
    BandStereo stereoMode() const noexcept { return stereo; }
    float takePeak() noexcept { const float p = peak; peak = 0; return p; }

private:
    struct ChannelState
    {
        Biquad ampPeak, ampCab1, ampCab2, detailHP;
        OnePole ampTight, tapeLP;
        DCBlocker dc;
        EnvFollower dynFast, dynSlow, normEnv, fuzzEnv, sagEnv, bandSag, bandDynEnv, noiseEnv;
        Adaa adaaA, adaaB;
        circuits::TriodeStage triode;
        circuits::DiodeClipper diode;
        circuits::Hysteresis hyst;
        circuits::IronCore iron;
        circuits::StackAmp stack;
        float slewPrev = 0, crushHeld = 0, crushPhase = 1;
        float dynGain = 1.0f, dynGainTarget = 1.0f, dynStep = 0.0f;
        int dynCounter = 0;
        float ddGain = 1.0f, ddStep = 0.0f;      // global Dynamics drive factor, updated every 4 samples
        int ddCounter = 0;
        uint32_t rng = 0x9e3779b9u;
        void resetMemory();
    };

    void applyType (Algo t);
    void updateShapeFilters();
    void invalidateAdaa() { for (auto& c : st) { c.adaaA.invalidate(); c.adaaB.invalidate(); } }
    float bandDynamicsGain (ChannelState& s, float detect) noexcept;
    float distort (ChannelState& s, float u, float g, float bo, float bd, float ax, int si) noexcept;

    std::array<ChannelState, 2> st;
    double fs = 44100.0;
    int os = 1;
    bool useAdaa = true;     // anti-derivative anti-aliasing (switchable for profiling and tests)

    Algo type = Algo::Tube, pendingType = Algo::Tube;
    float shape = 0.5f, dynAmt = 0.0f;
    float lastShapeForFilters = -1.0f;
    Algo lastTypeForFilters = Algo::Count;
    float crushBits = 16.0f, crushHoldInc = 1.0f;
    HarmonicRecipe recipe;
    bool hasHarm = false;
    float sagAmt = 0.0f, bandDyn = 0.0f, threshDb = -24.0f;
    bool dynPost = false;
    BandStereo stereo = BandStereo::Stereo;
    float analogAmt = 0.0f;
    std::array<float, 2> chanGain { 1.0f, 1.0f }, chanBias { 0.0f, 0.0f };

    Algo compType = Algo::Count;
    float compGain = -1.0f, compShape = -1.0f, compBias = -99.0f, compValue = 1.0f;

    Ramp gain, comp, harm, mix, level, biasOff, biasDc;
    Ramp onAmt, gate, typeFade;
    float peak = 0;
};

} // namespace klinge
