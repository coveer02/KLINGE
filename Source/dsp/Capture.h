#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Primitives.h"

namespace nam { class DSP; }

namespace klinge
{
// ---------------------------------------------------------------------------
// Capture: runs a Neural Amp Modeler profile (.nam) inside KLINGE.
// Models are loaded on a background thread and handed to the audio thread
// through a lock-free single-slot exchange; old models are freed on the
// message thread.
// ---------------------------------------------------------------------------
struct CaptureModel
{
    std::unique_ptr<nam::DSP> dsp[2];
    juce::String name, path;
    double expectedRate = -1.0, preparedRate = -1.0;
    int preparedBlock = 0;
    float normGain = 1.0f;
    ~CaptureModel();
};

struct CaptureSettings
{
    bool on = false, post = false, stereo = false;
    float inDb = 0, outDb = 0, mix = 1;
};

class CaptureStage : private juce::Timer
{
public:
    CaptureStage();
    ~CaptureStage() override;

    // message thread
    void loadAsync (const juce::File& file);
    void clearModel();
    juce::String getModelName() const;
    juce::String getModelPath() const;
    juce::String getStatus() const;
    bool hasModel() const;

    // prepare / audio thread
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* const* ch, int nc, int n, const CaptureSettings& s) noexcept;

private:
    void timerCallback() override;
    void install (std::unique_ptr<CaptureModel> m);

    std::atomic<CaptureModel*> pending { nullptr }, retired { nullptr };
    CaptureModel* active = nullptr;                          // audio thread only (and prepare)
    std::unique_ptr<CaptureModel> queued;                    // message thread: waiting for a free slot
    std::atomic<bool> clearRequested { false };

    mutable juce::SpinLock infoLock;
    juce::String modelName, modelPath, status;

    double fs = 48000.0;
    int maxN = 512;
    std::vector<float> bufIn[2], bufOut[2];
    Ramp inGain, outGain, mixAmt, onAmt;
    juce::ThreadPool pool { 1 };
    JUCE_DECLARE_WEAK_REFERENCEABLE (CaptureStage)
};

} // namespace klinge
