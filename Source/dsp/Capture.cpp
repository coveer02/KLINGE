#include "Capture.h"
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

namespace klinge
{
CaptureModel::~CaptureModel() = default;

CaptureStage::CaptureStage()
{
    for (auto* r : { &inGain, &outGain, &mixAmt, &onAmt }) r->snap (0.0f);
    inGain.snap (1.0f); outGain.snap (1.0f); mixAmt.snap (1.0f);
    startTimerHz (10);
}

CaptureStage::~CaptureStage()
{
    stopTimer();
    pool.removeAllJobs (true, 5000);
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
    delete active;
}

juce::String CaptureStage::getModelName() const { const juce::SpinLock::ScopedLockType l (infoLock); return modelName; }
juce::String CaptureStage::getModelPath() const { const juce::SpinLock::ScopedLockType l (infoLock); return modelPath; }
juce::String CaptureStage::getStatus() const    { const juce::SpinLock::ScopedLockType l (infoLock); return status; }
bool CaptureStage::hasModel() const             { return getModelPath().isNotEmpty(); }

void CaptureStage::loadAsync (const juce::File& file)
{
    {
        const juce::SpinLock::ScopedLockType l (infoLock);
        status = "Loading " + file.getFileName() + " ...";
    }
    const double rate = fs;
    const int block = maxN;
    juce::WeakReference<CaptureStage> weak (this);
    pool.addJob ([weak, file, rate, block]
    {
        auto m = std::make_shared<CaptureModel>();
        juce::String err;
        try
        {
            for (int c = 0; c < 2; ++c)
            {
                m->dsp[c] = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()));
                if (m->dsp[c] == nullptr) throw std::runtime_error ("unsupported model");
                if (m->dsp[c]->NumInputChannels() != 1 || m->dsp[c]->NumOutputChannels() != 1)
                    throw std::runtime_error ("only mono models are supported");
                m->dsp[c]->Reset (rate, block);
            }
            m->preparedRate = rate;
            m->preparedBlock = block;
            m->expectedRate = m->dsp[0]->GetExpectedSampleRate();
            if (m->dsp[0]->HasLoudness())
                m->normGain = (float) juce::Decibels::decibelsToGain (-18.0 - m->dsp[0]->GetLoudness());
            m->name = file.getFileNameWithoutExtension();
            m->path = file.getFullPathName();
        }
        catch (const std::exception& e) { err = e.what(); if (err.isEmpty()) err = "invalid file"; }

        juce::MessageManager::callAsync ([weak, m, err, file]
        {
            auto* self = weak.get();
            if (self == nullptr) return;
            const juce::SpinLock::ScopedLockType l (self->infoLock);
            if (err.isNotEmpty() || m->dsp[0] == nullptr)
            {
                self->status = "Could not load " + file.getFileName() + ": " + err;
                return;
            }
            auto owned = std::make_unique<CaptureModel>();
            owned->dsp[0] = std::move (m->dsp[0]); owned->dsp[1] = std::move (m->dsp[1]);
            owned->name = m->name; owned->path = m->path; owned->expectedRate = m->expectedRate;
            owned->preparedRate = m->preparedRate; owned->preparedBlock = m->preparedBlock; owned->normGain = m->normGain;
            self->modelName = owned->name;
            self->modelPath = owned->path;
            self->status = owned->expectedRate > 0 && std::abs (owned->expectedRate - self->fs) > 1.0
                               ? "Trained at " + juce::String (owned->expectedRate / 1000.0, 1) + " kHz, running at "
                                     + juce::String (self->fs / 1000.0, 1) + " kHz: the tone may differ slightly."
                               : juce::String ("Loaded.");
            self->queued = std::move (owned);
            self->clearRequested = false;
        });
    });
}

void CaptureStage::clearModel()
{
    queued.reset();
    delete pending.exchange (nullptr);     // not yet seen by the audio thread
    clearRequested = true;
    const juce::SpinLock::ScopedLockType l (infoLock);
    modelName.clear(); modelPath.clear(); status.clear();
}

// Message thread: hand queued models to the audio thread when the exchange slot is free,
// and free models the audio thread has retired.
void CaptureStage::timerCallback()
{
    delete retired.exchange (nullptr);
    if (queued != nullptr && pending.load() == nullptr)
    {
        // a model prepared for another rate or block size is re-prepared here, before the audio thread sees it
        if (std::abs (queued->preparedRate - fs) > 0.5 || queued->preparedBlock < maxN)
        {
            for (auto& d : queued->dsp) if (d) d->Reset (fs, maxN);
            queued->preparedRate = fs;
            queued->preparedBlock = maxN;
        }
        pending.store (queued.release());
    }
}

void CaptureStage::prepare (double sampleRate, int maxBlock)
{
    // audio is stopped here, so the active model can be reset directly
    fs = sampleRate;
    maxN = std::max (32, maxBlock);
    for (int c = 0; c < 2; ++c) { bufIn[c].assign ((size_t) maxN, 0.0f); bufOut[c].assign ((size_t) maxN, 0.0f); }
    if (auto* p = pending.exchange (nullptr)) { delete active; active = p; }
    if (active != nullptr)
    {
        for (auto& d : active->dsp) if (d) d->Reset (fs, maxN);
        active->preparedRate = fs;
        active->preparedBlock = maxN;
    }
}

void CaptureStage::reset()
{
    for (auto* r : { &inGain, &outGain, &mixAmt, &onAmt }) r->snap (r->target);
}

void CaptureStage::process (float* const* ch, int nc, int n, const CaptureSettings& s) noexcept
{
    if (clearRequested.load() && retired.load() == nullptr)
    {
        clearRequested = false;
        retired.store (active);          // freed later on the message thread
        active = nullptr;
    }
    if (auto* p = pending.load())
    {
        if (retired.load() == nullptr && pending.compare_exchange_strong (p, nullptr))
        {
            retired.store (active);
            active = p;
            if (active != nullptr && active->dsp[0] == nullptr) { delete active; active = nullptr; }
        }
    }

    const bool usable = active != nullptr && active->dsp[0] != nullptr && n <= maxN;
    onAmt.setTarget (s.on && usable ? 1.0f : 0.0f, n);
    inGain.setTarget (dbToGain (s.inDb), n);
    outGain.setTarget (dbToGain (s.outDb) * (usable ? active->normGain : 1.0f), n);
    mixAmt.setTarget (clampf (s.mix, 0.0f, 1.0f), n);
    if (! usable || (onAmt.current <= 0.0f && onAmt.isSettled()))
    {
        for (int i = 0; i < n; ++i) { onAmt.next(); inGain.next(); outGain.next(); mixAmt.next(); }
        return;
    }

    const bool stereo = s.stereo && nc > 1;
    // mono mode: the model runs on the mid signal, the side passes through
    for (int i = 0; i < n; ++i)
    {
        const float gi = inGain.next();
        if (stereo) { bufIn[0][(size_t) i] = ch[0][i] * gi; bufIn[1][(size_t) i] = ch[1][i] * gi; }
        else bufIn[0][(size_t) i] = (nc > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i]) * gi;
    }
    for (int c = 0; c < (stereo ? 2 : 1); ++c)
    {
        float* ip = bufIn[c].data();
        float* op = bufOut[c].data();
        active->dsp[c]->process (&ip, &op, n);
    }
    for (int i = 0; i < n; ++i)
    {
        const float on = onAmt.next(), go = outGain.next(), mx = mixAmt.next() * on;
        if (stereo)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float w = bufOut[c][(size_t) i] * go;
                ch[c][i] += mx * (w - ch[c][i]);
            }
        }
        else
        {
            const float w = bufOut[0][(size_t) i] * go;
            if (nc > 1)
            {
                const float m = 0.5f * (ch[0][i] + ch[1][i]), sd = 0.5f * (ch[0][i] - ch[1][i]);
                const float nm = m + mx * (w - m);
                ch[0][i] = nm + sd; ch[1][i] = nm - sd;
            }
            else ch[0][i] += mx * (w - ch[0][i]);
        }
        if (! std::isfinite (ch[0][i])) ch[0][i] = 0;
        if (nc > 1 && ! std::isfinite (ch[1][i])) ch[1][i] = 0;
    }
}

} // namespace klinge
