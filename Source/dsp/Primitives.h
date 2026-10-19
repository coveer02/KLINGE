#pragma once

#include <cmath>
#include <algorithm>

namespace klinge
{
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr double kPiD = 3.14159265358979323846;

inline float dbToGain (float db) noexcept   { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept    { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float clampf (float x, float lo, float hi) noexcept { return std::min (std::max (x, lo), hi); }
inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

// Exact comparison without tripping -Wfloat-equal (used for "did this value change" checks)
inline bool same (float a, float b) noexcept { return ! (a < b) && ! (a > b); }

// Flush tiny values in recursive paths (belt and braces on top of FTZ/DAZ)
inline double undenorm (double x) noexcept { return std::abs (x) < 1.0e-30 ? 0.0 : x; }

// ---------------------------------------------------------------------------
// All recursive filters keep coefficients and state in double precision.
// At high oversampling rates (up to 16x at 192 kHz = 3 MHz) low cutoffs put
// poles within ~1e-5 of the unit circle, where single precision is not enough.
// ---------------------------------------------------------------------------

// Biquad (transposed direct form II), RBJ cookbook designs
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0.0; }

    inline float process (float xf) noexcept
    {
        const double x = xf;
        const double y = b0 * x + z1;
        z1 = undenorm (b1 * x - a1 * y + z2);
        z2 = undenorm (b2 * x - a2 * y);
        return (float) y;
    }

    void copyCoeffs (const Biquad& o) noexcept { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }
    void setIdentity() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    void setNormalised (double B0, double B1, double B2, double A0, double A1, double A2) noexcept
    {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0;
        a1 = A1 / A0; a2 = A2 / A0;
    }

    // Cutoff is kept inside (0.1 Hz, 0.49 fs): poles stay strictly inside the unit circle.
    static void wcos (double fs, double f, double q, double& cw, double& alpha) noexcept
    {
        f = std::min (std::max (f, 0.1), fs * 0.49);
        q = std::max (q, 0.05);
        const double w = 2.0 * kPiD * f / fs;
        cw = std::cos (w);
        alpha = std::sin (w) / (2.0 * q);
    }

    void lowpass (double fs, double f, double q = 0.7071067811865476) noexcept
    {
        double cw, al; wcos (fs, f, q, cw, al);
        setNormalised ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + al, -2 * cw, 1 - al);
    }
    void highpass (double fs, double f, double q = 0.7071067811865476) noexcept
    {
        double cw, al; wcos (fs, f, q, cw, al);
        setNormalised ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + al, -2 * cw, 1 - al);
    }
    void allpass (double fs, double f, double q = 0.7071067811865476) noexcept
    {
        double cw, al; wcos (fs, f, q, cw, al);
        setNormalised (1 - al, -2 * cw, 1 + al, 1 + al, -2 * cw, 1 - al);
    }
    void peak (double fs, double f, double q, double gainDb) noexcept
    {
        double cw, al; wcos (fs, f, q, cw, al);
        const double A = std::pow (10.0, gainDb / 40.0);
        setNormalised (1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A);
    }
    void lowShelf (double fs, double f, double gainDb, double q = 0.7071067811865476) noexcept
    {
        double cw, al; wcos (fs, f, q, cw, al);
        const double A = std::pow (10.0, gainDb / 40.0), sA = 2.0 * std::sqrt (A) * al;
        setNormalised (A * ((A + 1) - (A - 1) * cw + sA), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - sA),
                       (A + 1) + (A - 1) * cw + sA, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sA);
    }
    void highShelf (double fs, double f, double gainDb, double q = 0.7071067811865476) noexcept
    {
        double cw, al; wcos (fs, f, q, cw, al);
        const double A = std::pow (10.0, gainDb / 40.0), sA = 2.0 * std::sqrt (A) * al;
        setNormalised (A * ((A + 1) + (A - 1) * cw + sA), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sA),
                       (A + 1) - (A - 1) * cw + sA, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sA);
    }
};

// ---------------------------------------------------------------------------
// 2-pole state-variable filter, topology-preserving transform (Zavalishin).
// Unlike direct-form biquads it stays well-behaved while the cutoff moves, so
// automated sweeps do not zip or click. The cutoff coefficient is interpolated
// per sample towards each new target.
// ---------------------------------------------------------------------------
struct Svf
{
    double ic1 = 0, ic2 = 0;
    double g = 0.0, gTarget = 0.0, gStep = 0.0;
    double k = 1.4142135623730951;           // 1/Q, Butterworth
    double a1 = 1, a2 = 0, a3 = 0;
    int remaining = 0;

    void reset() noexcept { ic1 = ic2 = 0; }

    void setCutoff (double fs, double f, int rampSamples) noexcept
    {
        gTarget = std::tan (kPiD * std::min (std::max (f, 1.0), fs * 0.49) / fs);
        if (rampSamples <= 0 || g <= 0.0) { g = gTarget; remaining = 0; updateCoeffs(); return; }
        remaining = rampSamples;
        gStep = (gTarget - g) / rampSamples;
    }

    void updateCoeffs() noexcept
    {
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    // returns the low-pass output; 'hp' receives the high-pass output
    inline double tick (double x, double& hp) noexcept
    {
        double bp;
        return tick3 (x, bp, hp);
    }

    // low-pass return, band-pass and high-pass via references
    inline double tick3 (double x, double& bp, double& hp) noexcept
    {
        if (remaining > 0) { g = (--remaining == 0) ? gTarget : g + gStep; updateCoeffs(); }
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = undenorm (2.0 * v1 - ic1);
        ic2 = undenorm (2.0 * v2 - ic2);
        bp = v1;
        hp = x - k * v1 - v2;
        return v2;
    }

    // 2nd-order Butterworth all-pass (the phase of an LR4 crossover sum)
    inline double allpass (double x) noexcept
    {
        double bp, hp;
        tick3 (x, bp, hp);
        return x - 2.0 * k * bp;
    }
};

// A low or high cut that can be switched off without a click: the filter keeps
// running and the output crossfades between filtered and unfiltered signal.
struct CutFilter
{
    Svf svf;
    bool highpass = false;
    float on = 0.0f, onTarget = 0.0f, onStep = 0.0f;

    void reset() noexcept { svf.reset(); }
    void set (double fs, double f, bool enabled, int rampSamples, int fadeSamples) noexcept
    {
        svf.setCutoff (fs, f, rampSamples);
        onTarget = enabled ? 1.0f : 0.0f;
        if (fadeSamples <= 0) { on = onTarget; onStep = 0; }
        else onStep = (onTarget - on) / (float) fadeSamples;
    }
    inline float process (float x) noexcept
    {
        double hp;
        const double lp = svf.tick (x, hp);
        const float y = (float) (highpass ? hp : lp);
        if (! same (on, onTarget))
        {
            on += onStep;
            if ((onStep > 0 && on > onTarget) || (onStep < 0 && on < onTarget)) on = onTarget;
        }
        return x + on * (y - x);
    }
};

// First-order sections (bilinear, prewarped)
struct OnePole
{
    double b0 = 1, b1 = 0, a1 = 0, z = 0;
    void reset() noexcept { z = 0; }
    inline float process (float xf) noexcept
    {
        const double x = xf;
        const double y = b0 * x + z;
        z = undenorm (b1 * x - a1 * y);
        return (float) y;
    }
    static double warp (double fs, double f) noexcept
    {
        return std::tan (kPiD * std::min (std::max (f, 0.1), fs * 0.49) / fs);
    }
    void lowpass (double fs, double f) noexcept
    {
        const double K = warp (fs, f);
        b0 = K / (1 + K); b1 = b0; a1 = (K - 1) / (1 + K);
    }
    void highpass (double fs, double f) noexcept
    {
        const double K = warp (fs, f);
        b0 = 1 / (1 + K); b1 = -b0; a1 = (K - 1) / (1 + K);
    }
    // Minimum-phase tilt shelf H(s) = (G s + w) / (s + w). 'inverse' gives the exact inverse filter,
    // which is stable for every G > 0 (its pole sits at (G - K) / (G + K), inside the unit circle).
    void tiltShelf (double fs, double f, double G, bool inverse) noexcept
    {
        G = std::max (G, 1.0e-3);
        const double K = warp (fs, f);
        double nb0 = G + K, nb1 = K - G, na0 = 1 + K, na1 = K - 1;
        if (inverse) { std::swap (nb0, na0); std::swap (nb1, na1); }
        b0 = nb0 / na0; b1 = nb1 / na0; a1 = na1 / na0;
    }
};

// DC blocker (one zero at DC, one pole just inside)
struct DCBlocker
{
    double R = 0.9995, x1 = 0, y1 = 0;
    void setup (double fs, double fc = 5.0) noexcept { R = std::exp (-2.0 * kPiD * fc / fs); }
    void reset() noexcept { x1 = y1 = 0; }
    inline float process (float xf) noexcept
    {
        const double x = xf;
        const double y = x - x1 + R * y1;
        x1 = x; y1 = undenorm (y);
        return (float) y;
    }
};

// Peak envelope follower with separate attack/release (double state: no stalling at high rates)
struct EnvFollower
{
    double att = 0.0, rel = 0.0, env = 0.0;
    static double coeff (double fs, double ms) noexcept { return ms <= 0.0 ? 0.0 : std::exp (-1.0 / (fs * ms * 0.001)); }
    void setup (double fs, double attackMs, double releaseMs) noexcept { att = coeff (fs, attackMs); rel = coeff (fs, releaseMs); }
    void reset (double v = 0.0) noexcept { env = v; }
    inline float process (float xAbs) noexcept
    {
        const double x = xAbs;
        const double c = x > env ? att : rel;
        env = undenorm (x + c * (env - x));
        return (float) env;
    }
};

// Mean-square (power) follower, symmetric time constant
struct PowerFollower
{
    double c = 0.0, p = 0.0;
    void setup (double fs, double ms) noexcept { c = EnvFollower::coeff (fs, ms); }
    void reset (double v = 0.0) noexcept { p = v; }
    inline float process (float xf) noexcept
    {
        const double x2 = (double) xf * xf;
        p = undenorm (x2 + c * (p - x2));
        return (float) p;
    }
};

// Linear per-sample ramp for parameter changes
struct Ramp
{
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int remaining = 0;
    void snap (float v) noexcept { current = target = v; remaining = 0; step = 0; }
    void setTarget (float v, int numSteps) noexcept
    {
        if (same (v, target) && remaining > 0) return;
        target = v;
        if (numSteps <= 0 || same (v, current)) { current = v; remaining = 0; return; }
        remaining = numSteps;
        step = (target - current) / (float) numSteps;
    }
    inline float next() noexcept
    {
        if (remaining > 0) { current += step; if (--remaining == 0) current = target; }
        return current;
    }
    bool isSettled() const noexcept { return remaining == 0; }
};

// Control-rate exponential smoother (updated once per processing chunk)
struct CtlSmoother
{
    float value = 0.0f;
    bool initialised = false;
    // Returns true when the value moved. Log mode smooths frequencies evenly per octave.
    bool update (float target, float k, bool logDomain) noexcept
    {
        if (! initialised) { value = target; initialised = true; return true; }
        if (same (value, target)) return false;
        if (logDomain)
        {
            const float lv = std::log (value), lt = std::log (target);
            const float n = lv + k * (lt - lv);
            value = std::abs (lt - n) < 1.0e-4f ? target : std::exp (n);
        }
        else
        {
            const float n = value + k * (target - value);
            value = std::abs (target - n) < 1.0e-4f ? target : n;
        }
        return true;
    }
    void snapTo (float v) noexcept { value = v; initialised = true; }
};

} // namespace klinge
