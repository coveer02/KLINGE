#include "Band.h"
#include "CompTables.h"

namespace klinge
{
// Grid / input scaling of the circuit models: digital full scale -> volts or field units
namespace scale
{
    inline constexpr float triodeVolts = 1.5f;
    inline constexpr float stackVolts  = 1.2f;
    inline constexpr float diodeVolts  = 0.35f;
    inline constexpr float hystField   = 1.0f;
    inline constexpr float ironUnits   = 1.0f;
}

float compensationFor (Algo a, float driveGain, float driveDb, float shape, float bias) noexcept
{
    switch (a)
    {
        case Algo::Triode:     return comp::lookup (comp::triode, driveDb, shape);
        case Algo::Diode:      return comp::lookup (comp::diode, driveDb, shape);
        case Algo::Hysteresis: return comp::lookup (comp::hysteresis, driveDb, shape);
        case Algo::Iron:       return comp::lookup (comp::iron, driveDb, shape);
        case Algo::Stack:      return comp::lookup (comp::stack, driveDb, shape);
        case Algo::Crush:      return 1.0f;
        case Algo::Silk: case Algo::Tube: case Algo::Tape: case Algo::Detail: case Algo::Amp: case Algo::Grind:
        case Algo::Fuzz: case Algo::Fold: case Algo::Rectify: case Algo::Slew: case Algo::Count:
            break;
    }
    return levelCompensation (a, driveGain, shape, bias);
}

void BandProcessor::ChannelState::resetMemory()
{
    ampPeak.reset(); ampCab1.reset(); ampCab2.reset(); detailHP.reset();
    ampTight.reset(); tapeLP.reset(); fuzzEnv.reset(); sagEnv.reset();
    adaaA.reset(); adaaB.reset();
    triode.reset(); diode.reset(); hyst.reset(); iron.reset(); stack.reset();
    slewPrev = crushHeld = 0; crushPhase = 1;
}

// ===========================================================================
void BandProcessor::prepare (double fsOversampled, int osFactor)
{
    fs = fsOversampled;
    os = std::max (1, osFactor);
    useAdaa = true;   // measured: hard curves (Amp, Detail, Grind) still gain ~10 dB at 8x and 16x
    for (auto& s : st)
    {
        s.dynFast.setup (fs, 1.0, 40.0);
        s.dynSlow.setup (fs, 200.0, 400.0);
        s.normEnv.setup (fs, 0.0, 80.0);
        s.fuzzEnv.setup (fs, 1.0, 30.0);
        s.sagEnv.setup (fs, 5.0, 120.0);
        s.bandSag.setup (fs, 4.0, 160.0);
        s.bandDynEnv.setup (fs, 1.0, 80.0);
        s.noiseEnv.setup (fs, 1.0, 50.0);
        s.dc.setup (fs, 8.0);
        s.detailHP.highpass (fs, 1200.0);
        s.ampPeak.peak (fs, 1100.0, 0.8, 6.0);
        s.ampCab1.lowpass (fs, 5200.0, 0.9);
        s.ampCab2.lowpass (fs, 9000.0, 0.6);
        s.triode.prepare (fs);
        s.diode.prepare (fs);
        s.iron.prepare (fs);
        s.stack.prepare (fs);
    }
    lastShapeForFilters = -1.0f;
    lastTypeForFilters = Algo::Count;
    compType = Algo::Count;
    reset();
}

void BandProcessor::reset()
{
    for (auto& s : st)
    {
        s.resetMemory();
        s.dc.reset();
        s.dynFast.reset(); s.dynSlow.reset(); s.normEnv.reset(); s.bandSag.reset(); s.bandDynEnv.reset(); s.noiseEnv.reset();
        s.dynGain = s.dynGainTarget = 1.0f; s.dynStep = 0.0f; s.dynCounter = 0;
        s.ddGain = 1.0f; s.ddStep = 0.0f; s.ddCounter = 0;
    }
    type = pendingType;
    invalidateAdaa();
    lastShapeForFilters = -1.0f;
    updateShapeFilters();
    for (auto* r : { &gain, &comp, &harm, &mix, &level, &biasOff, &biasDc, &onAmt, &gate })
        r->snap (r->target);
    typeFade.snap (1.0f);
    peak = 0;
}

void BandProcessor::applyType (Algo t)
{
    type = t;
    for (auto& s : st) s.resetMemory();       // shared envelope followers stay warm
    lastShapeForFilters = -1.0f;
    updateShapeFilters();
}

void BandProcessor::updateShapeFilters()
{
    if (same (shape, lastShapeForFilters) && type == lastTypeForFilters)
        return;
    lastShapeForFilters = shape;
    lastTypeForFilters = type;

    const double tapeFc  = 20000.0 * std::pow (0.15, (double) shape);
    const double tightFc = (double) lerp (320.0f, 25.0f, shape);
    for (auto& s : st)
    {
        s.tapeLP.lowpass (fs, tapeFc);
        s.ampTight.highpass (fs, tightFc);
        switch (type)
        {
            case Algo::Triode:     s.triode.setDrift (shape); break;
            case Algo::Diode:      s.diode.setTone (shape); break;
            case Algo::Hysteresis: s.hyst.setWidth (shape); break;
            case Algo::Iron:       s.iron.setCore (shape); break;
            case Algo::Stack:      s.stack.setVoice (shape); break;
            case Algo::Silk: case Algo::Tube: case Algo::Tape: case Algo::Detail: case Algo::Amp: case Algo::Grind:
            case Algo::Fuzz: case Algo::Fold: case Algo::Rectify: case Algo::Crush: case Algo::Slew: case Algo::Count:
                break;
        }
    }
}

void BandProcessor::setParams (const BandParams& p, float globalDriveDb, float dynAmount, const HarmonicRecipe& r,
                               float analog, int rampSamples, int fadeSamples)
{
    // ---- algorithm switching: fade the wet part out, switch, fade back in (dry stays continuous)
    bool justSwitched = false;
    pendingType = p.type;
    if (pendingType != type)
    {
        typeFade.setTarget (0.0f, fadeSamples);
        if (typeFade.current <= 0.0f && typeFade.isSettled())
        {
            applyType (pendingType);
            typeFade.setTarget (1.0f, fadeSamples);
            justSwitched = true;
        }
    }
    else
    {
        typeFade.setTarget (1.0f, fadeSamples);
    }

    const float newShape = clampf (p.shape, 0.0f, 1.0f);
    if (! same (newShape, shape))
    {
        shape = newShape;
        invalidateAdaa();
        updateShapeFilters();
    }
    dynAmt = clampf (dynAmount, -1.0f, 1.0f);
    recipe = r;
    hasHarm = p.harm > 0.0005f && ! r.isZero();
    sagAmt = clampf (p.sag, 0.0f, 1.0f);
    bandDyn = clampf (p.dyn, -1.0f, 1.0f);
    threshDb = clampf (p.threshDb, -80.0f, 0.0f);
    dynPost = p.dynPost;
    stereo = p.stereo;

    analogAmt = clampf (analog, 0.0f, 1.0f);
    chanGain = { 1.0f + 0.03f * analogAmt, 1.0f - 0.03f * analogAmt };      // component tolerance between channels
    chanBias = { 0.025f * analogAmt, -0.015f * analogAmt };

    const float totalDb = clampf (p.driveDb + globalDriveDb, -24.0f, maxTotalDrive);
    float g = dbToGain (totalDb);
    const float bTarget = clampf (p.bias, -1.0f, 1.0f) * 0.6f;
    const bool circuit = isCircuit ((int) type);
    const float dcTarget = circuit ? 0.0f : staticCurve (type, bTarget, shape);

    if (type != compType || ! same (g, compGain) || ! same (shape, compShape) || ! same (bTarget, compBias))
    {
        compType = type; compGain = g; compShape = shape; compBias = bTarget;
        compValue = compensationFor (type, g, totalDb, shape, bTarget);
    }
    float c = compValue;

    if (type == Algo::Crush)
    {
        const float intensity = clampf (totalDb / 48.0f, 0.0f, 1.0f);
        const float bitsAmt = intensity * std::min (1.0f, 2.0f * (1.0f - shape));
        const float rateAmt = intensity * std::min (1.0f, 2.0f * shape);
        crushBits = 16.0f - 13.5f * std::pow (bitsAmt, 0.6f);
        crushHoldInc = 1.0f / (1.0f + 39.0f * std::pow (rateAmt, 1.6f) * (float) os);
        g = 1.0f; c = 1.0f;
    }

    const int levelRamp = justSwitched ? 0 : rampSamples;
    gain.setTarget (g, levelRamp);
    comp.setTarget (c, levelRamp);
    biasDc.setTarget (dcTarget, levelRamp);
    biasOff.setTarget (bTarget, levelRamp);
    harm.setTarget (hasHarm ? p.harm : 0.0f, rampSamples);
    mix.setTarget (clampf (p.mix, 0.0f, 1.0f), rampSamples);
    level.setTarget (dbToGain (p.levelDb), rampSamples);
    onAmt.setTarget (p.on ? 1.0f : 0.0f, fadeSamples);
    gate.setTarget (p.mute ? 0.0f : 1.0f, fadeSamples);
}

void BandProcessor::tick() noexcept
{
    gain.next(); comp.next(); harm.next(); mix.next(); level.next();
    biasOff.next(); biasDc.next(); onAmt.next(); gate.next(); typeFade.next();
}

// Band compressor / expander. The gain computer runs every 8 samples and the
// gain is interpolated in between.
float BandProcessor::bandDynamicsGain (ChannelState& s, float detect) noexcept
{
    const float env = s.bandDynEnv.process (detect);
    if (--s.dynCounter <= 0)
    {
        s.dynCounter = 8;
        const float lvl = gainToDb (env);
        float grDb = 0.0f;
        constexpr float knee = 6.0f;
        if (bandDyn < 0.0f)
        {
            const float slope = 1.0f - 1.0f / (1.0f + 7.0f * -bandDyn);     // up to 8:1
            const float over = lvl - threshDb;
            if (over > knee * 0.5f)       grDb = -slope * over;
            else if (over > -knee * 0.5f) grDb = -slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        }
        else
        {
            const float slope = 3.0f * bandDyn;                             // up to 1:4 expansion
            const float under = threshDb - lvl;
            if (under > knee * 0.5f)       grDb = -slope * under;
            else if (under > -knee * 0.5f) grDb = -slope * (under + knee * 0.5f) * (under + knee * 0.5f) / (2.0f * knee);
            grDb = std::max (grDb, -40.0f);
        }
        s.dynGainTarget = dbToGain (grDb);
        s.dynStep = (s.dynGainTarget - s.dynGain) / 8.0f;
    }
    s.dynGain += s.dynStep;
    return s.dynGain;
}

float BandProcessor::distort (ChannelState& s, float u0, float g, float bo, float bd, float ax, int si) noexcept
{
    const Algo t = type;
    const float sh = shape;
    auto curveF = [t, sh] (float z) { return staticCurve (t, z, sh); };
    auto curveA = [t, sh] (double z) { return antiderivative (t, z, sh); };
    const bool adaa = useAdaa;
    auto nl = [&s, adaa, &curveF, &curveA] (float z) { return adaa ? s.adaaA.process ((double) z, curveF, curveA) : curveF (z); };
    const float x = u0;

    switch (type)
    {
        case Algo::Silk:
        case Algo::Grind:
        case Algo::Fold:
        case Algo::Rectify:
            return nl (x * g + bo) - bd;

        case Algo::Fuzz:
        {
            float y = nl (x * g + bo) - bd;
            const float env = s.fuzzEnv.process (ax);
            if (shape > 0.001f)
            {
                const float th = 0.12f * shape * shape;
                const float gg = clampf (env / th, 0.0f, 1.0f);
                y *= gg * gg;
            }
            return y;
        }

        case Algo::Tube:
        {
            const float u = x * g;
            const float sag = s.sagEnv.process (std::abs (u));
            const float b = bo - 0.35f * shape * std::min (sag, 2.0f);
            return nl (u + b) - curves::tubeCurve (b, shape);
        }

        case Algo::Tape:
            return s.tapeLP.process (nl (x * g + bo) - bd);

        case Algo::Detail:
        {
            const float u = x * g + bo;
            float uRef = u, cl;
            if (adaa)
            {
                uRef = 0.5f * (u + (float) s.adaaA.x1);              // aligned with the ADAA half-sample delay
                cl = s.adaaA.process ((double) u,
                                      [] (float z) { return clampf (z, -1.0f, 1.0f); },
                                      [] (double z) { return anti::clamp1 (z); });
            }
            else cl = clampf (u, -1.0f, 1.0f);
            const float detail = s.detailHP.process (uRef - cl);
            float v = cl + shape * detail - bd;
            const float av = std::abs (v);
            if (av > 1.0f) v = std::copysign (1.0f + 0.3f * std::tanh ((av - 1.0f) / 0.3f), v);
            return v;
        }

        case Algo::Amp:
        {
            static const float off = std::tanh (0.25f);
            float u = s.ampPeak.process (x * g);
            u = s.ampTight.process (u) + bo;
            float s2;
            if (adaa)
            {
                const float s1 = s.adaaA.process ((double) u,
                                                  [] (float z) { return std::tanh (1.7f * z + 0.25f); },
                                                  [] (double z) { return anti::logcosh (1.7 * z + 0.25) / 1.7; });
                s2 = s.adaaB.process ((double) (3.2f * (s1 - off)),
                                      [] (float z) { return curves::cubicClip (z); },
                                      [] (double z) { return anti::cubic (z); });
            }
            else s2 = curves::ampStage (u);
            return s.ampCab2.process (s.ampCab1.process (s2 - bd));
        }

        case Algo::Crush:
        {
            s.crushPhase += crushHoldInc;
            if (s.crushPhase >= 1.0f)
            {
                s.crushPhase -= std::floor (s.crushPhase);
                const float q = std::exp2 (crushBits - 1.0f);
                s.crushHeld = std::round (clampf (x + bo, -1.0f, 1.0f) * q) / q;
            }
            return s.crushHeld - bo;
        }

        case Algo::Slew:
        {
            const float u = x * g + bo;
            const float m = (0.8f * std::pow (1.0f - shape, 3.0f) + 0.003f) * (float) (48000.0 / fs);
            s.slewPrev += clampf (u - s.slewPrev, -m, m);
            if (! adaa) return std::tanh (s.slewPrev) - bd;
            return s.adaaA.process ((double) s.slewPrev,
                                    [] (float z) { return std::tanh (z); },
                                    [] (double z) { return anti::logcosh (z); }) - bd;
        }

        // ---- circuit models (input in volts / field units, output normalised)
        case Algo::Triode:
            return (float) s.triode.process ((double) (x * g * scale::triodeVolts), (double) bo * 2.0);
        case Algo::Diode:
            return (float) (s.diode.process ((double) ((x * g + bo * 0.5f) * scale::diodeVolts)) / scale::diodeVolts);
        case Algo::Hysteresis:
            return (float) s.hyst.process ((double) ((x * g + bo) * scale::hystField));
        case Algo::Iron:
            return (float) s.iron.process ((double) (x * g * scale::ironUnits), (double) bo * 1.5);
        case Algo::Stack:
            return (float) s.stack.process ((double) (x * g * scale::stackVolts), (double) bo * 2.0);

        case Algo::Count:
            break;
    }
    juce::ignoreUnused (si);
    return x;
}

float BandProcessor::processSample (float x, int si) noexcept
{
    auto& s = st[(size_t) si];
    const float ax = std::abs (x);
    const float dF = s.dynFast.process (ax);
    const float dS = s.dynSlow.process (ax);

    const float gt = gate.current;
    if (gt <= 0.0f) return 0.0f;                       // fully muted; followers stay warm

    // ---- band dynamics before the distortion
    float xin = x;
    const bool dynOn = bandDyn < -1.0e-3f || bandDyn > 1.0e-3f;
    if (dynOn && ! dynPost)
        xin *= bandDynamicsGain (s, ax);

    // ---- drive, global dynamics, sag
    float g = gain.current * chanGain[(size_t) si];
    if (dynAmt < -1.0e-4f || dynAmt > 1.0e-4f)
    {
        if (--s.ddCounter <= 0)
        {
            s.ddCounter = 4;
            const float rr = clampf ((dF + 1.0e-5f) / (dS + 1.0e-5f), 0.063f, 7.94f);
            s.ddStep = (std::pow (rr, dynAmt) - s.ddGain) * 0.25f;
        }
        s.ddGain += s.ddStep;
        g *= s.ddGain;
    }
    else { s.ddGain = 1.0f; s.ddStep = 0.0f; }
    float bo = biasOff.current + chanBias[(size_t) si];
    if (sagAmt > 1.0e-4f)
    {
        const float e = std::min (s.bandSag.process (std::abs (xin) * g), 2.0f);
        g /= 1.0f + sagAmt * 1.5f * e;                 // supply sag: loud passages pull the gain down
        bo += sagAmt * 0.2f * std::min (e, 1.5f);      // and shift the operating point (even harmonics bloom)
    }

    float y = distort (s, xin, g, bo, biasDc.current, std::abs (xin), si);
    y *= comp.current;

    if (hasHarm)
    {
        const float env = s.normEnv.process (std::abs (xin));
        y += harm.current * harmonicStatic (recipe, xin, env);
    }

    y = s.dc.process (y);

    if (dynOn && dynPost)
        y *= bandDynamicsGain (s, std::abs (y));

    if (analogAmt > 1.0e-4f)
    {
        // signal-dependent noise (like tape modulation noise): silent input stays silent
        s.rng = s.rng * 1664525u + 1013904223u;
        const float n = (float) (int32_t) s.rng * (1.0f / 2147483648.0f);
        y += n * analogAmt * 2.0e-3f * s.noiseEnv.process (std::abs (y));
    }

    const float wet = mix.current * onAmt.current * typeFade.current;
    float out = (x + wet * (y - x)) * level.current * gt;
    if (! std::isfinite (out)) { s.resetMemory(); s.dc.reset(); out = 0.0f; }
    peak = std::max (peak, std::abs (out));
    return out;
}

} // namespace klinge
