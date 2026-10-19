#pragma once

#include "Primitives.h"
#include <array>
#include <juce_core/juce_core.h>

namespace klinge
{
// ---------------------------------------------------------------------------
// Distortion algorithms
// ---------------------------------------------------------------------------
enum class Algo
{
    Silk = 0,  // console-style sine saturation, very clean
    Tube,      // asymmetric, even harmonics, dynamic sag
    Tape,      // soft knee, gentle HF wear
    Detail,    // clipper that restores the fine detail it shaved off
    Amp,       // two-stage high-gain amp with tight control and cab
    Grind,     // crossover distortion + hard clip: grain in the tails
    Fuzz,      // asymmetric fuzz with sputter gate
    Fold,      // wavefolder: level becomes timbre
    Rectify,   // half / full wave -> octave
    Crush,     // bit depth + sample rate reduction
    Slew,      // slew-rate limiting: smears transients into grit
    // ---- v2.0: stateful circuit models
    Triode,    // 12AX7 common-cathode stage: cathode bias, grid conduction, blocking (wandering operating point)
    Diode,     // RC diode clipper (wave digital filter): frequency-dependent clipping
    Hysteresis,// tape magnetisation (Jiles-Atherton): saturation with memory
    Iron,      // transformer core: flux saturation, lows distort first
    Stack,     // two triode stages, interstage voicing, power sag, cabinet
    Count
};

inline bool isCircuit (int a) noexcept { return a >= 11 && a < 16; }

inline constexpr int numAlgos = (int) Algo::Count;

inline const juce::StringArray& algoNames()
{
    static const juce::StringArray n { "Silk", "Tube", "Tape", "Detail", "Amp", "Grind",
                                       "Fuzz", "Fold", "Rectify", "Crush", "Slew",
                                       "Triode", "Diode", "Hysteresis", "Iron", "Stack" };
    return n;
}

// Order in which the algorithms are listed in the UI (grouped by character)
inline const std::array<int, 16>& algoDisplayOrder()
{
    static const std::array<int, 16> o { 0, 2, 13, 14, 1, 11,      // warm: Silk Tape Hysteresis Iron Tube Triode
                                         3, 12, 4, 15, 6, 5,      // clip: Detail Diode Amp Stack Fuzz Grind
                                         7, 8, 9, 10 };           // creative: Fold Rectify Crush Slew
    return o;
}

// Label of the per-algorithm "Shape" control
inline const char* shapeLabel (Algo a)
{
    switch (a)
    {
        case Algo::Silk:    return "Density";
        case Algo::Tube:    return "Sag";
        case Algo::Tape:    return "Wear";
        case Algo::Detail:  return "Detail";
        case Algo::Amp:     return "Body";
        case Algo::Grind:   return "Grain";
        case Algo::Fuzz:    return "Gate";
        case Algo::Fold:    return "Edge";
        case Algo::Rectify: return "Octave";
        case Algo::Crush:   return "Bits/Rate";
        case Algo::Slew:    return "Smear";
        case Algo::Triode:  return "Drift";
        case Algo::Diode:   return "Tone";
        case Algo::Hysteresis: return "Width";
        case Algo::Iron:    return "Core";
        case Algo::Stack:   return "Voice";
        case Algo::Count:   break;
    }
    return "Shape";
}

inline const char* algoDescription (Algo a)
{
    switch (a)
    {
        case Algo::Silk:    return "Console-style sine saturation. Adds density while staying very close to the source.";
        case Algo::Tube:    return "Asymmetric tube stage. Even harmonics; Sag lets the bias breathe with the signal.";
        case Algo::Tape:    return "Soft-knee tape curve. Wear rolls off the top like a tired machine.";
        case Algo::Detail:  return "Clipper that restores the quiet detail clipping removes. Loud, but still alive.";
        case Algo::Amp:     return "Two-stage high-gain amp with cab. Body moves from tight to loose low end.";
        case Algo::Grind:   return "Crossover distortion into a hard clip. Puts grain into tails and quiet parts.";
        case Algo::Fuzz:    return "Asymmetric fuzz. Gate makes quiet parts sputter and break up.";
        case Algo::Fold:    return "Wavefolder. More level means a different timbre, not just more clipping.";
        case Algo::Rectify: return "Rectifier. Octave blends from half wave to full wave (octave up).";
        case Algo::Crush:   return "Bit and sample-rate reduction. Crunch without squashing the dynamics.";
        case Algo::Slew:    return "Slew limiter. Fast edges smear into grit, slow parts pass untouched.";
        case Algo::Triode:  return "Modelled 12AX7 stage. Loud hits push the bias point, so the sound blooms and recovers. Drift sets how far.";
        case Algo::Diode:   return "Modelled diode clipper with capacitor. Lows clip harder than highs. Tone moves the corner.";
        case Algo::Hysteresis: return "Modelled tape magnetisation with memory. Soft, thick and slightly smeared. Width sets the loop.";
        case Algo::Iron:    return "Modelled transformer. Lows saturate first, highs stay open. Core sets how high it reaches.";
        case Algo::Stack:   return "Two modelled triode stages with voicing, power sag and cabinet. Voice goes from tight to loose.";
        case Algo::Count:   break;
    }
    return "";
}

// ---------------------------------------------------------------------------
// Memoryless curves (also used for the transfer-curve and harmonic displays)
// ---------------------------------------------------------------------------
namespace curves
{
    inline float cubicClip (float x) noexcept      // smooth, reaches +-1 at |x| = 1
    {
        if (x <= -1.0f) return -1.0f;
        if (x >= 1.0f)  return 1.0f;
        return 1.5f * x - 0.5f * x * x * x;
    }

    inline float sineClip (float x) noexcept
    {
        return std::sin (clampf (x, -0.5f * kPi, 0.5f * kPi));
    }

    inline float triFold (float x) noexcept        // triangle wavefolder, slope 1 around 0
    {
        float t = x + 1.0f;
        t = t - 4.0f * std::floor (t * 0.25f);
        return t < 2.0f ? t - 1.0f : 3.0f - t;
    }

    inline float tubeCurve (float x, float s) noexcept
    {
        const float a = 0.3f + 0.7f * s;
        const float k = 1.0f - 0.45f * a;           // softer negative half -> even harmonics
        return x >= 0.0f ? std::tanh (x) : std::tanh (x * k) / k;
    }

    inline float ampStage (float x) noexcept
    {
        static const float off = std::tanh (0.25f);
        return cubicClip (3.2f * (std::tanh (1.7f * x + 0.25f) - off));
    }

    inline float deadZone (float x, float d) noexcept
    {
        const float m = std::abs (x) - d;
        return m <= 0.0f ? 0.0f : std::copysign (m / (1.0f - d), x);
    }

    inline float fuzzCurve (float x) noexcept
    {
        return x >= 0.0f ? 1.0f - std::exp (-2.4f * x)
                         : -1.15f * (1.0f - std::exp (1.7f * x));
    }
}

inline float staticCurve (Algo a, float x, float s) noexcept
{
    using namespace curves;
    switch (a)
    {
        case Algo::Silk:    return (1.0f - s) * sineClip (x) + s * std::tanh (1.25f * x) / std::tanh (1.25f);
        case Algo::Tube:    return tubeCurve (x, s);
        case Algo::Tape:    return 0.5f * std::tanh (x) + 0.5f * x / (1.0f + std::abs (x));
        case Algo::Detail:  return clampf (x, -1.0f, 1.0f);
        case Algo::Amp:     return ampStage (x);
        case Algo::Grind:   return clampf (deadZone (x, 0.3f * s), -1.0f, 1.0f);
        case Algo::Fuzz:    return fuzzCurve (x);
        case Algo::Fold:    return (1.0f - s) * std::sin (0.5f * kPi * x) + s * triFold (x);
        case Algo::Rectify: return std::tanh ((1.0f - s) * x + s * std::abs (x));
        case Algo::Crush:   return clampf (x, -1.0f, 1.0f);
        case Algo::Slew:    return std::tanh (x);
        // circuit models are stateful; this memoryless stand-in is only used as a fallback
        case Algo::Triode:
        case Algo::Stack:   return tubeCurve (x, 0.6f);
        case Algo::Diode:   return std::tanh (x);
        case Algo::Hysteresis:
        case Algo::Iron:    return std::tanh (x);
        case Algo::Count:   break;
    }
    return x;
}

// ---------------------------------------------------------------------------
// Antiderivatives for first-order ADAA (antiderivative anti-aliasing).
// F'(x) == staticCurve(x). Evaluated in double for numerical safety.
// ---------------------------------------------------------------------------
namespace anti
{
    // log(cosh(x)) = |x| - ln 2 + log1p(exp(-2|x|)). The last term is smooth and decays fast,
    // so it comes from a table with cubic Hermite interpolation (error < 1e-12, far cheaper than
    // exp + log1p). A series covers small |x|, where the subtraction would cancel.
    struct SoftplusTable
    {
        static constexpr int N = 8192;
        static constexpr double range = 20.0, h = range / N;
        std::array<double, N + 1> g {}, d {};
        SoftplusTable()
        {
            for (int i = 0; i <= N; ++i)
            {
                const double a = i * h;
                g[(size_t) i] = std::log1p (std::exp (-2.0 * a));
                d[(size_t) i] = -2.0 / (1.0 + std::exp (2.0 * a));
            }
        }
        static const SoftplusTable& get() { static const SoftplusTable t; return t; }
        double eval (double a) const noexcept
        {
            if (a >= range) return std::exp (-2.0 * a);           // below 1e-17 anyway
            const double pos = a / h;
            const int i = std::min (N - 1, (int) pos);
            const double t = pos - i, t2 = t * t, t3 = t2 * t;
            const double h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
            return h00 * g[(size_t) i] + h10 * h * d[(size_t) i] + h01 * g[(size_t) i + 1] + h11 * h * d[(size_t) i + 1];
        }
    };

    inline double logcosh (double x) noexcept
    {
        const double a = std::abs (x);
        if (a < 1.0e-2)
        {
            const double a2 = a * a;
            return a2 * (0.5 - a2 * (1.0 / 12.0 - a2 * (1.0 / 45.0)));
        }
        static const SoftplusTable& tab = SoftplusTable::get();
        return a - 0.6931471805599453 + tab.eval (a);
    }
    inline double clamp1 (double x) noexcept   { const double a = std::abs (x); return a <= 1.0 ? 0.5 * x * x : a - 0.5; }
    inline double cubic (double x) noexcept    { const double a = std::abs (x); return a <= 1.0 ? 0.75 * x * x - 0.125 * x * x * x * x : a - 0.375; }
    inline double sineClip (double x) noexcept
    {
        const double a = std::abs (x), h = 1.5707963267948966;
        return a <= h ? 1.0 - std::cos (x) : 1.0 + (a - h);
    }
    inline double triFold (double x) noexcept
    {
        double u = x + 1.0;
        u -= 4.0 * std::floor (u * 0.25);
        const double G = u < 2.0 ? 0.5 * u * u - u : 3.0 * (u - 2.0) - 0.5 * (u * u - 4.0);
        return G + 0.5;
    }
}

inline bool hasAntiderivative (Algo a) noexcept
{
    return a == Algo::Silk || a == Algo::Tube || a == Algo::Tape || a == Algo::Grind
        || a == Algo::Fuzz || a == Algo::Fold || a == Algo::Rectify;
}

inline double antiderivative (Algo a, double x, float sf) noexcept
{
    const double s = sf;
    switch (a)
    {
        case Algo::Silk:
        {
            static const double t = std::tanh (1.25);
            return (1.0 - s) * anti::sineClip (x) + s * anti::logcosh (1.25 * x) / (1.25 * t);
        }
        case Algo::Tube:
        {
            const double k = 1.0 - 0.45 * (0.3 + 0.7 * s);
            return x >= 0.0 ? anti::logcosh (x) : anti::logcosh (k * x) / (k * k);
        }
        case Algo::Tape:
        {
            const double ax = std::abs (x);
            return 0.5 * anti::logcosh (x) + 0.5 * (ax - std::log1p (ax));
        }
        case Algo::Grind:
        {
            const double d = 0.3 * s, ax = std::abs (x);
            if (ax <= d) return 0.0;
            if (ax <= 1.0) return (ax - d) * (ax - d) / (2.0 * (1.0 - d));
            return 0.5 * (1.0 - d) + (ax - 1.0);
        }
        case Algo::Fuzz:
            return x >= 0.0 ? x + (std::exp (-2.4 * x) - 1.0) / 2.4
                            : -1.15 * (x - (std::exp (1.7 * x) - 1.0) / 1.7);
        case Algo::Fold:
            return (1.0 - s) * (2.0 / 3.141592653589793) * (1.0 - std::cos (1.5707963267948966 * x))
                   + s * anti::triFold (x);
        case Algo::Rectify:
        {
            if (x >= 0.0) return anti::logcosh (x);
            const double c = 1.0 - 2.0 * s;          // slope of the negative half before tanh
            if (std::abs (c) < 1.0e-12) return 0.5 * c * x * x;   // tanh(cx) ~ cx
            return anti::logcosh (c * x) / c;         // accurate for small c thanks to the logcosh series
        }
        case Algo::Detail:
        case Algo::Amp:
        case Algo::Crush:
        case Algo::Slew:
        case Algo::Triode:
        case Algo::Diode:
        case Algo::Hysteresis:
        case Algo::Iron:
        case Algo::Stack:
        case Algo::Count:
            break;                                    // these use their own stages in the engine
    }
    return 0.5 * x * x;
}

// First-order ADAA state for one nonlinearity
struct Adaa
{
    double x1 = 0.0, F1 = 0.0;
    bool primed = false;
    void reset() noexcept { x1 = F1 = 0.0; primed = false; }
    void invalidate() noexcept { primed = false; }

    template <typename Fn, typename AntiFn>
    inline float process (double x, Fn&& f, AntiFn&& F) noexcept
    {
        if (! primed) { F1 = F (x1); primed = true; }
        const double Fx = F (x);
        const double d = x - x1;
        double y;
        if (std::abs (d) > 1.0e-5)
            y = (Fx - F1) / d;
        else
            y = (double) f ((float) (0.5 * (x + x1)));
        x1 = x; F1 = Fx;
        return (float) y;
    }
};

// Gain that brings a -12 dBFS sine back to its input RMS after the curve.
// Keeps "drive" a character control instead of a volume control.
inline float levelCompensation (Algo a, float driveGain, float s, float bias) noexcept
{
    if (a == Algo::Crush)
        return 1.0f;

    constexpr int N = 64;
    constexpr float ref = 0.25f;
    const float dc = staticCurve (a, bias, s);
    float sum = 0.0f, sum2 = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        const float x = ref * std::sin (2.0f * kPi * (float) i / (float) N);
        const float y = staticCurve (a, driveGain * x + bias, s) - dc;
        sum += y; sum2 += y * y;
    }
    const float mean = sum / N;
    const float rms  = std::sqrt (std::max (sum2 / N - mean * mean, 1.0e-12f));
    const float refRms = ref * 0.70710678f;
    return clampf (refRms / rms, 0.0005f, 4.0f);
}

// ---------------------------------------------------------------------------
// Harmonic designer (Chebyshev). Two flavours, blended by "Lock":
//  - analog: leading-term power series, harmonics grow with level (like hardware)
//  - locked: level-normalised Chebyshev, harmonic ratios stay constant at any level
// ---------------------------------------------------------------------------
struct HarmonicRecipe
{
    std::array<float, 7> h {};   // harmonics 2..8, bipolar -1..1
    float lock = 1.0f;
    bool isZero() const noexcept
    {
        for (auto v : h) if (! same (v, 0.0f)) return false;
        return true;
    }
};

// Odd harmonics 3 and 7 are phase-flipped so that a positive value is in phase with what
// ordinary (compressive) saturation produces. Negative values therefore cancel it.
inline constexpr float harmonicSign (int n) noexcept { return (n == 3 || n == 7) ? -1.0f : 1.0f; }

inline float chebyshevSum (const HarmonicRecipe& r, float x) noexcept   // x in [-1, 1]
{
    float tPrev = 1.0f, t = x, sum = 0.0f;
    for (int n = 2; n <= 8; ++n)
    {
        const float tNext = 2.0f * x * t - tPrev;
        tPrev = t; t = tNext;
        sum += harmonicSign (n) * r.h[(size_t) (n - 2)] * t;
    }
    return sum;
}

inline float powerSum (const HarmonicRecipe& r, float x) noexcept      // x in [-1, 1]
{
    float p = x, sum = 0.0f, scale = 1.0f;
    for (int n = 2; n <= 8; ++n)
    {
        p *= x; scale *= 2.0f;
        sum += harmonicSign (n) * r.h[(size_t) (n - 2)] * 0.5f * scale * p;     // 2^(n-1) x^n
    }
    return sum;
}

// Memoryless view of the designer for a signal with known peak envelope 'env'
inline float harmonicStatic (const HarmonicRecipe& r, float x, float env) noexcept
{
    const float xn = clampf (x / std::max (env, 1.0e-6f), -1.0f, 1.0f);
    const float locked = chebyshevSum (r, xn) * env;
    const float analog = powerSum (r, clampf (x * 1.5f, -1.0f, 1.0f)) / 1.5f;
    return r.lock * locked + (1.0f - r.lock) * analog;
}

} // namespace klinge
