#include "Engine.h"

namespace klinge
{
// ===========================================================================
// Parameter mapping
// ===========================================================================
void buildEngineParams (const float* v, const ParamIndex& ix, EngineParams& p) noexcept
{
    p.inGainDb = v[ix.inGain];
    p.driveDb = v[ix.drive];
    p.preHP = v[ix.preHP]; p.preLP = v[ix.preLP];
    p.focusDb = v[ix.focus];
    p.dynDrive = v[ix.dynDrive] * 0.01f;
    p.punch = v[ix.punch] * 0.01f;
    p.punchSpeedMs = v[ix.punchSpeed];
    p.toneLock = v[ix.toneLock] * 0.01f;
    p.postHP = v[ix.postHP]; p.postLP = v[ix.postLP];
    p.mix = v[ix.mix] * 0.01f;
    p.outDb = v[ix.outGain];
    for (int s = 0; s < 4; ++s) p.xover[(size_t) s] = v[ix.xover[s]];
    p.bandCount = juce::jlimit (2, numBands, 2 + (int) v[ix.bandCount]);
    p.fbAmount = v[ix.fbAmount] * 0.01f;
    p.fbTimeMs = v[ix.fbTime];
    p.fbTone = v[ix.fbTone];
    p.analog = v[ix.analog] * 0.01f;
    p.routing = (Routing) juce::jlimit (0, 2, (int) v[ix.routing]);
    p.autoGain = v[ix.autoGain] > 0.5f;
    p.delta = v[ix.delta] > 0.5f;
    p.clipOut = v[ix.clipOut] > 0.5f;
    p.osIndex = juce::jlimit (0, 4, (int) v[ix.oversample]);
    p.osQuality = juce::jlimit (0, 1, (int) v[ix.osQuality]);
    p.renderOs = juce::jlimit (0, 3, (int) v[ix.renderOs]);
    p.recipe.lock = v[ix.harmLock] * 0.01f;
    for (int n = 0; n < numHarmonics; ++n) p.recipe.h[(size_t) n] = v[ix.harm[n]] * 0.01f;
    for (int b = 0; b < numBands; ++b)
    {
        auto& d = p.bands[(size_t) b];
        const auto& B = ix.band[b];
        d.type = (Algo) juce::jlimit (0, numAlgos - 1, (int) v[B.type]);
        d.driveDb = v[B.drive]; d.shape = v[B.shape] * 0.01f; d.bias = v[B.bias] * 0.01f;
        d.harm = v[B.harm] * 0.01f; d.mix = v[B.mix] * 0.01f; d.levelDb = v[B.level];
        d.sag = v[B.sag] * 0.01f; d.dyn = v[B.dyn] * 0.01f; d.threshDb = v[B.thresh]; d.dynPost = v[B.dynPos] > 0.5f;
        d.stereo = (BandStereo) juce::jlimit (0, 2, (int) v[B.stereo]);
        d.on = v[B.on] > 0.5f; d.solo = v[B.solo] > 0.5f; d.mute = v[B.mute] > 0.5f;
    }
    p.capture.on = v[ix.capOn] > 0.5f;
    p.capture.post = v[ix.capPos] > 0.5f;
    p.capture.stereo = v[ix.capStereo] > 0.5f;
    p.capture.inDb = v[ix.capIn];
    p.capture.outDb = v[ix.capOut];
    p.capture.mix = v[ix.capMix] * 0.01f;
}

// ===========================================================================
// Engine
// ===========================================================================
Engine::Engine()
{
    using OS = juce::dsp::Oversampling<float>;
    for (auto* set : { &osStages, &dryStages })
        for (int q = 0; q < 2; ++q)
            for (int f = 0; f < 5; ++f)
                (*set)[(size_t) q][(size_t) f] = std::make_unique<OS> (
                    2, (size_t) f,
                    q == 0 ? OS::filterHalfBandFIREquiripple : OS::filterHalfBandPolyphaseIIR,
                    true, false);   // latency made integer by an exact delay at the oversampled rate
    latency.store (latencyOf (2, 0));
}

int Engine::renderIndex (int osIndex, int mode) noexcept
{
    switch (mode)
    {
        case 1:  return std::min (4, osIndex + 1);
        case 2:  return std::min (4, osIndex + 2);
        case 3:  return 4;
        default: return osIndex;
    }
}

int Engine::latencyOf (int osIndex, int osQuality) const
{
    osIndex = juce::jlimit (0, 4, osIndex);
    osQuality = juce::jlimit (0, 1, osQuality);
    return (int) std::ceil (osStages[(size_t) osQuality][(size_t) osIndex]->getLatencyInSamples() - 1.0e-4f);
}

int Engine::latencyFor (int osIndex, int osQuality, int renderMode) const
{
    return std::max (latencyOf (osIndex, osQuality), latencyOf (renderIndex (osIndex, renderMode), osQuality));
}

void Engine::prepare (double sampleRate, int, int numChannels)
{
    fs = sampleRate > 0.0 ? sampleRate : 44100.0;
    numCh = juce::jlimit (1, 2, numChannels);

    for (auto* set : { &osStages, &dryStages })
        for (auto& qs : *set)
            for (auto& o : qs)
                o->initProcessing ((size_t) kChunk);

    work.setSize (2, kChunk);
    dryBuf.setSize (4, kChunk);

    for (auto* a : { &anDry, &anWet })
    {
        a->lo1.lowpass (fs, 250); a->lo2.lowpass (fs, 250);
        a->hi1.highpass (fs, 2500); a->hi2.highpass (fs, 2500);
        a->mHP.highpass (fs, 250); a->mLP.lowpass (fs, 2500);
        a->pLo.setup (fs, 300); a->pMid.setup (fs, 300); a->pHi.setup (fs, 300);
    }

    cSlowD.setup (fs, 200, 200);
    cSlowW.setup (fs, 200, 200);
    lastPunchSpeed = -1;
    punchSmooth.lowpass (fs, 300);
    agDry.setup (fs, 400);
    agWet.setup (fs, 400);
    agDryFast.setup (fs, 10);
    agWetFast.setup (fs, 10);
    agUp   = EnvFollower::coeff (fs, 350.0);
    agDown = EnvFollower::coeff (fs, 3.0);

    const size_t fbSize = (size_t) std::ceil (0.032 * fs * 16.0) + 8;
    for (auto& c : ch)
    {
        c.preHP.highpass = c.refHP.highpass = c.postHP.highpass = true;
        c.preLP.highpass = c.refLP.highpass = c.postLP.highpass = false;
        c.fbLine.assign (fbSize, 0.0f);
        c.analogLP.lowpass (fs, 14000.0);
    }

    if (capture != nullptr) capture->prepare (fs, kChunk);

    cur = Structure();
    reset();
}

void Engine::reset()
{
    for (auto& c : ch)
    {
        for (auto* f : { &c.preHP, &c.preLP, &c.refHP, &c.refLP, &c.postHP, &c.postLP }) f->reset();
        c.lockLow.reset(); c.lockHigh.reset();
        c.lockLow.setIdentity(); c.lockHigh.setIdentity();
        c.emph.reset(); c.deEmph.reset(); c.analogLP.reset();
        c.split.reset();
        c.osRing.fill (0.0f); c.osRingPos = 0;
        std::fill (c.fbLine.begin(), c.fbLine.end(), 0.0f);
        c.fbPos = 0; c.fbState = 0.0f;
        c.fbLP.reset(); c.fbHP.reset(); c.fbEnv.reset();
        c.padRing.fill (0.0f); c.padPos = 0;
    }
    for (auto& d : dryCh) d.reset();
    for (auto* set : { &osStages, &dryStages })
        for (auto& qs : *set) for (auto& o : qs) o->reset();
    for (auto& b : bands) b.reset();
    anDry.reset(); anWet.reset();
    lockLowDb = lockHighDb = 0;
    cFastD.reset(); cSlowD.reset(); cFastW.reset(); cSlowW.reset();
    punchSmooth.reset();
    agDry.reset(); agWet.reset(); agDryFast.reset(); agWetFast.reset();
    agHold = 1.0f; agLast = 1.0f;
    for (auto* r : { &inGain, &outGain, &mixRamp, &deltaRamp, &clipRamp, &fbGain, &fbDelay, &analogAmt })
        r->snap (r->target);
    fade.snap (1.0f);
    holdRemaining = 0;
    shaperGain = 1.0;
    curXover = { -1, -1, -1, -1 };
    curFbTone = -1.0f;
    focusHist.fill (curFocus);
    deEmphFocus = -1000.0f;
    firstChunk = true;
}

// ---------------------------------------------------------------------------
// Structure: oversampling, filter type, routing, band count and latency padding.
// Changes are applied at the bottom of a short fade, then the output stays silent
// until the fresh filters are filled, then fades back in. Nothing clicks.
// ---------------------------------------------------------------------------
Engine::Structure Engine::wantedStructure (const EngineParams& p, int nc) const noexcept
{
    Structure s;
    s.q = juce::jlimit (0, 1, p.osQuality);
    s.os = p.nonRealtime ? renderIndex (p.osIndex, p.renderOs) : juce::jlimit (0, 4, p.osIndex);
    s.pad = latencyFor (p.osIndex, s.q, p.renderOs) - latencyOf (s.os, s.q);
    s.routing = p.routing;
    if (nc == 1 && s.routing == Routing::MidSide) s.routing = Routing::Single;
    s.bands = s.routing == Routing::Multiband ? juce::jlimit (2, numBands, p.bandCount) : 0;
    s.capPost = p.capture.post;
    return s;
}

void Engine::applyStructure (const Structure& s) noexcept
{
    const bool osChanged = s.os != cur.os || s.q != cur.q;
    const bool layoutChanged = s.routing != cur.routing || s.bands != cur.bands;
    cur = s;

    if (osChanged)
    {
        osFactor = 1 << cur.os;
        wetOS()->reset();
        dryOS()->reset();
        const float lf = wetOS()->getLatencyInSamples();
        const int lat = std::max (0, (int) std::ceil (lf - 1.0e-4f));
        osExtra = juce::jlimit (0, 127, (int) std::lround (((float) lat - lf) * (float) osFactor));
        for (auto& b : bands) b.prepare (fs * osFactor, osFactor);
        for (auto& c : ch)
        {
            c.emph.reset(); c.deEmph.reset();
            std::fill (c.fbLine.begin(), c.fbLine.end(), 0.0f);
            c.fbPos = 0; c.fbState = 0.0f;
            c.fbLP.reset(); c.fbHP.reset();
            c.fbEnv.setup (fs * osFactor, 1.0, 250.0);
            c.fbEnv.reset();
        }
        curFbTone = -1.0f;
        focusHist.fill (curFocus);
        deEmphFocus = -1000.0f;
    }
    if (osChanged || layoutChanged)
    {
        wetOS()->reset();
        dryOS()->reset();
        for (auto& b : bands) b.reset();
        for (auto& c : ch) { c.split.reset(); c.osRing.fill (0.0f); c.osRingPos = 0; }
        for (auto& d : dryCh) d.reset();
        curXover = { -1, -1, -1, -1 };
    }
    for (auto& c : ch) { c.padRing.fill (0.0f); c.padPos = 0; }
    latency.store (latencyOf (cur.os, cur.q) + cur.pad);
}

void Engine::handleStructure (const EngineParams& p, int nc) noexcept
{
    const Structure want = wantedStructure (p, nc);
    const bool nrtChanged = p.nonRealtime != lastNonRealtime;
    lastNonRealtime = p.nonRealtime;

    if (cur.os < 0 || firstChunk || nrtChanged)
    {
        if (want != cur || cur.os < 0) applyStructure (want);
        fade.snap (1.0f);
        holdRemaining = 0;
        return;
    }
    if (holdRemaining > 0)
        return;

    const int declick = std::max (1, (int) (0.005 * fs));
    if (! (want != cur))
        fade.setTarget (1.0f, declick);
    else if (fade.current <= 0.0f && fade.isSettled())
    {
        applyStructure (want);
        holdRemaining = 2 * latencyOf (cur.os, cur.q) + 64;   // let the reset filters fill before fading in
    }
    else
        fade.setTarget (0.0f, declick);
}

// ---------------------------------------------------------------------------
// Control-rate update. Values arrive already smoothed (and modulated) from the
// processor, so ramps only interpolate across the chunk.
// ---------------------------------------------------------------------------
void Engine::updateControl (const EngineParams& p, int n) noexcept
{
    const bool snap = firstChunk;
    firstChunk = false;
    const int interp = snap ? 0 : n;
    const int interpOS = snap ? 0 : n * osFactor;
    const int fadeBase = snap ? 0 : (int) (0.003 * fs);
    const double fo = fs * osFactor;

    // ---- pre / post cuts
    {
        const float hp = std::max (p.preHP, 20.0f), lp = clampf (p.preLP, 1000.0f, 20000.0f);
        const float php = std::max (p.postHP, 20.0f), plp = clampf (p.postLP, 1000.0f, 20000.0f);
        for (auto& c : ch)
        {
            c.preHP.set (fs, hp, hp > 20.5f, interp, fadeBase);
            c.refHP.set (fs, hp, hp > 20.5f, interp, fadeBase);
            c.preLP.set (fs, lp, lp < 19900.0f, interp, fadeBase);
            c.refLP.set (fs, lp, lp < 19900.0f, interp, fadeBase);
            c.postHP.set (fs, php, php > 20.5f, interp, fadeBase);
            c.postLP.set (fs, plp, plp < 19900.0f, interp, fadeBase);
        }
    }

    // ---- focus: emphasis now, de-emphasis with the same value once it has passed the latency
    const float focus = clampf (p.focusDb, -12.0f, 12.0f);
    if (! same (focus, curFocus) || snap)
    {
        curFocus = focus;
        const double G = dbToGain (focus);
        for (auto& c : ch) c.emph.tiltShelf (fs, 650.0, G, false);
        focusMakeup = (float) (1.0 / std::sqrt (G));
    }
    if (snap) focusHist.fill (curFocus);
    focusHist[chunkCounter & 15u] = curFocus;
    const unsigned delayChunks = (unsigned) juce::jlimit (0, 15, (latency.load() + kChunk / 2) / kChunk);
    const float df = focusHist[(chunkCounter - delayChunks) & 15u];
    if (! same (df, deEmphFocus))
    {
        deEmphFocus = df;
        const double G = dbToGain (df);
        for (auto& c : ch) c.deEmph.tiltShelf (fs, 650.0, G, true);
        deEmphMakeup = (float) std::sqrt (G);
    }

    // ---- crossovers (strictly increasing, interpolated across the chunk)
    {
        float prev = 0.0f;
        for (int s = 0; s < 4; ++s)
        {
            float f = clampf (p.xover[(size_t) s], 30.0f, 16000.0f);
            if (s > 0) f = std::max (f, prev * 1.2f);
            f = std::min (f, (float) (fo * 0.45));
            prev = f;
            if (same (f, curXover[(size_t) s])) continue;
            const int r = curXover[(size_t) s] < 0 ? 0 : interpOS;
            curXover[(size_t) s] = f;
            for (auto& c : ch)
            {
                c.split.a[(size_t) s].setCutoff (fo, f, r);
                c.split.lp2[(size_t) s].setCutoff (fo, f, r);
                c.split.hp2[(size_t) s].setCutoff (fo, f, r);
                for (int b = 0; b < s; ++b) c.split.ap[(size_t) b][(size_t) s].setCutoff (fo, f, r);
            }
            for (auto& d : dryCh) d.ap[(size_t) s].setCutoff (fo, f, r);
        }
    }

    if (! same (p.punchSpeedMs, lastPunchSpeed))
    {
        lastPunchSpeed = p.punchSpeedMs;
        const double ms = juce::jlimit (1.0, 1000.0, (double) p.punchSpeedMs);
        cFastD.setup (fs, 0.5, ms);
        cFastW.setup (fs, 0.5, ms);
    }

    // ---- bands
    const int nb = activeBands();
    bool anySolo = false;
    for (int b = 0; b < nb; ++b) anySolo = anySolo || p.bands[(size_t) b].solo;
    const int fadeOS = snap ? 0 : (int) (0.004 * fo);
    for (int b = 0; b < numBands; ++b)
    {
        auto bp = p.bands[(size_t) b];
        bp.mute = bp.mute || (b < nb && anySolo && ! bp.solo);
        bands[(size_t) b].setParams (bp, p.driveDb, p.dynDrive, p.recipe, p.bypass ? 0.0f : p.analog, interpOS, fadeOS);
    }

    // ---- feedback
    if (! same (p.fbTone, curFbTone))
    {
        curFbTone = p.fbTone;
        for (auto& c : ch) { c.fbLP.lowpass (fo, clampf (p.fbTone, 300.0f, 20000.0f)); c.fbHP.highpass (fo, 30.0); }
    }
    const float maxDelay = (float) ch[0].fbLine.size() - 4.0f;
    fbGain.setTarget (p.bypass ? 0.0f : clampf (p.fbAmount, 0.0f, 0.95f), interpOS);
    fbDelay.setTarget (clampf (p.fbTimeMs * 0.001f * (float) fo, 1.0f, maxDelay), interpOS);

    // ---- global ramps (bypass = unity gain, fully dry)
    const int boolFade = snap ? 0 : (int) (0.02 * fs);
    inGain.setTarget (p.bypass ? 1.0f : dbToGain (p.inGainDb), interp);
    outGain.setTarget (p.bypass ? 1.0f : dbToGain (p.outDb), interp);
    mixRamp.setTarget (p.bypass ? 0.0f : clampf (p.mix, 0.0f, 1.0f), interp);
    analogAmt.setTarget (p.bypass ? 0.0f : clampf (p.analog, 0.0f, 1.0f), interp);
    deltaRamp.setTarget (! p.bypass && p.delta ? 1.0f : 0.0f, boolFade);
    clipRamp.setTarget (! p.bypass && p.clipOut ? 1.0f : 0.0f, boolFade);
}

// ---------------------------------------------------------------------------
void Engine::beginBlock() noexcept
{
    inPk = outPk = 0.0f;
    punchAcc = 0.0;
    punchCount = 0;
}

void Engine::endBlock() noexcept
{
    meters.inPeak.store (std::max (meters.inPeak.load(), inPk));
    meters.outPeak.store (std::max (meters.outPeak.load(), outPk));
    meters.punchDb.store (gainToDb ((float) (punchAcc / std::max (1, punchCount))));
    meters.autoGainDb.store (gainToDb (agLast));
    meters.lockLowDb.store (lockLowDb);
    meters.lockHighDb.store (lockHighDb);
    meters.shaperGain.store ((float) shaperGain);
    for (int b = 0; b < numBands; ++b)
        meters.bandPeak[(size_t) b].store (std::max (meters.bandPeak[(size_t) b].load(), bands[(size_t) b].takePeak()));
}

void Engine::processChunk (float* const* io, int nc, int n, const EngineParams& p, const ShaperRun& shp) noexcept
{
    nc = std::min (nc, numCh);
    if (nc <= 0 || n <= 0) return;
    n = std::min (n, kChunk);

    handleStructure (p, nc);
    updateControl (p, n);

    const bool multiband = cur.routing == Routing::Multiband;
    const bool midSide   = cur.routing == Routing::MidSide;
    const int nb = activeBands();
    float* wk[2] = { work.getWritePointer (0), work.getWritePointer (1) };
    float* dr[4] = { dryBuf.getWritePointer (0), dryBuf.getWritePointer (1),
                     dryBuf.getWritePointer (2), dryBuf.getWritePointer (3) };
    if (nc == 1) { std::fill (wk[1], wk[1] + n, 0.0f); std::fill (dr[1], dr[1] + n, 0.0f); }

    // ---- 1. input and pre filters (base rate)
    for (int i = 0; i < n; ++i)
    {
        const float gIn = inGain.next();
        for (int c = 0; c < nc; ++c)
        {
            auto& C = ch[(size_t) c];
            const float x = io[c][i] * gIn;
            inPk = std::max (inPk, std::abs (x));
            dr[c][i] = x;
            wk[c][i] = C.preLP.process (C.preHP.process (x));
        }
    }
    if (scopeEnabled.load (std::memory_order_relaxed))
    {
        for (int i = 0; i < n; ++i) scopeTmp[(size_t) i] = nc > 1 ? 0.5f * (dr[0][i] + dr[1][i]) : dr[0][i];
        scopeIn.push (scopeTmp.data(), n);
    }

    if (capture != nullptr && ! cur.capPost)
        capture->process (wk, nc, n, p.capture);

    for (int c = 0; c < nc; ++c)
    {
        auto& C = ch[(size_t) c];
        for (int i = 0; i < n; ++i) wk[c][i] = C.emph.process (wk[c][i]) * focusMakeup;
    }

    // ---- 2. dry: identical oversampling round trip, crossover phase, alignment delay
    {
        juce::dsp::AudioBlock<float> dblock (dr, 2, (size_t) n);
        auto dUp = dryOS()->processSamplesUp (dblock);
        const int dN = (int) dUp.getNumSamples();
        if (multiband || osExtra > 0)
        {
            for (int c = 0; c < nc; ++c)
            {
                auto& D = dryCh[(size_t) c];
                float* d = dUp.getChannelPointer ((size_t) c);
                for (int i = 0; i < dN; ++i)
                {
                    double v = d[i];
                    if (multiband)
                        for (int s = 0; s < nb - 1; ++s) v = D.ap[(size_t) s].allpass (v);
                    float fv = (float) v;
                    if (osExtra > 0)
                    {
                        D.ring[(size_t) D.pos] = fv;
                        fv = D.ring[(size_t) ((D.pos - osExtra) & 127)];
                        D.pos = (D.pos + 1) & 127;
                    }
                    d[i] = fv;
                }
            }
        }
        dryOS()->processSamplesDown (dblock);
        for (int c = 0; c < nc; ++c)
        {
            auto& C = ch[(size_t) c];
            for (int i = 0; i < n; ++i) dr[2 + c][i] = C.refLP.process (C.refHP.process (dr[c][i]));
        }
    }

    if (midSide)
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (wk[0][i] + wk[1][i]), s = 0.5f * (wk[0][i] - wk[1][i]);
            wk[0][i] = m; wk[1][i] = s;
        }

    // ---- 3. oversampled distortion
    {
        juce::dsp::AudioBlock<float> block (wk, (size_t) nc, (size_t) n);
        auto up = wetOS()->processSamplesUp (block);
        const int upN = (int) up.getNumSamples();
        float* u[2] = { up.getChannelPointer (0), nc > 1 ? up.getChannelPointer (1) : nullptr };
        const int fbSize = (int) ch[0].fbLine.size();

        for (int i = 0; i < upN; ++i)
        {
            for (int b = 0; b < nb; ++b) bands[(size_t) b].tick();
            const float fg = fbGain.next(), fd = fbDelay.next();

            float x[2] = { 0, 0 }, y[2] = { 0, 0 };
            for (int c = 0; c < nc; ++c)
            {
                auto& C = ch[(size_t) c];
                const float gate = std::min (1.0f, 100.0f * C.fbEnv.process (std::abs (u[c][i])));   // -40 dBFS knee
                x[c] = u[c][i] + fg * gate * C.fbState;
            }

            if (multiband)
            {
                float bs[2][numBands] {};
                for (int c = 0; c < nc; ++c)
                {
                    auto& S = ch[(size_t) c].split;
                    double rest = x[c];
                    for (int s = 0; s < nb - 1; ++s)
                    {
                        double bp, hp;
                        const double lpA = S.a[(size_t) s].tick3 (rest, bp, hp);
                        double dummy;
                        double lo = S.lp2[(size_t) s].tick (lpA, dummy);
                        double bp2, hp2;
                        S.hp2[(size_t) s].tick3 (hp, bp2, hp2);
                        for (int t = s + 1; t < nb - 1; ++t) lo = S.ap[(size_t) s][(size_t) t].allpass (lo);
                        bs[c][s] = (float) lo;
                        rest = hp2;
                    }
                    bs[c][nb - 1] = (float) rest;
                }
                for (int b = 0; b < nb; ++b)
                {
                    auto& B = bands[(size_t) b];
                    if (nc < 2 || B.stereoMode() == BandStereo::Stereo)
                    {
                        for (int c = 0; c < nc; ++c) y[c] += B.processSample (bs[c][b], c);
                    }
                    else
                    {
                        const float m = 0.5f * (bs[0][b] + bs[1][b]), sd = 0.5f * (bs[0][b] - bs[1][b]);
                        float ym, ys;
                        if (B.stereoMode() == BandStereo::Mid) { ym = B.processSample (m, 0); ys = sd * B.passGain(); }
                        else                                   { ys = B.processSample (sd, 0); ym = m * B.passGain(); }
                        y[0] += ym + ys; y[1] += ym - ys;
                    }
                }
            }
            else if (midSide)
            {
                for (int c = 0; c < nc; ++c) y[c] = bands[(size_t) c].processSample (x[c], 0);   // band 1 = mid, band 2 = side
            }
            else
            {
                auto& B = bands[0];
                if (nc < 2 || B.stereoMode() == BandStereo::Stereo)
                {
                    for (int c = 0; c < nc; ++c) y[c] = B.processSample (x[c], c);
                }
                else
                {
                    const float m = 0.5f * (x[0] + x[1]), sd = 0.5f * (x[0] - x[1]);
                    float ym, ys;
                    if (B.stereoMode() == BandStereo::Mid) { ym = B.processSample (m, 0); ys = sd * B.passGain(); }
                    else                                   { ys = B.processSample (sd, 0); ym = m * B.passGain(); }
                    y[0] = ym + ys; y[1] = ym - ys;
                }
            }

            for (int c = 0; c < nc; ++c)
            {
                auto& C = ch[(size_t) c];
                // feedback: delayed, filtered and soft-limited copy of the output, added at the input
                C.fbLine[(size_t) C.fbPos] = y[c];
                // read position in double: in float, fbPos - delay + size can round up to exactly size
                const double rp = (double) C.fbPos - (double) clampf (fd, 0.0f, (float) (fbSize - 2));
                const double rpos = rp < 0.0 ? rp + (double) fbSize : rp;
                int i0 = (int) rpos;
                const float fr = (float) (rpos - (double) i0);
                if (i0 >= fbSize) i0 -= fbSize;
                const float d0 = C.fbLine[(size_t) i0], d1 = C.fbLine[(size_t) ((i0 + 1) % fbSize)];
                if (fg > 0.0f)
                    C.fbState = std::tanh (C.fbHP.process (C.fbLP.process (d0 + fr * (d1 - d0))));
                C.fbPos = C.fbPos + 1 >= fbSize ? 0 : C.fbPos + 1;

                float yy = y[c];
                if (osExtra > 0)
                {
                    C.osRing[(size_t) C.osRingPos] = yy;
                    yy = C.osRing[(size_t) ((C.osRingPos - osExtra) & 127)];
                    C.osRingPos = (C.osRingPos + 1) & 127;
                }
                u[c][i] = yy;
            }
        }
        wetOS()->processSamplesDown (block);
    }

    if (midSide)
        for (int i = 0; i < n; ++i)
        {
            const float l = wk[0][i] + wk[1][i], r = wk[0][i] - wk[1][i];
            wk[0][i] = l; wk[1][i] = r;
        }

    for (int c = 0; c < nc; ++c)
    {
        auto& C = ch[(size_t) c];
        for (int i = 0; i < n; ++i) wk[c][i] = C.deEmph.process (wk[c][i]) * deEmphMakeup;
    }

    if (capture != nullptr && cur.capPost)
        capture->process (wk, nc, n, p.capture);

    // ---- 4. post chain (base rate)
    const float norm = 1.0f / (float) nc;
    const bool shaperOn = shp.curve != nullptr && shp.amount > 0.0f && ! p.bypass;
    for (int i = 0; i < n; ++i)
    {
        float wet[2] = { 0, 0 }, dRaw[2] = { 0, 0 }, dRef[2] = { 0, 0 };
        float dMono = 0, wMono = 0;
        const float an = analogAmt.next();
        for (int c = 0; c < nc; ++c)
        {
            float w = wk[c][i];
            const float lpw = ch[(size_t) c].analogLP.process (w);
            w += an * (lpw - w);                                   // analog bandwidth
            wet[c] = w;
            dRaw[c] = dr[c][i];
            dRef[c] = dr[2 + c][i];
            dMono += dRef[c]; wMono += w;
        }
        dMono *= norm; wMono *= norm;

        auto analyse = [] (Analysis& a, float xx)
        {
            a.pLo.process (a.lo2.process (a.lo1.process (xx)));
            a.pHi.process (a.hi2.process (a.hi1.process (xx)));
            a.pMid.process (a.mLP.process (a.mHP.process (xx)));
        };
        analyse (anDry, dMono);
        analyse (anWet, wMono);

        float dAbs = 0, wAbs = 0;
        for (int c = 0; c < nc; ++c)
        {
            auto& C = ch[(size_t) c];
            wet[c] = C.postLP.process (C.postHP.process (C.lockHigh.process (C.lockLow.process (wet[c]))));
            dAbs = std::max (dAbs, std::abs (dRef[c]));
            wAbs = std::max (wAbs, std::abs (wet[c]));
        }

        // Punch
        const float fD = cFastD.process (dAbs), sD = cSlowD.process (dAbs);
        const float fW = cFastW.process (wAbs), sW = cSlowW.process (wAbs);
        float pg = 1.0f;
        const float punchAmt = p.bypass ? 0.0f : clampf (p.punch, 0.0f, 1.5f);
        if (punchAmt > 1.0e-4f)
        {
            constexpr float e = 1.0e-4f;
            const float ratio = ((fD + e) / (sD + e)) / ((fW + e) / (sW + e));
            pg = clampf (std::pow (ratio, punchAmt), 0.05f, 3.16f);
        }
        pg = punchSmooth.process (pg - 1.0f) + 1.0f;
        punchAcc += pg;
        ++punchCount;

        // Auto gain
        float dPow = 0, wPow = 0;
        for (int c = 0; c < nc; ++c)
        {
            wet[c] *= pg;
            dPow += dRaw[c] * dRaw[c];
            wPow += wet[c] * wet[c];
        }
        const float dLev = std::sqrt (dPow * norm), wLev = std::sqrt (wPow * norm);
        const float pd = agDry.process (dLev), pw = agWet.process (wLev);
        const float pdF = agDryFast.process (dLev), pwF = agWetFast.process (wLev);
        if (pd > 1.0e-9f && pw > 1.0e-12f)
            agHold = clampf (std::sqrt (pd / pw), 0.125f, 8.0f);
        float agTarget = p.autoGain && ! p.bypass ? agHold : 1.0f;
        if (p.autoGain && ! p.bypass && pwF > 1.0e-12f)
            agTarget = std::min (agTarget, std::max (1.0f, std::sqrt (4.0f * std::max (pdF, pd) / pwF)));
        const double kk = agTarget < agLast ? agDown : agUp;
        agLast = (float) (agTarget + kk * (agLast - agTarget));

        // Shaper: rhythmic volume on the processed signal, after loudness matching
        float sg = 1.0f;
        if (shaperOn || shaperGain < 0.99999)
        {
            const float target = shaperOn ? 1.0f - shp.amount * (1.0f - shp.curve->lookup (shp.phase0 + shp.inc * i)) : 1.0f;
            shaperGain = target + shp.smoothCoef * (shaperGain - target);
            sg = (float) shaperGain;
        }

        const float mx = mixRamp.next(), dl = deltaRamp.next(), cr = clipRamp.next();
        const float go = outGain.next();
        float fd;
        if (holdRemaining > 0)
        {
            fd = 0.0f;
            if (--holdRemaining == 0) fade.setTarget (1.0f, std::max (1, (int) (0.005 * fs)));
        }
        else fd = fade.next();

        for (int c = 0; c < nc; ++c)
        {
            auto& C = ch[(size_t) c];
            const float w = wet[c] * agLast * sg;
            const float mixed = dRaw[c] + mx * (w - dRaw[c]);
            float o = mixed + dl * ((w - dRaw[c]) - mixed);
            o *= go;
            if (cr > 0.0f)
            {
                const float a = std::abs (o);
                if (a > 0.891f)
                {
                    const float clipped = std::copysign (0.891f + 0.109f * std::tanh ((a - 0.891f) / 0.109f), o);
                    o += cr * (clipped - o);
                }
            }
            o *= fd;
            if (cur.pad > 0)
            {
                C.padRing[(size_t) C.padPos] = o;
                o = C.padRing[(size_t) ((C.padPos - cur.pad) & 511)];
                C.padPos = (C.padPos + 1) & 511;
            }
            if (! std::isfinite (o)) o = 0.0f;
            outPk = std::max (outPk, std::abs (o));
            io[c][i] = o;
        }
    }
    if (scopeEnabled.load (std::memory_order_relaxed))
    {
        for (int i = 0; i < n; ++i) scopeTmp[(size_t) i] = nc > 1 ? 0.5f * (io[0][i] + io[1][i]) : io[0][i];
        scopeOut.push (scopeTmp.data(), n);
    }

    // ---- 5. Faithful: correction shelves, once per chunk
    {
        const float lockAmt = p.bypass ? 0.0f : clampf (p.toneLock, 0.0f, 1.0f);
        auto ratioDb = [] (double d, double w) { return (float) (10.0 * std::log10 ((d + 1.0e-12) / (w + 1.0e-12))); };
        float tLow = 0, tHigh = 0;
        if (lockAmt > 0.0005f)
        {
            if (anDry.pMid.p > 1.0e-9 && anWet.pMid.p > 1.0e-9)
            {
                const float mid = ratioDb (anDry.pMid.p, anWet.pMid.p);
                tLow  = clampf ((ratioDb (anDry.pLo.p, anWet.pLo.p) - mid) * lockAmt, -18.0f, 18.0f);
                tHigh = clampf ((ratioDb (anDry.pHi.p, anWet.pHi.p) - mid) * lockAmt, -18.0f, 18.0f);
            }
            else { tLow = lockLowDb; tHigh = lockHighDb; }
        }
        const float kl = std::min (1.0f, (float) n / (float) (0.15 * fs));
        float newLow  = lockLowDb  + kl * (tLow  - lockLowDb);
        float newHigh = lockHighDb + kl * (tHigh - lockHighDb);
        if (std::abs (newLow) < 0.001f && std::abs (tLow) < 0.001f)   newLow = 0.0f;
        if (std::abs (newHigh) < 0.001f && std::abs (tHigh) < 0.001f) newHigh = 0.0f;
        if (std::abs (newLow - lockLowDb) > 0.001f || std::abs (newHigh - lockHighDb) > 0.001f
            || (same (newLow, 0.0f) && ! same (lockLowDb, 0.0f)) || (same (newHigh, 0.0f) && ! same (lockHighDb, 0.0f)))
        {
            lockLowDb = newLow;
            lockHighDb = newHigh;
            ch[0].lockLow.lowShelf (fs, 250, lockLowDb, 0.6);
            ch[0].lockHigh.highShelf (fs, 2500, lockHighDb, 0.6);
            ch[1].lockLow.copyCoeffs (ch[0].lockLow);
            ch[1].lockHigh.copyCoeffs (ch[0].lockHigh);
        }
    }

    ++chunkCounter;
}

} // namespace klinge
