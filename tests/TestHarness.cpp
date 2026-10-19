// KLINGE offline test harness
// Renders audio through the real processor and checks measurable behaviour.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <random>
#include <thread>
#include <functional>

using namespace klinge;

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

static constexpr double SR = 48000.0;

static std::unique_ptr<KlingeProcessor> makeProc (int block = 512, double sr = SR)
{
    auto p = std::make_unique<KlingeProcessor>();
    p->setPlayConfigDetails (2, 2, sr, block);
    p->prepareToPlay (sr, block);
    return p;
}

static void setP (KlingeProcessor& p, const juce::String& id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    jassert (prm != nullptr);
    if (prm) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static juce::AudioBuffer<float> render (KlingeProcessor& p, const juce::AudioBuffer<float>& in, int block = 512)
{
    juce::AudioBuffer<float> out (in);
    juce::MidiBuffer midi;
    for (int s = 0; s < out.getNumSamples(); s += block)
    {
        const int n = std::min (block, out.getNumSamples() - s);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, s, n);
        p.processBlock (view, midi);
    }
    return out;
}

// ---------------------------------------------------------------- signals
static juce::AudioBuffer<float> sine (double f, float amp, double secs)
{
    juce::AudioBuffer<float> b (2, (int) (secs * SR));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * i / SR);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}

static juce::AudioBuffer<float> pinkNoise (float rmsDb, double secs, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    juce::AudioBuffer<float> b (2, (int) (secs * SR));
    for (int c = 0; c < 2; ++c)
    {
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float w = nd (rng);
            b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
            b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
            b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
            b.setSample (c, i, b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f);
            b6 = w * 0.115926f;
        }
    }
    const float rms = std::sqrt ((b.getRMSLevel (0, 0, b.getNumSamples()) * b.getRMSLevel (0, 0, b.getNumSamples())
                                  + b.getRMSLevel (1, 0, b.getNumSamples()) * b.getRMSLevel (1, 0, b.getNumSamples())) * 0.5f);
    b.applyGain (juce::Decibels::decibelsToGain (rmsDb) / rms);
    return b;
}

// Percussive loop: 16th-note noise hits with fast decays + a sine kick on quarters
static juce::AudioBuffer<float> drumLoop (double secs)
{
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> ud (-1.0f, 1.0f);
    juce::AudioBuffer<float> b (2, (int) (secs * SR));
    b.clear();
    const int step = (int) (SR * 60.0 / 150.0 / 4.0);   // 150 BPM 16ths
    for (int s = 0, k = 0; s < b.getNumSamples(); s += step, ++k)
    {
        const bool quarter = k % 4 == 0;
        for (int i = 0; i < step && s + i < b.getNumSamples(); ++i)
        {
            const float t = (float) i / (float) SR;
            float v = 0.18f * ud (rng) * std::exp (-t * (k % 2 ? 60.0f : 25.0f));
            if (quarter) v += 0.45f * std::sin (2.0f * juce::MathConstants<float>::pi * (55.0f + 120.0f * std::exp (-t * 30.0f)) * t) * std::exp (-t * 9.0f);
            b.setSample (0, s + i, v);
            b.setSample (1, s + i, v * 0.9f + 0.02f * ud (rng) * std::exp (-t * 40.0f));
        }
    }
    return b;
}

// ---------------------------------------------------------------- analysis
static float rmsDb (const juce::AudioBuffer<float>& b, int start = 0, int len = -1)
{
    if (len < 0) len = b.getNumSamples() - start;
    double s = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = start; i < start + len; ++i) s += (double) b.getSample (c, i) * b.getSample (c, i);
    return (float) (10.0 * std::log10 (s / (len * b.getNumChannels()) + 1e-20));
}

static bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

struct Spectrum { std::vector<float> mag; double binHz; };

static Spectrum spectrum (const juce::AudioBuffer<float>& b, int start, int order = 15)
{
    const int N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data ((size_t) N * 2, 0.0f);
    for (int i = 0; i < N; ++i)
    {
        const double w = 0.35875 - 0.48829 * std::cos (2 * juce::MathConstants<double>::pi * i / (N - 1))
                         + 0.14128 * std::cos (4 * juce::MathConstants<double>::pi * i / (N - 1))
                         - 0.01168 * std::cos (6 * juce::MathConstants<double>::pi * i / (N - 1));
        data[(size_t) i] = (float) w * b.getSample (0, start + i);
    }
    fft.performFrequencyOnlyForwardTransform (data.data());
    Spectrum s;
    s.mag.assign (data.begin(), data.begin() + N / 2);
    s.binHz = SR / N;
    return s;
}

static float peakNear (const Spectrum& s, double hz, int width = 4)
{
    const int c = (int) std::round (hz / s.binHz);
    float m = 0;
    for (int k = std::max (1, c - width); k <= std::min ((int) s.mag.size() - 1, c + width); ++k) m = std::max (m, s.mag[(size_t) k]);
    return m;
}

struct HarmReport { float thd; float h[6]; float inharmonicDb; };

static HarmReport harmonics (const juce::AudioBuffer<float>& out, double f0)
{
    auto s = spectrum (out, out.getNumSamples() - (1 << 15) - 10);
    HarmReport r {};
    const float fund = peakNear (s, f0);
    double sum = 0;
    for (int k = 2; k <= 40 && k * f0 < SR / 2; ++k)
    {
        const float m = peakNear (s, k * f0);
        sum += (double) m * m;
        if (k <= 7) r.h[k - 2] = juce::Decibels::gainToDecibels (m / fund, -150.0f);
    }
    r.thd = (float) (100.0 * std::sqrt (sum) / fund);
    // energy away from harmonics (aliasing + noise)
    double in = 0;
    const int guard = 8;
    for (size_t k = 20; k < s.mag.size(); ++k)
    {
        const double hz = (double) k * s.binHz;
        const double nearest = std::round (hz / f0) * f0;
        if (std::abs (hz - nearest) > guard * s.binHz) in += (double) s.mag[k] * s.mag[k];
    }
    r.inharmonicDb = (float) (10.0 * std::log10 (in / ((double) fund * fund) + 1e-30));
    return r;
}

// short-time RMS envelope in dB (5 ms frames)
static std::vector<float> envelopeDb (const juce::AudioBuffer<float>& b, int offset)
{
    const int frame = (int) (0.005 * SR);
    std::vector<float> e;
    for (int s = offset; s + frame < b.getNumSamples(); s += frame)
    {
        double acc = 0;
        for (int i = 0; i < frame; ++i) acc += (double) b.getSample (0, s + i) * b.getSample (0, s + i);
        e.push_back ((float) (10.0 * std::log10 (acc / frame + 1e-12)));
    }
    return e;
}

static float correlation (const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = std::min (a.size(), b.size());
    double ma = 0, mb = 0;
    for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < n; ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
    return (float) (sab / std::sqrt (saa * sbb + 1e-20));
}

static float stdDev (const std::vector<float>& a)
{
    double m = 0; for (auto v : a) m += v; m /= a.size();
    double s = 0; for (auto v : a) s += (v - m) * (v - m);
    return (float) std::sqrt (s / a.size());
}

// band energies (dB) below 250 Hz, 250-2500, above 2500
static std::array<float, 3> bandEnergies (const juce::AudioBuffer<float>& b)
{
    auto s = spectrum (b, b.getNumSamples() - (1 << 15) - 10);
    double e[3] = { 0, 0, 0 };
    for (size_t k = 1; k < s.mag.size(); ++k)
    {
        const double hz = k * s.binHz;
        const int band = hz < 250 ? 0 : (hz < 2500 ? 1 : 2);
        e[band] += (double) s.mag[k] * s.mag[k];
    }
    return { (float) (10 * std::log10 (e[0])), (float) (10 * std::log10 (e[1])), (float) (10 * std::log10 (e[2])) };
}

// ================================================================= tests
static void testAntiderivatives()
{
    std::printf ("\n== ADAA antiderivatives match their curves ==\n");
    for (int a = 0; a < numAlgos; ++a)
    {
        const auto algo = (Algo) a;
        if (! hasAntiderivative (algo)) continue;
        double worst = 0;
        for (float s : { 0.0f, 0.37f, 1.0f })
            for (double x = -6.0; x <= 6.0; x += 0.0137)
            {
                const double h = 1.0e-5;
                const double d = (antiderivative (algo, x + h, s) - antiderivative (algo, x - h, s)) / (2 * h);
                worst = std::max (worst, std::abs (d - staticCurve (algo, (float) x, s)));
            }
        check (worst < 2.0e-3, algoNames()[a] + ": F'(x) matches the curve (max error " + juce::String (worst, 6) + ")");
    }
}

static void testAlgorithms()
{
    testAntiderivatives();
    std::printf ("\n== Algorithms: 100 Hz sine at -12 dBFS, drive 24 dB, auto gain on ==\n");
    std::printf ("  %-8s %8s %8s %7s %7s %7s %7s %10s\n", "type", "out dB", "THD %", "H2", "H3", "H4", "H5", "inharm dB");
    const auto in = sine (100.0, 0.25f, 2.0);
    const float inDb = rmsDb (in);
    for (int a = 0; a < numAlgos; ++a)
    {
        auto p = makeProc();
        setP (*p, "b1_type", (float) a);
        setP (*p, "drive", 24);
        auto out = render (*p, in);
        auto h = harmonics (out, 100.0);
        const float o = rmsDb (out, out.getNumSamples() / 2) ;
        std::printf ("  %-8s %8.2f %8.2f %7.1f %7.1f %7.1f %7.1f %10.1f\n", algoNames()[a].toRawUTF8(), o - inDb, h.thd,
                     h.h[0], h.h[1], h.h[2], h.h[3], h.inharmonicDb);
        check (allFinite (out), algoNames()[a] + ": output finite");
        check (std::abs (o - inDb) < 3.0f, algoNames()[a] + ": auto gain keeps level within 3 dB (" + juce::String (o - inDb, 2) + " dB)");
        check (h.thd > 0.5f || h.inharmonicDb > -40.0f, algoNames()[a] + ": adds distortion (harmonic, or aliasing for Crush)");
    }
}

static void testAliasing()
{
    struct Case { const char* name; Algo type; float drive; };
    const Case cases[] = { { "Tube, drive 24 dB", Algo::Tube, 24 }, { "Amp, drive 40 dB", Algo::Amp, 40 } };
    for (auto& cs : cases)
    {
        std::printf ("\n== Aliasing: 5.5 kHz sine at -6 dBFS, %s ==\n", cs.name);
        const auto in = sine (5500.0, 0.5f, 2.0);
        float prev = 1000, first = 0;
        for (int os = 0; os <= 4; ++os)
        {
            auto p = makeProc();
            setP (*p, "b1_type", (float) cs.type);
            setP (*p, "drive", cs.drive);
            setP (*p, "oversampling", (float) os);
            auto out = render (*p, in);
            auto h = harmonics (out, 5500.0);
            std::printf ("  oversampling %-4s  inharmonic (alias) energy re. fundamental: %7.1f dB\n",
                         oversamplingNames()[os].toRawUTF8(), h.inharmonicDb);
            if (os == 0) first = h.inharmonicDb;
            if (os > 0) check (h.inharmonicDb < prev + 1.0f, "more oversampling does not increase aliasing");
            prev = h.inharmonicDb;
        }
        check (prev < first - 15.0f, "16x removes at least 15 dB of aliasing compared with no oversampling");
    }
}

// Welch-averaged magnitude ratio (out / in) in 1/6-octave bands, returns max |dev| in dB
static float maxResponseDeviation (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int offset,
                                   double sr = SR)
{
    const int order = sr > 96000.0 ? 15 : 13, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<double> pin ((size_t) N / 2, 0.0), pout ((size_t) N / 2, 0.0);
    std::vector<float> a ((size_t) N * 2), b ((size_t) N * 2);
    for (int seg = 0; seg < 12; ++seg)
    {
        const int st = (int) sr + seg * N / 2;
        if (st + N + offset >= out.getNumSamples()) break;
        for (int i = 0; i < N; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (N - 1));
            a[(size_t) i] = w * in.getSample (0, st + i);
            b[(size_t) i] = w * out.getSample (0, st + offset + i);
        }
        std::fill (a.begin() + N, a.end(), 0.0f); std::fill (b.begin() + N, b.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (a.data());
        fft.performFrequencyOnlyForwardTransform (b.data());
        for (int k = 0; k < N / 2; ++k) { pin[(size_t) k] += (double) a[(size_t) k] * a[(size_t) k]; pout[(size_t) k] += (double) b[(size_t) k] * b[(size_t) k]; }
    }
    float worst = 0;
    for (double f = 40.0; f < 16000.0; f *= std::pow (2.0, 1.0 / 6.0))
    {
        const int k0 = (int) (f / (sr / N)), k1 = (int) (f * std::pow (2.0, 1.0 / 6.0) / (sr / N));
        double si = 0, so = 0;
        for (int k = k0; k <= std::max (k0, k1); ++k) { si += pin[(size_t) k]; so += pout[(size_t) k]; }
        worst = std::max (worst, (float) std::abs (10.0 * std::log10 (so / si)));
    }
    return worst;
}

static void testParallelAlignment()
{
    std::printf ("\n== Parallel blending: clean bands, mix 50 %%. Comb filtering would show as response deviation ==\n");
    const auto in = pinkNoise (-18.0f, 4.0);
    for (int routing = 0; routing < 3; ++routing)
        for (int q = 0; q < 2; ++q)
            for (int os : { 0, 1, 2, 3, 4 })
            {
                auto p = makeProc();
                setP (*p, "routing", (float) routing);
                setP (*p, "oversampling", (float) os);
                setP (*p, "os_quality", (float) q);
                setP (*p, "mix", 50);
                for (int b = 1; b <= 3; ++b) setP (*p, "b" + juce::String (b) + "_mix", 0);
                auto out = render (*p, in);
                const int L = p->expectedLatency();
                const float dev = maxResponseDeviation (in, out, L);
                float nullDb = 0;
                if (routing != 1)
                {
                    double e = 0, r = 0;
                    for (int c = 0; c < 2; ++c)
                        for (int i = 48000; i < in.getNumSamples() - L; ++i)
                        {
                            const double d = out.getSample (c, i + L) - in.getSample (c, i);
                            e += d * d; r += (double) in.getSample (c, i) * in.getSample (c, i);
                        }
                    nullDb = (float) (10.0 * std::log10 (e / r + 1e-30));
                }
                const bool linear = q == 0;
                std::printf ("  %-10s %-13s OS %-4s latency %3d   max response deviation %5.2f dB   %s\n",
                             routingNames()[routing].toRawUTF8(), linear ? "linear phase" : "low latency",
                             oversamplingNames()[os].toRawUTF8(), L, dev,
                             routing != 1 ? ("output vs input " + juce::String (nullDb, 1) + " dB").toRawUTF8() : "(allpass crossover)");
                if (linear)
                    check (dev < 0.3f, routingNames()[routing] + " / " + oversamplingNames()[os] + ": parallel mix has no comb filtering");
                else
                    check (dev < 3.0f, routingNames()[routing] + " / " + oversamplingNames()[os] + " low latency: deviation stays moderate");
            }
}

static void testPunch()
{
    std::printf ("\n== Punch: drum loop through Amp at 36 dB drive ==\n");
    const auto in = drumLoop (6.0);
    const int L0 = 48000;
    const auto envIn = envelopeDb (in, L0);
    float corr[3] {}, dev[3] {};
    const float amounts[] = { 0.0f, 100.0f, 150.0f };
    for (int i = 0; i < 3; ++i)
    {
        auto p = makeProc();
        setP (*p, "b1_type", (float) Algo::Amp);
        setP (*p, "drive", 36);
        setP (*p, "punch", amounts[i]);
        setP (*p, "punch_speed", 20);
        auto out = render (*p, in);
        auto env = envelopeDb (out, L0 + p->expectedLatency());
        corr[i] = correlation (envIn, env);
        dev[i] = stdDev (env);
        std::printf ("  punch %5.0f %%   envelope correlation with dry %.3f   envelope spread %.1f dB (dry %.1f dB)\n",
                     amounts[i], corr[i], dev[i], stdDev (envIn));
        check (allFinite (out), "output finite");
    }
    check (corr[1] > corr[0] + 0.02f, "Punch 100 % brings the distorted envelope closer to the dry one");
    check (dev[1] > dev[0] + 1.0f, "Punch 100 % restores dynamic range (micro-dynamics)");
    check (dev[2] > dev[1], "Punch 150 % exaggerates beyond the dry dynamics");
}

static void testFaithful()
{
    std::printf ("\n== Faithful (tone lock): pink noise through Fuzz at 36 dB ==\n");
    const auto in = pinkNoise (-18.0f, 5.0);
    const auto ein = bandEnergies (in);
    float err[2] {};
    for (int i = 0; i < 2; ++i)
    {
        auto p = makeProc();
        setP (*p, "b1_type", (float) Algo::Fuzz);
        setP (*p, "b1_shape", 0);
        setP (*p, "drive", 36);
        setP (*p, "tone_lock", i == 0 ? 0.0f : 100.0f);
        auto out = render (*p, in);
        const auto e = bandEnergies (out);
        const float dl = (e[0] - e[1]) - (ein[0] - ein[1]);
        const float dh = (e[2] - e[1]) - (ein[2] - ein[1]);
        err[i] = std::abs (dl) + std::abs (dh);
        std::printf ("  faithful %3d %%   low/mid balance shift %+6.1f dB   high/mid shift %+6.1f dB\n", i * 100, dl, dh);
    }
    check (err[1] < err[0] * 0.5f, "Faithful 100 % at least halves the tonal balance shift");
}

static void testAutoGain()
{
    std::printf ("\n== Auto gain across drive (Tube, pink noise -18 dB RMS) ==\n");
    const auto in = pinkNoise (-18.0f, 4.0);
    const float inDb = rmsDb (in, 96000);
    float worst = 0;
    for (float d : { 0.0f, 12.0f, 24.0f, 36.0f, 48.0f })
    {
        auto p = makeProc();
        setP (*p, "b1_type", (float) Algo::Tube);
        setP (*p, "drive", d);
        auto out = render (*p, in);
        const float diff = rmsDb (out, 96000) - inDb;
        worst = std::max (worst, std::abs (diff));
        std::printf ("  drive %4.0f dB   output %+5.2f dB re. input\n", d, diff);
    }
    check (worst < 1.5f, "loudness stays within 1.5 dB of the input at every drive");
}

static void testHarmonicDesigner()
{
    std::printf ("\n== Harmonic designer: Silk at 0 dB drive, h2 = 50 %% ==\n");
    for (float lock : { 100.0f, 0.0f })
    {
        float ratio[2] {};
        int idx = 0;
        for (float amp : { 0.5f, 0.0316f })   // -6 and -30 dBFS
        {
            auto p = makeProc();
            setP (*p, "b1_type", (float) Algo::Silk);
            setP (*p, "drive", 0);
            setP (*p, "h2", 50);
            setP (*p, "harm_lock", lock);
            auto out = render (*p, sine (200.0, amp, 2.0));
            auto h = harmonics (out, 200.0);
            ratio[idx++] = h.h[0];
            std::printf ("  lock %3.0f %%  input %5.1f dBFS   2nd harmonic %6.1f dB re. fundamental\n",
                         lock, juce::Decibels::gainToDecibels (amp), h.h[0]);
        }
        if (lock > 50) check (std::abs (ratio[0] - ratio[1]) < 1.5f && std::abs (ratio[0] + 6.0f) < 2.0f,
                              "locked: 2nd harmonic sits near -6 dB at any level");
        else check (ratio[1] < ratio[0] - 15.0f, "analog: harmonics fall away at low level");
    }

    std::printf ("\n== Harmonic designer: negative h3 cancels the 3rd harmonic of a clipper ==\n");
    float h3[2] {};
    for (int i = 0; i < 2; ++i)
    {
        auto p = makeProc();
        setP (*p, "b1_type", (float) Algo::Silk);
        setP (*p, "b1_shape", 0);
        setP (*p, "drive", 9);
        setP (*p, "h3", i == 0 ? 0.0f : -6.0f);
        auto out = render (*p, sine (200.0, 0.5f, 2.0));
        h3[i] = harmonics (out, 200.0).h[1];
        std::printf ("  h3 %+5.0f %%   3rd harmonic %6.1f dB\n", i == 0 ? 0.0f : -6.0f, h3[i]);
    }
    check (h3[1] < h3[0] - 6.0f, "pulling a harmonic below zero subtracts it");
}

static void testDetail()
{
    std::printf ("\n== Detail clipper: 60 Hz at 0 dBFS + quiet 3 kHz tone at -30 dBFS, drive 18 ==\n");
    juce::AudioBuffer<float> in (2, (int) (2.0 * SR));
    for (int i = 0; i < in.getNumSamples(); ++i)
    {
        const float v = 0.9f * (float) std::sin (2 * juce::MathConstants<double>::pi * 60 * i / SR)
                        + 0.0316f * (float) std::sin (2 * juce::MathConstants<double>::pi * 3000 * i / SR);
        in.setSample (0, i, v); in.setSample (1, i, v);
    }
    const float inTone = peakNear (spectrum (in, in.getNumSamples() - (1 << 15) - 10), 3000.0);
    float kept[2] {};
    for (int i = 0; i < 2; ++i)
    {
        auto p = makeProc();
        setP (*p, "b1_type", (float) Algo::Detail);
        setP (*p, "b1_shape", i == 0 ? 0.0f : 100.0f);
        setP (*p, "drive", 18);
        setP (*p, "auto_gain", 0);
        auto out = render (*p, in);
        auto s = spectrum (out, out.getNumSamples() - (1 << 15) - 10);
        kept[i] = juce::Decibels::gainToDecibels (peakNear (s, 3000.0) / inTone) - juce::Decibels::gainToDecibels (peakNear (s, 60.0) / 0.9f);
        std::printf ("  detail %3d %%   quiet tone relative to the loud one: %+6.1f dB (0 = fully kept)\n", i * 100, kept[i]);
    }
    check (kept[1] > kept[0] + 6.0f, "Detail keeps the quiet tone that plain clipping masks");
}

static void testFuzzing()
{
    std::printf ("\n== Random parameters and block sizes ==\n");
    std::mt19937 rng (42);
    auto p = makeProc (256);
    auto params = p->getParameters();
    const auto in = drumLoop (1.0);
    float worst = 0;
    bool finite = true;
    for (int round = 0; round < 150; ++round)
    {
        for (auto* prm : params)
            prm->setValueNotifyingHost (std::uniform_real_distribution<float> (0, 1) (rng));
        setP (*p, "oversampling", (float) (rng() % 4));
        setP (*p, "in_gain", 0);   // level gains are tested separately; here we look for instability
        setP (*p, "out_gain", 0);
        juce::AudioBuffer<float> buf (2, 4096);
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, in, c, (round * 4096) % (in.getNumSamples() - 4096), 4096);
        juce::MidiBuffer midi;
        int pos = 0;
        while (pos < 4096)
        {
            const int n = std::min (4096 - pos, (int) (1 + rng() % 1500));
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, n);
            p->processBlock (view, midi);
            pos += n;
        }
        finite = finite && allFinite (buf);
        const float pk = buf.getMagnitude (0, 4096);
        worst = std::max (worst, pk);
        if (pk > 4.0f)
        {
            juce::String info;
            for (auto id : { "routing", "b1_type", "delta", "auto_gain", "punch", "mix", "b1_level", "b1_harm", "h2", "h3", "harm_lock", "dyn_drive", "b1_mix", "tone_lock" })
                info << id << "=" << p->apvts.getParameter (id)->getCurrentValueAsText() << " ";
            std::printf ("    round %d peak %+.1f dBFS: %s\n", round, juce::Decibels::gainToDecibels (pk), info.toRawUTF8());
            // steady-state reproduction with a fresh instance
            auto fresh = makeProc();
            auto fp = fresh->getParameters();
            for (int k = 0; k < params.size(); ++k) fp[k]->setValueNotifyingHost (params[k]->getValue());
            auto o2 = render (*fresh, in);
            std::printf ("      fresh instance, same settings: peak %+.1f dBFS (rms %.1f)\n",
                         juce::Decibels::gainToDecibels (o2.getMagnitude (0, o2.getNumSamples())), rmsDb (o2));
            for (auto* prm : params)
                std::printf ("%s=%s; ", prm->getName (20).toRawUTF8(), prm->getCurrentValueAsText().toRawUTF8());
            std::printf ("\n");
        }
    }
    std::printf ("  150 random settings, block sizes 1..1500, max output %.1f dBFS\n", juce::Decibels::gainToDecibels (worst));
    check (finite, "no NaN or infinity under random settings");
    check (worst < 8.0f, "output stays bounded (< +18 dBFS at unity in/out gain) under random settings");

    // silence in -> silence out (no self-oscillation or DC)
    auto q = makeProc();
    setP (*q, "b1_type", (float) Algo::Tube);
    setP (*q, "drive", 48);
    setP (*q, "b1_bias", 100);
    juce::AudioBuffer<float> sil (2, 96000);
    sil.clear();
    auto out = render (*q, sil);
    check (out.getMagnitude (48000, 48000) < 1.0e-4f, "silence in gives silence out, even with full bias");
}

static void testCpu()
{
    std::printf ("\n== CPU (real-time factor, stereo 48 kHz, 512 blocks) ==\n");
    const auto in = pinkNoise (-18.0f, 10.0);
    struct Cfg { const char* name; int routing; int os; int type; };
    const Cfg cfgs[] = { { "Single Tube 2x", 0, 1, 1 }, { "Single Amp 4x", 0, 2, 4 }, { "Multiband 4x", 1, 2, 4 },
                         { "Multiband 8x + harmonics", 1, 3, 4 }, { "Multiband 16x", 1, 4, 4 } };
    for (auto& c : cfgs)
    {
        auto p = makeProc();
        setP (*p, "routing", (float) c.routing);
        setP (*p, "oversampling", (float) c.os);
        for (int b = 1; b <= 3; ++b) setP (*p, "b" + juce::String (b) + "_type", (float) c.type);
        setP (*p, "punch", 80); setP (*p, "tone_lock", 50); setP (*p, "dyn_drive", 30);
        if (juce::String (c.name).contains ("harmonics")) { setP (*p, "h2", 30); setP (*p, "h3", 20); }
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto out = render (*p, in);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("  %-26s %6.1f%% of one core\n", c.name, 100.0 * ms / 10000.0);
    }
}

static void testState()
{
    std::printf ("\n== State save / restore ==\n");
    std::mt19937 rng (3);
    auto a = makeProc();
    for (auto* prm : a->getParameters()) prm->setValueNotifyingHost (std::uniform_real_distribution<float> (0, 1) (rng));
    juce::MemoryBlock mb;
    a->getStateInformation (mb);
    auto b = makeProc();
    b->setStateInformation (mb.getData(), (int) mb.getSize());
    int mismatches = 0;
    auto pa = a->getParameters(), pb = b->getParameters();
    for (int i = 0; i < pa.size(); ++i)
        if (pa[i]->getCurrentValueAsText() != pb[i]->getCurrentValueAsText())
        {
            ++mismatches;
            std::printf ("    mismatch %s: %s vs %s\n", pa[i]->getName (64).toRawUTF8(),
                         pa[i]->getCurrentValueAsText().toRawUTF8(), pb[i]->getCurrentValueAsText().toRawUTF8());
        }
    check (mismatches == 0, "all " + juce::String (pa.size()) + " parameters survive a save/restore round trip");
}

static void testPresets()
{
    std::printf ("\n== Factory presets on a drum loop ==\n");
    const auto in = drumLoop (3.0);
    const float inDb = rmsDb (in);
    for (int i = 0; i < (int) factoryPresets().size(); ++i)
    {
        auto p = makeProc();
        p->setCurrentProgram (i);
        auto out = render (*p, in);
        const bool ok = allFinite (out) && out.getMagnitude (0, out.getNumSamples()) < 4.0f;
        std::printf ("  %-22s out %+6.1f dB re. input   peak %+5.1f dBFS\n", factoryPresets()[(size_t) i].name,
                     rmsDb (out) - inDb, juce::Decibels::gainToDecibels (out.getMagnitude (0, out.getNumSamples())));
        check (ok, juce::String (factoryPresets()[(size_t) i].name) + ": finite, peak under +12 dBFS");
    }
}

static void testSampleRates()
{
    std::printf ("\n== Sample rates ==\n");
    for (double sr : { 44100.0, 96000.0, 192000.0 })
    {
        auto p = makeProc (1024, sr);
        setP (*p, "b1_type", (float) Algo::Amp);
        setP (*p, "drive", 30);
        setP (*p, "routing", 1);
        juce::AudioBuffer<float> b (2, (int) sr);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * 220 * i / sr);
            b.setSample (0, i, v); b.setSample (1, i, v);
        }
        juce::MidiBuffer midi;
        for (int s = 0; s < b.getNumSamples(); s += 1024)
        {
            juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), 2, s, std::min (1024, b.getNumSamples() - s));
            p->processBlock (view, midi);
        }
        check (allFinite (b) && b.getMagnitude (0, b.getNumSamples()) > 0.01f, "runs at " + juce::String (sr / 1000.0, 1) + " kHz");
    }
}


// ---------------------------------------------------------------- review: click freedom
// Largest second difference |y[n] - 2y[n-1] + y[n-2]|: a click shows up as a spike far above
// the steady-state curvature of the signal.
static float maxCurvature (const juce::AudioBuffer<float>& b, int from, int to)
{
    float m = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = std::max (2, from); i < std::min (to, b.getNumSamples()); ++i)
            m = std::max (m, std::abs (b.getSample (c, i) - 2.0f * b.getSample (c, i - 1) + b.getSample (c, i - 2)));
    return m;
}

static void testClicks()
{
    std::printf ("\n== Click freedom: switch a setting mid-stream, compare the transition with steady state ==\n");
    struct Case { const char* name; std::function<void (KlingeProcessor&)> before, after; bool bypassAfter; };
    auto P = [] (const char* id, float v) { return [id, v] (KlingeProcessor& p) { setP (p, id, v); }; };
    auto none = [] (KlingeProcessor&) {};
    const Case cases[] = {
        { "band on -> off",          none, P ("b1_on", 0), false },
        { "band mute",               none, P ("b1_mute", 1), false },
        { "delta on",                none, P ("delta", 1), false },
        { "type Silk -> Fuzz",       none, P ("b1_type", (float) Algo::Fuzz), false },
        { "routing -> Multiband",    none, P ("routing", 1), false },
        { "routing -> Mid/Side",     none, P ("routing", 2), false },
        { "oversampling 2x -> 8x",   P ("oversampling", 1), P ("oversampling", 3), false },
        { "OS filter -> low latency",none, P ("os_quality", 1), false },
        { "host bypass on",          none, none, true },
        { "post high cut 20k -> 2k", none, P ("post_lp", 2000), false },
        { "pre low cut off -> 400",  none, P ("pre_hp", 400), false },
        { "focus 0 -> +12 dB",       none, P ("focus", 12), false },
        { "bias 0 -> +80 %",         none, P ("b1_bias", 80), false },
        { "shape 0 -> 100 %",        P ("b1_shape", 0), P ("b1_shape", 100), false },
        { "crossover 200 -> 1.2k",   P ("routing", 1), P ("xover_lo", 1200), false },
        { "output clip on (+6 dB)",  P ("out_gain", 6), P ("clip_out", 1), false },
        { "drive 12 -> 40 dB",       none, P ("drive", 40), false },
        { "input gain +12 dB",       none, P ("in_gain", 12), false },
        { "output gain -12 dB",      none, P ("out_gain", -12), false },
        { "mix 100 -> 0 %",          none, P ("mix", 0), false },
        { "band mix 100 -> 0 %",     none, P ("b1_mix", 0), false },
        { "band level -12 dB",       none, P ("b1_level", -12), false },
        { "harmonic 2 0 -> +100 %",  none, P ("h2", 100), false },
        { "harmonic 3 -> -100 %",    P ("h3", 50), P ("h3", -100), false },
        { "harmonic lock 100 -> 0",  P ("h2", 60), P ("harm_lock", 0), false },
        { "punch 0 -> 150 %",        none, P ("punch", 150), false },
        { "faithful 0 -> 100 %",     none, P ("tone_lock", 100), false },
        { "dynamics 0 -> +100 %",    none, P ("dyn_drive", 100), false },
        { "dynamics 0 -> -100 %",    none, P ("dyn_drive", -100), false },
        { "auto gain off",           none, P ("auto_gain", 0), false },
        { "routing Multi -> Single", P ("routing", 1), P ("routing", 0), false },
        { "solo band 2 (multiband)", P ("routing", 1), P ("b2_solo", 1), false },
        { "type Amp -> Crush",       P ("b1_type", (float) Algo::Amp), P ("b1_type", (float) Algo::Crush), false },
        { "type Tube -> Fold",       P ("b1_type", (float) Algo::Tube), P ("b1_type", (float) Algo::Fold), false },
        { "punch speed 5 -> 200 ms", P ("punch", 100), P ("punch_speed", 200), false },
        { "preset load",             none, [] (KlingeProcessor& p) { p.setCurrentProgram (2); }, false },
    };

    // 157 Hz + 2411 Hz (non-commensurate, so the switch never lands on a convenient zero crossing).
    // The switch point is a block boundary where the signal is near its peak.
    const int total = (int) (1.5 * SR), sw = 141 * 256, block = 256;
    juce::AudioBuffer<float> in (2, total);
    for (int i = 0; i < total; ++i)
    {
        const double t = i / SR;
        const float v = 0.22f * (float) std::sin (2 * juce::MathConstants<double>::pi * 157 * t)
                      + 0.05f * (float) std::sin (2 * juce::MathConstants<double>::pi * 2411 * t);
        in.setSample (0, i, v); in.setSample (1, i, v);
    }
    std::printf ("  signal value at the switch point: %+.3f\n", in.getSample (0, sw));

    // negative control: the metric must flag a hard switch
    {
        juce::AudioBuffer<float> hard (in);
        for (int c = 0; c < 2; ++c) hard.applyGain (c, sw, total - sw, 0.5f);
        const float r = maxCurvature (hard, sw - 64, sw + 19200)
                        / std::max (maxCurvature (hard, sw - 9600, sw - 64), maxCurvature (hard, sw + 19200, total));
        std::printf ("  %-26s transition / steady curvature %5.2f\n", "control: hard -6 dB step", r);
        check (r > 2.0f, "the click metric detects a hard step");
    }

    for (auto& cs : cases)
    {
        auto p = makeProc (block);
        setP (*p, "b1_type", (float) Algo::Silk);
        setP (*p, "drive", 12);
        cs.before (*p);
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        bool bypass = false;
        for (int s = 0; s < total; s += block)
        {
            if (s == sw) { cs.after (*p); bypass = cs.bypassAfter; }
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, s, std::min (block, total - s));
            if (bypass) p->processBlockBypassed (view, midi); else p->processBlock (view, midi);
        }
        const float before = maxCurvature (out, sw - 9600, sw - 64);
        const float after  = maxCurvature (out, sw + 19200, total);
        const float trans  = maxCurvature (out, sw - 64, sw + 19200);
        const float ratio  = trans / std::max (std::max (before, after), 1.0e-6f);
        std::printf ("  %-26s transition / steady curvature %5.2f\n", cs.name, ratio);
        if (ratio >= 2.0f)
        {
            // locate the worst spike for diagnosis
            float m = 0; int at = 0;
            for (int c = 0; c < 2; ++c)
                for (int i = std::max (2, sw - 64); i < sw + 19200; ++i)
                {
                    const float v = std::abs (out.getSample (c, i) - 2.0f * out.getSample (c, i - 1) + out.getSample (c, i - 2));
                    if (v > m) { m = v; at = i; }
                }
            std::printf ("      worst spike %.4f at %d samples after the switch\n", m, at - sw);
        }
        check (allFinite (out) && ratio < 2.0f, juce::String (cs.name) + ": no click");
    }
}

// ---------------------------------------------------------------- review: precision at extreme rates
static void testHighRatePrecision()
{
    std::printf ("\n== Precision at 192 kHz with 16x oversampling (3 MHz internal rate) ==\n");
    const double sr = 192000.0;
    auto makeNoise = [sr] (double secs)
    {
        std::mt19937 rng (5);
        std::normal_distribution<float> nd (0.0f, 0.1f);
        juce::AudioBuffer<float> b (2, (int) (secs * sr));
        for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, nd (rng));
        return b;
    };
    {
        const auto in = makeNoise (3.0);
        auto p = makeProc (1024, sr);
        setP (*p, "routing", 1);
        setP (*p, "oversampling", 4);
        setP (*p, "xover_lo", 40);
        setP (*p, "xover_hi", 12000);
        setP (*p, "mix", 50);
        for (int b = 1; b <= 3; ++b) setP (*p, "b" + juce::String (b) + "_mix", 0);
        auto out = render (*p, in, 1024);
        const float dev = maxResponseDeviation (in, out, p->expectedLatency(), sr);
        std::printf ("  multiband, crossover at 40 Hz, parallel response deviation %.3f dB\n", dev);
        check (dev < 0.3f, "crossover and allpass compensation stay exact at a 3 MHz internal rate");
    }
    {
        // Slow envelope followers must not stall at very high rates: on a steady tone the
        // dynamics ratio is 1, so Dynamics +100 % must sound like Dynamics 0 %.
        float lev[2] {};
        for (int i = 0; i < 2; ++i)
        {
            auto p = makeProc (1024, sr);
            setP (*p, "oversampling", 4);
            setP (*p, "b1_type", (float) Algo::Tube);
            setP (*p, "drive", 24);
            setP (*p, "auto_gain", 0);
            setP (*p, "dyn_drive", i == 0 ? 0.0f : 100.0f);
            juce::AudioBuffer<float> b (2, (int) (3.0 * sr));
            for (int k = 0; k < b.getNumSamples(); ++k)
            {
                const float v = 0.25f * (float) std::sin (2 * juce::MathConstants<double>::pi * 200 * k / sr);
                b.setSample (0, k, v); b.setSample (1, k, v);
            }
            auto out = render (*p, b, 1024);
            lev[i] = rmsDb (out, (int) (2.0 * sr));
        }
        std::printf ("  steady tone: dynamics 0 %% %.2f dB, dynamics 100 %% %.2f dB\n", lev[0], lev[1]);
        check (std::abs (lev[0] - lev[1]) < 0.5f, "envelope followers settle correctly at a 3 MHz internal rate");
    }
}

// ---------------------------------------------------------------- review: host bypass
static void testBypass()
{
    std::printf ("\n== Host bypass keeps the reported latency ==\n");
    juce::AudioBuffer<float> in (2, (int) (2.0 * SR));
    for (int i = 0; i < in.getNumSamples(); ++i)
    {
        const double t = i / SR;
        const float v = 0.2f * (float) std::sin (2 * juce::MathConstants<double>::pi * 110 * t)
                      + 0.1f * (float) std::sin (2 * juce::MathConstants<double>::pi * 1300 * t)
                      + 0.05f * (float) std::sin (2 * juce::MathConstants<double>::pi * 7000 * t);
        in.setSample (0, i, v); in.setSample (1, i, 0.8f * v);
    }
    for (int q = 0; q < 2; ++q)
    {
        auto p = makeProc();
        setP (*p, "b1_type", (float) Algo::Amp);
        setP (*p, "drive", 40);
        setP (*p, "os_quality", (float) q);
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        for (int s = 0; s < out.getNumSamples(); s += 512)
        {
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, s, std::min (512, out.getNumSamples() - s));
            p->processBlockBypassed (view, midi);
        }
        const int L = p->expectedLatency();
        double e = 0, r = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 24000; i < in.getNumSamples() - L; ++i)
            {
                const double d = out.getSample (c, i + L) - in.getSample (c, i);
                e += d * d; r += (double) in.getSample (c, i) * in.getSample (c, i);
            }
        const float nullDb = (float) (10.0 * std::log10 (e / r + 1e-30));
        std::printf ("  %-13s latency %d, bypassed output vs input delayed by latency: %.1f dB\n",
                     q == 0 ? "linear phase" : "low latency", L, nullDb);
        if (q == 0) check (nullDb < -50.0f, "bypass passes the input unchanged, delayed by exactly the reported latency");
        else        check (nullDb < -10.0f, "low-latency bypass is time-aligned (phase follows the IIR filters)");
    }
}

// ---------------------------------------------------------------- review: concurrency
// Audio processing on this thread while another thread saves and restores state, loads presets
// and moves parameters, the way hosts and UIs do. Run under ThreadSanitizer to check for races.
static void testConcurrency()
{
    std::printf ("\n== Concurrency: processing while another thread saves, loads and automates ==\n");
    auto p = makeProc (256);
    std::atomic<bool> stop { false };
    std::atomic<int> ops { 0 };
    std::thread other ([&]
    {
        std::mt19937 rng (11);
        juce::MemoryBlock mb;
        while (! stop.load())
        {
            switch (rng() % 4)
            {
                case 0: p->getStateInformation (mb); break;
                case 1: if (mb.getSize() > 0) p->setStateInformation (mb.getData(), (int) mb.getSize()); break;
                case 2: p->setCurrentProgram ((int) (rng() % (unsigned) p->getNumPrograms())); break;
                default:
                {
                    auto params = p->getParameters();
                    params[(int) (rng() % (unsigned) params.size())]->setValueNotifyingHost (std::uniform_real_distribution<float> (0, 1) (rng));
                    p->ui.band.store ((int) (rng() % 3));
                    break;
                }
            }
            ++ops;
        }
    });
    const auto in = drumLoop (0.5);
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    bool finite = true;
    for (int k = 0; k < 3000; ++k)
    {
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, in, c, (k * 256) % (in.getNumSamples() - 256), 256);
        p->processBlock (buf, midi);
        finite = finite && allFinite (buf);
    }
    stop = true;
    other.join();
    std::printf ("  3000 audio blocks processed while %d state / preset / parameter operations ran\n", ops.load());
    check (finite, "audio stays valid while state is changed concurrently");
}

// Editor open on the message thread (timers, meters, displays, preset and mode changes)
// while a separate thread runs the audio callback.
static void testConcurrencyWithEditor()
{
    std::printf ("\n== Concurrency with the editor open ==\n");
    auto p = makeProc (256);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    ed->setSize (KlingeEditor::designW, KlingeEditor::designH);
    std::atomic<bool> stop { false };
    std::atomic<int> blocks { 0 };
    std::atomic<bool> finite { true };
    std::thread audio ([&]
    {
        const auto in = drumLoop (0.5);
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        int k = 0;
        while (! stop.load())
        {
            for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, in, c, (k++ * 256) % (in.getNumSamples() - 256), 256);
            p->processBlock (buf, midi);
            if (! allFinite (buf)) finite = false;
            ++blocks;
        }
    });
    std::mt19937 rng (21);
    for (int step = 0; step < 60; ++step)
    {
        auto params = p->getParameters();
        params[(int) (rng() % (unsigned) params.size())]->setValueNotifyingHost (std::uniform_real_distribution<float> (0, 1) (rng));
        if (step % 10 == 0) p->setCurrentProgram ((int) (rng() % (unsigned) p->getNumPrograms()));
        p->ui.advanced.store (step % 2 == 0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    }
    stop = true;
    audio.join();
    ed.reset();
    std::printf ("  %d audio blocks while the editor refreshed and settings changed\n", blocks.load());
    check (finite.load() && blocks.load() > 100, "editor and audio thread run side by side without problems");
}


// ================================================================ v2.0 features
struct TestPlayHead : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        return p;
    }
};

static juce::AudioBuffer<float> renderWith (KlingeProcessor& p, const juce::AudioBuffer<float>& in, int block,
                                            TestPlayHead* ph, std::function<void (int, juce::MidiBuffer&)> midiAt = {})
{
    juce::AudioBuffer<float> out (in);
    for (int s = 0; s < out.getNumSamples(); s += block)
    {
        const int n = std::min (block, out.getNumSamples() - s);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), s, n);
        juce::MidiBuffer midi;
        if (midiAt) midiAt (s, midi);
        p.processBlock (view, midi);
        if (ph != nullptr) ph->ppq += n / SR * ph->bpm / 60.0;
    }
    return out;
}

static float windowRmsDb (const juce::AudioBuffer<float>& b, int from, int to)
{
    double acc = 0;
    for (int i = from; i < to; ++i) acc += (double) b.getSample (0, i) * b.getSample (0, i);
    return (float) (10.0 * std::log10 (acc / std::max (1, to - from) + 1e-20));
}

static void testCircuits()
{
    std::printf ("\n== Circuit models ==\n");
    const Algo circ[] = { Algo::Triode, Algo::Diode, Algo::Hysteresis, Algo::Iron, Algo::Stack };

    // level without auto gain (compensation tables)
    for (auto a : circ)
        for (float d : { 0.0f, 24.0f, 48.0f })
        {
            auto p = makeProc();
            setP (*p, "b1_type", (float) a);
            setP (*p, "drive", d);
            setP (*p, "auto_gain", 0);
            const auto in = sine (200.0, 0.25f, 1.5);
            auto out = render (*p, in);
            const float diff = rmsDb (out, 48000) - rmsDb (in, 48000);
            check (std::abs (diff) < 3.0f, algoNames()[(int) a] + " at " + juce::String (d, 0) + " dB drive keeps its level without auto gain ("
                                                + juce::String (diff, 2) + " dB)");
        }

    // extreme input: no NaN, bounded, and silence afterwards decays to silence
    for (auto a : circ)
    {
        auto p = makeProc();
        setP (*p, "b1_type", (float) a);
        setP (*p, "drive", 48);
        setP (*p, "b1_bias", 80);
        setP (*p, "b1_shape", 100);
        std::mt19937 rng (3);
        std::uniform_real_distribution<float> ud (-4.0f, 4.0f);
        juce::AudioBuffer<float> in (2, (int) (4.0 * SR));
        in.clear();
        for (int i = 0; i < (int) SR; ++i) { const float v = ud (rng) + 2.0f; in.setSample (0, i, v); in.setSample (1, i, -v); }
        auto out = render (*p, in);
        const float tail = out.getMagnitude ((int) (3.5 * SR), (int) (0.5 * SR));
        check (allFinite (out) && out.getMagnitude (0, out.getNumSamples()) < 8.0f && tail < 1.0e-3f,
               algoNames()[(int) a] + ": +12 dBFS noise with DC stays bounded, then decays to silence (tail "
                   + juce::String (juce::Decibels::gainToDecibels (tail), 1) + " dBFS)");
    }

    // Iron: lows saturate first
    {
        float thd[2] {};
        int k = 0;
        for (double f : { 60.0, 2000.0 })
        {
            auto p = makeProc();
            setP (*p, "b1_type", (float) Algo::Iron);
            setP (*p, "drive", 12);
            setP (*p, "b1_shape", 50);
            auto out = render (*p, sine (f, 0.25f, 2.0));
            thd[k++] = harmonics (out, f).thd;
        }
        std::printf ("  Iron THD at 60 Hz %.2f %%, at 2 kHz %.3f %%\n", thd[0], thd[1]);
        check (thd[0] > 10.0f * thd[1], "Iron: low frequencies distort far more than highs");
    }

    // Hysteresis: output depends on direction (memory)
    {
        BandProcessor bp;
        bp.prepare (SR * 4, 4);
        BandParams bpar;
        bpar.type = Algo::Hysteresis;
        bpar.driveDb = 12.0f;
        bpar.shape = 1.0f;
        HarmonicRecipe r;
        bp.setParams (bpar, 0.0f, 0.0f, r, 0.0f, 0, 0);
        float up = 0, down = 0;
        const int N = (int) (SR * 4 / 50.0);    // one 50 Hz period at the oversampled rate
        for (int cyc = 0; cyc < 6; ++cyc)
            for (int i = 0; i < N; ++i)
            {
                bp.tick();
                const float x = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * (float) i / (float) N);
                const float y = bp.processSample (x, 0);
                if (cyc == 5 && i == N / 2 - 1) down = y;                 // x ~ 0, falling
                if (cyc == 5 && i == N - 1) up = y;                       // x ~ 0, rising
            }
        std::printf ("  Hysteresis at zero input: rising %.4f, falling %.4f\n", up, down);
        check (std::abs (up - down) > 0.01f, "Hysteresis: the loop has memory (output differs by direction)");
    }

    // Triode: a loud hit shifts the bias, then it recovers (blocking / bloom)
    {
        auto p = makeProc();
        setP (*p, "b1_type", (float) Algo::Triode);
        setP (*p, "drive", 18);
        setP (*p, "b1_shape", 100);
        setP (*p, "auto_gain", 0);
        juce::AudioBuffer<float> in (2, (int) (3.0 * SR));
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const float amp = (i > (int) SR && i < (int) (1.2 * SR)) ? 1.0f : 0.1f;
            const float v = amp * (float) std::sin (2 * juce::MathConstants<double>::pi * 220 * i / SR);
            in.setSample (0, i, v); in.setSample (1, i, v);
        }
        auto out = render (*p, in);
        auto h2At = [&out] (int start)
        {
            juce::AudioBuffer<float> seg (1, 8192);
            seg.copyFrom (0, 0, out, 0, start, 8192);
            auto s = spectrum (seg, 0, 13);
            return juce::Decibels::gainToDecibels (peakNear (s, 440.0, 2) / peakNear (s, 220.0, 2));
        };
        const float before = h2At ((int) (0.8 * SR)), after = h2At ((int) (1.21 * SR)), late = h2At ((int) (2.8 * SR) - 8192);
        std::printf ("  Triode 2nd harmonic of a quiet tone: before %.1f dB, right after a loud hit %.1f dB, 1.5 s later %.1f dB\n",
                     before, after, late);
        check (std::abs (after - before) > 3.0f && std::abs (late - before) < 1.5f,
               "Triode: loud hits move the operating point, which then recovers");
    }

    // Diode with a dark corner: lows clip harder than highs
    {
        float thd[2] {};
        int k = 0;
        for (double f : { 100.0, 6000.0 })
        {
            auto p = makeProc();
            setP (*p, "b1_type", (float) Algo::Diode);
            setP (*p, "drive", 24);
            setP (*p, "b1_shape", 0);
            setP (*p, "auto_gain", 0);
            auto out = render (*p, sine (f, 0.25f, 2.0));
            thd[k++] = harmonics (out, f).thd;
        }
        std::printf ("  Diode (dark) THD at 100 Hz %.2f %%, at 6 kHz %.2f %%\n", thd[0], thd[1]);
        check (thd[0] > 2.0f * thd[1], "Diode: clipping depends on frequency (capacitor)");
    }
}

static void testModulation()
{
    std::printf ("\n== Modulation ==\n");
    // LFO on output gain: smooth, no zipper sidebands at the 1.5 kHz control rate
    {
        auto p = makeProc();
        setP (*p, "mix", 0);
        setP (*p, "lfo1_sync", 0);
        setP (*p, "lfo1_rate", 2.0f);
        p->modMatrix.assign ((int) ModSource::Lfo1, "out_gain", 0.25f);
        auto out = render (*p, sine (1000.0, 0.25f, 3.0));
        auto sp = spectrum (out, out.getNumSamples() - (1 << 15) - 10);
        const float fund = peakNear (sp, 1000.0, 8);
        float worst = 0;
        for (double f : { 500.0, 2500.0, 4000.0, 5500.0, 7000.0 }) worst = std::max (worst, peakNear (sp, f, 20));
        const float side = peakNear (sp, 1006.0, 3);   // LFO sidebands sit right next to the tone (expected)
        std::printf ("  LFO 2 Hz on output gain: zipper sidebands %.1f dB, intended sidebands %.1f dB re. tone\n",
                     juce::Decibels::gainToDecibels (worst / fund), juce::Decibels::gainToDecibels (side / fund));
        check (worst / fund < juce::Decibels::decibelsToGain (-80.0f), "modulation has no zipper noise at the control rate");
        check (std::abs (rmsDb (out, (int) SR, (int) SR) - rmsDb (out, (int) (1.25 * SR), 1000)) > 1.0f, "LFO actually moves the level");
    }

    // shaper synced to the host: gate on the first half of every beat, aligned for latency
    {
        auto p = makeProc();
        TestPlayHead ph;
        p->setPlayHead (&ph);
        setP (*p, "mix", 0);
        setP (*p, "shp_amount", 100);
        setP (*p, "shp_rate", 5);      // 1/4
        setP (*p, "shp_smooth", 0.5f);
        p->shaper.setPoints (ShaperCurve::preset (3));    // Gate 1/2
        setP (*p, "mix", 100);
        setP (*p, "b1_mix", 0);
        auto out = renderWith (*p, sine (300.0, 0.25f, 4.0), 512, &ph);
        const int L = p->expectedLatency();
        const int beat = 24000;
        float on = 0, off = 0;
        for (int k = 2; k < 7; ++k)
        {
            on  += windowRmsDb (out, k * beat + L + 1000, k * beat + L + 11000);
            off += windowRmsDb (out, k * beat + L + 13000, k * beat + L + 23000);
        }
        on /= 5; off /= 5;
        std::printf ("  shaper gate 1/4: open %.1f dB, closed %.1f dB\n", on, off);
        check (on - off > 40.0f, "shaper follows the host tempo and position, compensated for latency");
        p->setPlayHead (nullptr);
    }

    // envelope follower on the sidechain: kick bursts duck the output
    {
        auto p = std::make_unique<KlingeProcessor>();
        auto layout = p->getBusesLayout();
        layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
        const bool ok = p->setBusesLayout (layout);
        p->setRateAndBufferSizeDetails (SR, 512);
        p->prepareToPlay (SR, 512);
        setP (*p, "mix", 0);
        setP (*p, "env1_src", 1);
        setP (*p, "env1_att", 1);
        setP (*p, "env1_rel", 60);
        setP (*p, "env1_gain", 12);
        p->modMatrix.assign ((int) ModSource::Env1, "out_gain", -0.5f);
        juce::AudioBuffer<float> buf (4, (int) (3.0 * SR));
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const float v = 0.2f * (float) std::sin (2 * juce::MathConstants<double>::pi * 440 * i / SR);
            buf.setSample (0, i, v); buf.setSample (1, i, v);
            const int ph = i % 24000;
            const float kick = ph < 4000 ? 0.8f * (float) std::sin (2 * juce::MathConstants<double>::pi * 60 * ph / SR) : 0.0f;
            buf.setSample (2, i, kick); buf.setSample (3, i, kick);
        }
        juce::MidiBuffer midi;
        for (int s = 0; s < buf.getNumSamples(); s += 512)
        {
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 4, s, std::min (512, buf.getNumSamples() - s));
            p->processBlock (view, midi);
        }
        const int L = p->expectedLatency();
        const float during = windowRmsDb (buf, 48000 + L + 1500, 48000 + L + 3500);
        const float between = windowRmsDb (buf, 48000 + L + 14000, 48000 + L + 22000);
        std::printf ("  sidechain ducking: during kick %.1f dB, between kicks %.1f dB\n", during, between);
        check (ok && between - during > 10.0f, "envelope follower reads the sidechain input");
    }

    // MIDI velocity
    {
        auto p = makeProc();
        setP (*p, "mix", 0);
        p->modMatrix.assign ((int) ModSource::Velocity, "out_gain", 0.25f);
        auto out = renderWith (*p, sine (500.0, 0.1f, 2.0), 512, nullptr, [] (int s, juce::MidiBuffer& m)
        {
            if (s == 48128) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
        });
        const float before = rmsDb (out, 20000, 20000), after = rmsDb (out, 70000, 20000);
        std::printf ("  velocity 127 on output gain: before %.1f dB, after %.1f dB\n", before, after);
        check (after - before > 10.0f, "MIDI velocity modulates");
    }

    // state: slots and shaper points survive save / restore
    {
        auto a = makeProc();
        a->modMatrix.assign ((int) ModSource::Lfo2, "b2_shape", -0.33f);
        a->modMatrix.assign ((int) ModSource::Env1, "post_lp", 0.5f);
        a->shaper.setPoints (ShaperCurve::preset (8));
        juce::MemoryBlock mb;
        a->getStateInformation (mb);
        auto b = makeProc();
        b->setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (int i = 0; i < numModSlots; ++i)
        {
            const auto x = a->modMatrix.getSlot (i), y = b->modMatrix.getSlot (i);
            same = same && x.source == y.source && x.target == y.target && std::abs (x.depth - y.depth) < 1e-6f;
        }
        check (same && a->shaper.getPoints() == b->shaper.getPoints(), "modulation slots and shaper curve survive save / restore");
    }
}

static void testFeatures()
{
    std::printf ("\n== Bands, stereo, feedback, render quality ==\n");
    // 5 bands: split + phase compensation equals the dry path exactly, so blending never combs
    {
        const auto in = pinkNoise (-18.0f, 3.0);
        juce::AudioBuffer<float> outs[2];
        for (int k = 0; k < 2; ++k)
        {
            auto p = makeProc();
            setP (*p, "routing", 1);
            setP (*p, "num_bands", 3);
            setP (*p, "xover_lo", 90); setP (*p, "xover_hi", 400); setP (*p, "xover_3", 2500); setP (*p, "xover_4", 9000);
            setP (*p, "auto_gain", 0);
            setP (*p, "mix", k == 0 ? 100.0f : 0.0f);
            for (int b = 1; b <= 5; ++b) setP (*p, "b" + juce::String (b) + "_mix", 0);
            outs[k] = render (*p, in);
        }
        double e = 0, r = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 48000; i < in.getNumSamples(); ++i)
            {
                const double d = outs[0].getSample (c, i) - outs[1].getSample (c, i);
                e += d * d; r += (double) outs[1].getSample (c, i) * outs[1].getSample (c, i);
            }
        const float nullDb = (float) (10.0 * std::log10 (e / r + 1e-30));
        std::printf ("  5 bands with clean bands vs dry path: %.1f dB\n", nullDb);
        check (nullDb < -100.0f, "5-band split sums to exactly the dry path (no combing when blending)");
    }
    // per-band Mid / Side: the other half of the stereo image passes untouched
    for (int mode : { 1, 2 })
    {
        juce::AudioBuffer<float> in (2, (int) (2.0 * SR));
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const float v = 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * 700 * i / SR);
            in.setSample (0, i, v);
            in.setSample (1, i, mode == 1 ? -v : v);            // Mid mode gets pure side, Side mode gets pure mid
        }
        juce::AudioBuffer<float> outs[2];
        for (int k = 0; k < 2; ++k)
        {
            auto p = makeProc();
            setP (*p, "routing", 1);
            setP (*p, "drive", 36);
            setP (*p, "auto_gain", 0);
            setP (*p, "mix", k == 0 ? 100.0f : 0.0f);
            for (int b = 1; b <= 3; ++b) { setP (*p, "b" + juce::String (b) + "_stereo", (float) mode); setP (*p, "b" + juce::String (b) + "_type", 4); }
            outs[k] = render (*p, in);
        }
        double e = 0, r = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 24000; i < in.getNumSamples(); ++i)
            {
                const double d = outs[0].getSample (c, i) - outs[1].getSample (c, i);
                e += d * d; r += (double) outs[1].getSample (c, i) * outs[1].getSample (c, i);
            }
        const float nullDb = (float) (10.0 * std::log10 (e / r + 1e-30));
        std::printf ("  band mode %s with only %s input, heavy Amp: output vs clean path %.1f dB\n", mode == 1 ? "Mid" : "Side", mode == 1 ? "side" : "mid", nullDb);
        check (nullDb < -60.0f, juce::String (mode == 1 ? "Mid" : "Side") + " mode leaves the other half of the stereo image untouched");
    }
    // feedback: bounded, rings with signal, dies away in silence
    {
        auto p = makeProc();
        setP (*p, "b1_type", (float) Algo::Grind);
        setP (*p, "drive", 30);
        setP (*p, "fb_amount", 95);
        setP (*p, "fb_time", 1.0f);
        setP (*p, "fb_tone", 20000);
        juce::AudioBuffer<float> in (2, (int) (4.0 * SR));
        in.clear();
        std::mt19937 rng (9);
        std::uniform_real_distribution<float> ud (-1.0f, 1.0f);
        for (int i = 0; i < (int) SR; ++i) { const float v = ud (rng); in.setSample (0, i, v); in.setSample (1, i, v); }
        auto out = render (*p, in);
        const float tail = out.getMagnitude ((int) (3.0 * SR), (int) SR);
        std::printf ("  feedback 95 %%: peak %.1f dBFS, tail after 2 s of silence %.1f dBFS\n",
                     juce::Decibels::gainToDecibels (out.getMagnitude (0, out.getNumSamples())), juce::Decibels::gainToDecibels (tail, -200.0f));
        check (allFinite (out) && out.getMagnitude (0, out.getNumSamples()) < 8.0f && tail < 1.0e-4f,
               "feedback stays bounded and stops when the input stops");
    }
    // render quality: the reported latency covers live and offline, both are aligned
    {
        float xc[2] {};
        int lat[2] {};
        for (int offline = 0; offline < 2; ++offline)
        {
            auto p = makeProc();
            setP (*p, "oversampling", 2);
            setP (*p, "render_os", 3);
            setP (*p, "mix", 0);
            p->setNonRealtime (offline == 1);
            auto in = sine (1000.0, 0.25f, 1.0);
            auto out = render (*p, in);
            lat[offline] = p->expectedLatency();
            double e = 0, r = 0;
            for (int i = 12000; i < 40000; ++i)
            {
                const double d = out.getSample (0, i + lat[offline]) - in.getSample (0, i);
                e += d * d; r += (double) in.getSample (0, i) * in.getSample (0, i);
            }
            xc[offline] = (float) (10.0 * std::log10 (e / r + 1e-30));
        }
        std::printf ("  render quality 16x: reported latency %d / %d, live null %.1f dB, offline null %.1f dB\n", lat[0], lat[1], xc[0], xc[1]);
        check (lat[0] == lat[1] && xc[0] < -40.0f && xc[1] < -40.0f, "live and offline render share one latency, both aligned");
    }
    // v1.0 sessions still load
    {
        auto p = makeProc();
        juce::XmlElement xml ("KLINGE");
        auto add = [&xml] (const char* id, double v) { auto* e = xml.createNewChildElement ("PARAM"); e->setAttribute ("id", id); e->setAttribute ("value", v); };
        add ("drive", 30.0); add ("b1_type", 4.0); add ("routing", 1.0); add ("xover_lo", 150.0); add ("b3_mix", 40.0);
        auto* ui = xml.createNewChildElement ("UI");
        ui->setAttribute ("advanced", 1);
        ui->setAttribute ("preset", "My old sound");
        juce::MemoryBlock mb;
        juce::AudioProcessor::copyXmlToBinary (xml, mb);
        p->setStateInformation (mb.getData(), (int) mb.getSize());
        auto v = [&p] (const char* id) { return p->apvts.getRawParameterValue (id)->load(); };
        check (std::abs (v ("drive") - 30) < 0.01f && (int) v ("b1_type") == 4 && (int) v ("routing") == 1
                   && std::abs (v ("xover_lo") - 150) < 0.5f && std::abs (v ("b3_mix") - 40) < 0.1f
                   && (int) v ("num_bands") == 1 && std::abs (v ("fb_amount")) < 0.001f && p->ui.advanced.load()
                   && p->presets.getCurrentName() == "My old sound",
               "a v1.0 session loads with its values; new parameters take their defaults");
    }
}

static void testSession()
{
    std::printf ("\n== Undo, snapshots, random, capture ==\n");
    auto p = makeProc();
    auto& s = p->session;
    s.setEditorOpen (true);
    auto* drive = p->apvts.getParameter ("drive");
    auto gesture = [drive] (float v) { drive->beginChangeGesture(); drive->setValueNotifyingHost (drive->convertTo0to1 (v)); drive->endChangeGesture(); };
    auto wait = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
    gesture (20.0f); wait (1300);
    gesture (35.0f); wait (1300);
    const float now = drive->convertFrom0to1 (drive->getValue());
    s.undo(); wait (100);
    const float undone = drive->convertFrom0to1 (drive->getValue());
    s.redo(); wait (100);
    const float redone = drive->convertFrom0to1 (drive->getValue());
    std::printf ("  drive %.1f -> undo %.1f -> redo %.1f\n", now, undone, redone);
    check (std::abs (now - 35) < 0.1f && std::abs (undone - 20) < 0.1f && std::abs (redone - 35) < 0.1f, "undo and redo restore user edits");

    // automation does not create undo steps
    drive->setValueNotifyingHost (drive->convertTo0to1 (10.0f)); wait (1300);
    s.undo(); wait (100);
    check (std::abs (drive->convertFrom0to1 (drive->getValue()) - 20) < 0.1f, "host automation is not an undo step");

    // snapshots
    s.redo(); wait (100);
    gesture (12.0f); wait (600);
    s.selectSnapshot (1);
    gesture (44.0f);
    p->modMatrix.assign ((int) ModSource::Lfo1, "mix", 0.3f);
    s.selectSnapshot (0);
    const float a = drive->convertFrom0to1 (drive->getValue());
    const bool modA = p->modMatrix.slotsForTarget ("mix").empty();
    s.selectSnapshot (1);
    const float b = drive->convertFrom0to1 (drive->getValue());
    const bool modB = ! p->modMatrix.slotsForTarget ("mix").empty();
    std::printf ("  snapshot A drive %.1f, snapshot B drive %.1f\n", a, b);
    check (std::abs (a - 12) < 0.1f && std::abs (b - 44) < 0.1f && modA && modB, "A/B snapshots hold parameters and modulation");

    // locks survive preset loading
    s.setLocked ("out_gain", true);
    setP (*p, "out_gain", -7.0f);
    p->loadPresetByName ("Schranz Stack");
    check (std::abs (p->apvts.getRawParameterValue ("out_gain")->load() + 7.0f) < 0.01f
               && (int) p->apvts.getRawParameterValue ("b1_type")->load() == (int) Algo::Stack,
           "locked parameters keep their value when a preset loads");

    // randomise many times: always valid audio
    bool fine = true;
    const auto in = drumLoop (0.5);
    for (int k = 0; k < 40; ++k)
    {
        s.randomise();
        auto out = render (*p, in);
        fine = fine && allFinite (out) && out.getMagnitude (0, out.getNumSamples()) < 8.0f;
    }
    check (fine && std::abs (p->apvts.getRawParameterValue ("out_gain")->load() + 7.0f) < 0.01f, "randomise gives valid audio and respects locks");
    s.setEditorOpen (false);

    // capture
    const auto model = juce::File::getCurrentWorkingDirectory().getChildFile ("tests/data/test_lstm.nam");
    if (model.existsAsFile())
    {
        auto q = makeProc (256);
        q->capture.loadAsync (model);
        for (int i = 0; i < 50 && ! q->capture.getStatus().startsWith ("Loaded") && ! q->capture.getStatus().startsWith ("Trained"); ++i) wait (50);
        wait (300);                                    // timer hands the model to the audio side
        setP (*q, "mix", 100);
        setP (*q, "b1_mix", 0);
        setP (*q, "auto_gain", 0);
        const auto in2 = sine (220.0, 0.3f, 1.0);
        auto off = render (*q, in2, 256);
        setP (*q, "cap_on", 1);
        auto on = render (*q, in2, 256);
        on = render (*q, in2, 256);
        double e = 0;
        for (int i = 20000; i < 40000; ++i) { const double d = on.getSample (0, i) - off.getSample (0, i); e += d * d; }
        std::printf ("  capture: status \"%s\", difference on/off %.1f dB\n", q->capture.getStatus().toRawUTF8(), 10 * std::log10 (e / 20000 + 1e-20));
        check (q->capture.hasModel() && allFinite (on) && e > 1e-6, "a .nam capture loads and processes audio");

        auto bad = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("klinge_bad.nam");
        bad.replaceWithText ("{ not a model");
        q->capture.loadAsync (bad);
        for (int i = 0; i < 40 && ! q->capture.getStatus().startsWith ("Could not"); ++i) wait (50);
        auto after = render (*q, in2, 256);
        check (q->capture.getStatus().startsWith ("Could not") && allFinite (after), "a broken capture file is reported and does not interrupt audio");
        bad.deleteFile();
    }
    else check (false, "test model tests/data/test_lstm.nam found (run from the project folder)");
}

// ---------------------------------------------------------------- parameter text
// value -> text -> value -> text must be stable for every parameter (hosts type values in, CLAP validates it)
static void testParameterText()
{
    std::printf ("\n== Parameter text round trip ==\n");
    auto p = makeProc();
    int bad = 0, tested = 0;
    for (auto* prm : p->getParameters())
    {
        for (int k = 0; k <= 400; ++k)
        {
            const float v = (float) k / 400.0f;
            const auto s1 = prm->getText (v, 1024);
            const float v2 = prm->getValueForText (s1);
            const auto s2 = prm->getText (v2, 1024);
            ++tested;
            if (s1 != s2)
            {
                if (bad < 5) std::printf ("  %s: %.4f -> \"%s\" -> %.4f -> \"%s\"\n", prm->getName (64).toRawUTF8(), v, s1.toRawUTF8(), v2, s2.toRawUTF8());
                ++bad;
            }
        }
    }
    std::printf ("  %d conversions, %d inconsistent\n", tested, bad);
    check (bad == 0, "text of every parameter converts back to the same value");
}

// ---------------------------------------------------------------- capture with real architectures
// Uses the example models of NAM Core (KLINGE_NAM_EXAMPLES is set by CMake): a small WaveNet and the
// standard-size WaveNet that most published amp profiles use, for loading and CPU cost.
static void testCaptureModels()
{
    std::printf ("\n== Capture: WaveNet profiles ==\n");
#ifdef KLINGE_NAM_EXAMPLES
    const juce::File dir (KLINGE_NAM_EXAMPLES);
    auto wait = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
    for (auto name : { "wavenet.nam", "wavenet_a1_standard.nam" })
    {
        const auto f = dir.getChildFile (name);
        if (! f.existsAsFile()) { check (false, juce::String ("example model ") + name + " found"); continue; }
        for (bool stereo : { false, true })
        {
            auto q = makeProc (512);
            q->capture.loadAsync (f);
            for (int i = 0; i < 100 && ! q->capture.hasModel(); ++i) wait (50);
            wait (200);
            setP (*q, "cap_on", 1);
            setP (*q, "cap_stereo", stereo ? 1.0f : 0.0f);
            setP (*q, "oversampling", 0);
            setP (*q, "b1_type", 0);
            const auto in = drumLoop (10.0);
            render (*q, drumLoop (0.2));
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            auto out = render (*q, in);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const double cpu = 100.0 * ms / 10000.0;
            std::printf ("  %-26s %-6s %5.1f%% of one core at 48 kHz (%s)\n", name, stereo ? "stereo" : "mono", cpu,
                         q->capture.getStatus().toRawUTF8());
            check (q->capture.hasModel() && allFinite (out) && out.getMagnitude (0, out.getNumSamples()) > 1.0e-3f,
                   juce::String (name) + (stereo ? " (true stereo)" : " (mono)") + " loads and processes audio");
        }
    }
#else
    check (false, "KLINGE_NAM_EXAMPLES defined");
#endif
}

// ---------------------------------------------------------------- golden fingerprints
// Every factory preset renders a fixed signal; a compact fingerprint of the result (level, stereo
// balance, octave spectrum) is compared with tests/data/golden.txt. This catches unintended changes of
// the sound during refactoring. The signal is generated without std:: distributions, whose output
// differs between standard libraries, so the fingerprints hold on every platform.
static juce::AudioBuffer<float> goldenSignal()
{
    const int n = (int) (2.5 * SR);
    juce::AudioBuffer<float> b (2, n);
    uint32_t st = 12345u;
    auto rnd = [&st] { st = st * 1664525u + 1013904223u; return (float) (int32_t) st * (1.0f / 2147483648.0f); };
    double kickPhase = 0.0;
    float prevNoise = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / SR;
        const double beat = std::fmod (t, 0.5);
        kickPhase += 2.0 * juce::MathConstants<double>::pi * (45.0 + 105.0 * std::exp (-beat * 30.0)) / SR;
        const float kick = 0.7f * (float) (std::sin (kickPhase) * std::exp (-beat * 9.0));
        const double off = std::fmod (t + 0.25, 0.5);
        const float w = rnd();
        const float hat = 0.25f * (w - prevNoise) * (float) std::exp (-off * 60.0);
        prevNoise = w;
        const float tone = 0.08f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * t)
                         + 0.05f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1320.0 * t + 0.3);
        b.setSample (0, i, kick + hat + tone);
        b.setSample (1, i, kick + 0.8f * hat + 0.9f * tone);
    }
    return b;
}

static constexpr int goldenValues = 11;
static std::array<float, goldenValues> fingerprint (const juce::AudioBuffer<float>& out, int latency)
{
    std::array<float, goldenValues> v {};
    const int start = (int) (0.5 * SR) + latency;
    const int n = out.getNumSamples() - start;
    double m = 0.0, sd = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double l = out.getSample (0, start + i), r = out.getSample (1, start + i);
        m += 0.25 * (l + r) * (l + r);
        sd += 0.25 * (l - r) * (l - r);
    }
    v[0] = (float) (10.0 * std::log10 ((m + sd) / n + 1.0e-20));
    v[1] = (float) (10.0 * std::log10 ((sd + 1.0e-20) / (m + 1.0e-20)));

    constexpr int order = 13, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> work ((size_t) N * 2);
    std::vector<double> power ((size_t) N / 2, 0.0);
    int frames = 0;
    for (int pos = 0; pos + N <= n; pos += N / 2, ++frames)
    {
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const float w = 0.5f - 0.5f * (float) std::cos (2.0 * juce::MathConstants<double>::pi * i / (N - 1));
            work[(size_t) i] = 0.5f * (out.getSample (0, start + pos + i) + out.getSample (1, start + pos + i)) * w;
        }
        fft.performFrequencyOnlyForwardTransform (work.data());
        for (int k = 0; k < N / 2; ++k) power[(size_t) k] += (double) work[(size_t) k] * work[(size_t) k];
    }
    double total = 1.0e-30;
    for (auto pw : power) total += pw;
    for (int bnd = 0; bnd < 9; ++bnd)
    {
        const double fc = 62.5 * std::pow (2.0, bnd);
        const int k0 = (int) std::ceil (fc / std::sqrt (2.0) / (SR / N)), k1 = (int) std::floor (fc * std::sqrt (2.0) / (SR / N));
        double e = 1.0e-30;
        for (int k = std::max (1, k0); k <= std::min (N / 2 - 1, k1); ++k) e += power[(size_t) k];
        v[(size_t) bnd + 2] = (float) (10.0 * std::log10 (e / total));       // share of the energy, dB
    }
    juce::ignoreUnused (frames);
    return v;
}

static std::map<juce::String, std::array<float, goldenValues>> renderGolden()
{
    std::map<juce::String, std::array<float, goldenValues>> result;
    const auto in = goldenSignal();
    for (auto& fp : factoryPresets())
    {
        auto p = makeProc (512);
        p->loadPresetByName (fp.name);
        result[fp.name] = fingerprint (render (*p, in), p->expectedLatency());
    }
    return result;
}

static juce::File goldenFile() { return juce::File::getCurrentWorkingDirectory().getChildFile ("tests/data/golden.txt"); }

static void generateGolden()
{
    juce::String text ("# KLINGE golden fingerprints: preset | rms dB, side/mid dB, octave shares 62.5 Hz .. 16 kHz (dB)\n");
    for (auto& [name, v] : renderGolden())
    {
        text << name << " |";
        for (auto x : v) text << " " << juce::String (x, 3);
        text << "\n";
    }
    goldenFile().replaceWithText (text);
    std::printf ("wrote %s\n", goldenFile().getFullPathName().toRawUTF8());
}

static void testGolden()
{
    std::printf ("\n== Golden fingerprints of all factory presets ==\n");
    const auto f = goldenFile();
    check (f.existsAsFile(), "tests/data/golden.txt exists (KlingeTests gen-golden writes it)");
    if (! f.existsAsFile()) return;
    std::map<juce::String, std::array<float, goldenValues>> ref;
    juce::StringArray lines;
    f.readLines (lines);
    for (auto& l : lines)
    {
        if (l.startsWith ("#") || ! l.contains ("|")) continue;
        auto vals = juce::StringArray::fromTokens (l.fromFirstOccurrenceOf ("|", false, false), " ", "");
        vals.removeEmptyStrings();
        std::array<float, goldenValues> a {};
        for (int i = 0; i < goldenValues && i < vals.size(); ++i) a[(size_t) i] = vals[i].getFloatValue();
        ref[l.upToFirstOccurrenceOf (" |", false, false)] = a;
    }
    float worstRms = 0, worstBand = 0;
    juce::String worstName;
    int compared = 0;
    bool allOk = true;
    for (auto& [name, v] : renderGolden())
    {
        auto it = ref.find (name);
        if (it == ref.end()) { check (false, "golden entry for " + name); allOk = false; continue; }
        const auto& r = it->second;
        const float dRms = std::abs (v[0] - r[0]);
        const float dSide = r[1] > -60.0f || v[1] > -60.0f ? std::abs (v[1] - r[1]) : 0.0f;
        float dBand = 0.0f;
        for (int b = 2; b < goldenValues; ++b)
            if (r[(size_t) b] > -60.0f || v[(size_t) b] > -60.0f) dBand = std::max (dBand, std::abs (v[(size_t) b] - r[(size_t) b]));
        if (dRms > worstRms) worstRms = dRms;
        if (dBand > worstBand) { worstBand = dBand; worstName = name; }
        const bool ok = dRms < 0.3f && dSide < 1.0f && dBand < 0.75f;
        if (! ok) std::printf ("  %-24s rms %+.2f dB, side %+.2f dB, worst band %.2f dB\n", name.toRawUTF8(), v[0] - r[0], v[1] - r[1], dBand);
        allOk = allOk && ok;
        ++compared;
    }
    std::printf ("  %d presets compared. Largest level difference %.3f dB, largest band difference %.3f dB (%s)\n",
                 compared, worstRms, worstBand, worstName.toRawUTF8());
    check (allOk && compared == (int) factoryPresets().size(), "every factory preset sounds as recorded");
}

// ---------------------------------------------------------------- editor logic
template <typename T>
static T* findChild (juce::Component& root, std::function<bool (T&)> match = {})
{
    for (auto* c : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (c))
            if (! match || match (*t)) return t;
        if (auto* r = findChild<T> (*c, match)) return r;
    }
    return nullptr;
}

static void testEditorLogic()
{
    std::printf ("\n== Editor logic ==\n");
    using namespace klinge::ui;
    auto p = makeProc();
    p->ui.advanced.store (true);
    p->ui.tab.store (0);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* ke = dynamic_cast<KlingeEditor*> (ed.get());
    ed->setSize (KlingeEditor::designW, KlingeEditor::designH);
    ed->setVisible (true);
    auto pump = [&] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
    pump (100);

    // drag and drop of a modulation source onto a knob
    auto* driveKnob = findChild<Knob> (*ed, [] (Knob& k) { return k.paramID == "drive" && visibleInTree (k); });
    bool dropOk = false;
    if (driveKnob != nullptr)
    {
        juce::DragAndDropTarget::SourceDetails d ("mod:1", nullptr, {});
        dropOk = driveKnob->isInterestedInDragSource (d);
        driveKnob->itemDropped (d);
        dropOk = dropOk && p->modMatrix.slotsForTarget ("drive").size() == 1;
    }
    juce::DragAndDropTarget::SourceDetails bad ("something", nullptr, {});
    check (dropOk && driveKnob != nullptr && ! driveKnob->isInterestedInDragSource (bad),
           "dropping LFO 1 on Drive creates a modulation; foreign drags are ignored");
    auto* typeKnobTarget = findChild<Knob> (*ed, [] (Knob& k) { return k.paramID == "b1_shape"; });
    check (typeKnobTarget != nullptr, "band knobs are reachable in the editor");

    // undo with the keyboard
    auto* prm = p->apvts.getParameter ("focus");
    // an edit becomes an undo step once the sound has been stable for 400 ms (checked every 250 ms)
    prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (6.0f)); prm->endChangeGesture();
    pump (1300);
    prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (-4.0f)); prm->endChangeGesture();
    pump (1300);
    const bool keyHandled = ke->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
    const float afterUndo = prm->convertFrom0to1 (prm->getValue());
    ke->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
    const float afterRedo = prm->convertFrom0to1 (prm->getValue());
    std::printf ("  focus after undo %.1f dB, after redo %.1f dB\n", afterUndo, afterRedo);
    check (keyHandled && std::abs (afterUndo - 6.0f) < 0.05f && std::abs (afterRedo + 4.0f) < 0.05f, "Ctrl/Cmd+Z undoes, Shift+Ctrl/Cmd+Z redoes");

    // snapshot buttons
    auto* snapB = findChild<juce::TextButton> (*ed, [] (juce::TextButton& b) { return b.getButtonText() == "B"; });
    if (snapB != nullptr) snapB->triggerClick();
    pump (50);
    check (snapB != nullptr && p->session.getActiveSnapshot() == 1 && snapB->getToggleState(), "snapshot button B switches to snapshot B");
    if (auto* snapA = findChild<juce::TextButton> (*ed, [] (juce::TextButton& b) { return b.getButtonText() == "A"; }))
        snapA->triggerClick();
    pump (50);

    // algorithm box writes the parameter by algorithm, not by list position
    auto* algoBox = findChild<AlgoBox> (*ed);
    if (algoBox != nullptr) algoBox->setSelectedId ((int) klinge::Algo::Hysteresis + 1, juce::sendNotificationSync);
    const int typeNow = (int) p->apvts.getRawParameterValue ("b1_type")->load();
    check (algoBox != nullptr && typeNow == (int) klinge::Algo::Hysteresis, "algorithm box selects Hysteresis in band 1");
    setP (*p, "b1_type", (float) klinge::Algo::Fold);
    pump (60);
    check (algoBox != nullptr && algoBox->getSelectedId() == (int) klinge::Algo::Fold + 1, "algorithm box follows the parameter");

    // band selection stays valid when routing changes
    setP (*p, "routing", 1); setP (*p, "num_bands", 1);     // index 1 = three bands
    pump (80);
    if (auto* bands = findChild<BandsTab> (*ed)) bands->selectBand (4);
    pump (80);
    check (p->ui.band.load() == 2, "selecting band 5 with three bands active selects the last active band");
    setP (*p, "routing", 0);
    pump (120);
    const int bandAfter = p->ui.band.load();
    check (bandAfter == 0, "selected band falls back to the main band when routing changes to Single");

    // every tab can be shown and refreshed while audio runs
    bool tabsOk = true;
    for (int t = 0; t < 5; ++t)
    {
        ke->showTab (t);
        render (*p, drumLoop (0.05));
        pump (70);
        tabsOk = tabsOk && p->ui.tab.load() == t;
    }
    check (tabsOk, "all five tabs open");
    check (p->getEngine().scopeEnabled.load(), "analyser runs while the Analyse tab is visible");
    ke->showTab (0);
    pump (40);
    check (! p->getEngine().scopeEnabled.load(), "analyser stops when its tab is hidden");

    // preset browser: search and load with Enter
    ke->showBrowser (true);
    pump (40);
    auto* browser = findChild<PresetBrowser> (*ed);
    juce::TextEditor* search = browser != nullptr ? findChild<juce::TextEditor> (*browser) : nullptr;
    if (search != nullptr)
    {
        search->setText ("transformer", true);
        pump (40);
        search->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
        pump (40);
    }
    std::printf ("  preset after searching \"transformer\": %s\n", p->presets.getCurrentName().toRawUTF8());
    check (search != nullptr && p->presets.getCurrentName() == "Iron Bass Weight", "browser search by tag finds and loads the preset");
    ke->showBrowser (false);

    // mode switch follows the stored setting (as after a host restores the state)
    p->ui.advanced.store (false);
    pump (80);
    auto* basic = findChild<BasicView> (*ed);
    check (basic != nullptr && basic->isVisible(), "editor follows the stored Basic/Advanced setting");

    ed.reset();
    check (! p->getEngine().scopeEnabled.load(), "closing the editor stops the analyser");
}

// ---------------------------------------------------------------- UI snapshots
static void snapshots (const juce::File& dir)
{
    std::printf ("\n== Editor snapshots ==\n");
    struct Shot { const char* file; const char* preset; bool advanced; int tab; int band; int modSource; bool browser; bool model = false; };
    const Shot shots[] = {
        { "ui_basic.png",            "Woosh Shaper",       false, 0, 0, 1, false },
        { "ui_basic_circuit.png",    "Triode Bloom Drums", false, 0, 0, 1, false },
        { "ui_adv_bands.png",        "Five Band Master",   true,  0, 2, 1, false },
        { "ui_adv_bands_ms.png",     "Mid Grit Side Air",  true,  0, 1, 1, false },
        { "ui_adv_harmonics.png",    "Harmonic Gold",      true,  1, 0, 1, false },
        { "ui_adv_mod_shaper.png",   "Woosh Shaper",       true,  2, 0, 5, false },
        { "ui_adv_mod_lfo.png",      "LFO Fold Wobble",    true,  2, 0, 1, false },
        { "ui_adv_mod_env.png",      "Kick Duck Sidechain",true,  2, 0, 3, false },
        { "ui_adv_capture.png",      "Init",               true,  3, 0, 1, false, true },
        { "ui_adv_analyse.png",      "Schranz Stack",      true,  4, 0, 1, false },
        { "ui_browser.png",          "Schranz Stack",      true,  0, 0, 1, true  } };
    for (auto& s : shots)
    {
        auto p = makeProc();
        TestPlayHead ph;
        p->setPlayHead (&ph);
        check (p->loadPresetByName (s.preset), juce::String ("preset ") + s.preset + " exists");
        p->ui.advanced.store (s.advanced);
        p->ui.band.store (s.band);
        p->ui.tab.store (s.tab);
        p->ui.modSource.store (s.modSource);
        p->ui.scale.store (1.0f);
        if (s.model)
        {
            p->capture.loadAsync (juce::File::getCurrentWorkingDirectory().getChildFile ("tests/data/test_lstm.nam"));
            setP (*p, "cap_on", 1.0f);
        }
        const auto loop = drumLoop (0.5);
        render (*p, loop);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (KlingeEditor::designW, KlingeEditor::designH);
        if (auto* ke = dynamic_cast<KlingeEditor*> (ed.get()); ke != nullptr && s.browser) ke->showBrowser (true);
        ed->setVisible (true);   // as when a host attaches it
        for (int i = 0; i < 10; ++i)
        {
            render (*p, drumLoop (0.1));
            ph.ppq += 0.2;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = dir.getChildFile (s.file);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat png;
        const bool ok = png.writeImageToStream (img, os);
        check (ok, juce::String ("wrote ") + s.file);
        ed.reset();
        p->setPlayHead (nullptr);
    }
}

// Measures the raw output level of the circuit models and prints the compensation tables
// for Source/dsp/CompTables.h (run with the tables in that file set to {}).
static void generateCompTables()
{
    const double fsOS = 48000.0 * 4;
    const char* names[] = { "triode", "diode", "hysteresis", "iron", "stack" };
    const Algo types[] = { Algo::Triode, Algo::Diode, Algo::Hysteresis, Algo::Iron, Algo::Stack };
    for (int t = 0; t < 5; ++t)
    {
        std::printf ("inline constexpr Table %s {{\n", names[t]);
        for (int d = 0; d < 15; ++d)
        {
            std::printf ("    {{ ");
            for (int sIdx = 0; sIdx < 5; ++sIdx)
            {
                BandProcessor bp;
                bp.prepare (fsOS, 4);
                BandParams p;
                p.type = types[t];
                p.driveDb = -24.0f + 6.0f * (float) d;
                p.shape = 0.25f * (float) sIdx;
                p.mix = 1.0f;
                HarmonicRecipe r;
                bp.setParams (p, 0.0f, 0.0f, r, 0.0f, 0, 0);
                const int N = (int) (0.6 * fsOS);
                double acc = 0;
                for (int i = 0; i < N; ++i)
                {
                    bp.tick();
                    const float x = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 200.0 * i / fsOS);
                    const float y = bp.processSample (x, 0);
                    if (i > N / 2) acc += (double) y * y;
                }
                const double rms = std::sqrt (acc / (N - N / 2 - 1));
                const double comp = juce::jlimit (0.0005, 16.0, (0.25 / std::sqrt (2.0)) / std::max (rms, 1.0e-9));
                std::printf ("%.5ff%s", comp, sIdx < 4 ? ", " : "");
            }
            std::printf (" }}%s\n", d < 14 ? "," : "");
        }
        std::printf ("}};\n");
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String only = argc > 1 ? juce::String (argv[1]) : juce::String();
    auto want = [&] (const char* n) { return only.isEmpty() || only == n; };

    if (only == "gen-comp") { generateCompTables(); return 0; }
    if (only == "gen-golden") { generateGolden(); return 0; }
    if (only == "render-file" && argc > 3)
    {
        // render a WAV through KLINGE: KlingeTests render-file in.wav out.wav [preset name] [id=value ...]
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])));
        if (r == nullptr) { std::printf ("cannot read input\n"); return 1; }
        juce::AudioBuffer<float> buf (2, (int) r->lengthInSamples);
        r->read (&buf, 0, buf.getNumSamples(), 0, true, true);
        auto p = makeProc (512, r->sampleRate);
        for (int a = 4; a < argc; ++a)
        {
            const juce::String arg (argv[a]);
            if (arg.contains ("=")) setP (*p, arg.upToFirstOccurrenceOf ("=", false, false), arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
            else if (! p->loadPresetByName (arg)) std::printf ("unknown preset %s\n", argv[a]);
        }
        juce::MidiBuffer midi;
        for (int st = 0; st < buf.getNumSamples(); st += 512)
        {
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, st, std::min (512, buf.getNumSamples() - st));
            p->processBlock (view, midi);
        }
        auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]);
        outFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os = outFile.createOutputStream();
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (r->sampleRate).withNumChannels (2)
                                              .withBitsPerSample (32)
                                              .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        if (w != nullptr) w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        std::printf ("rendered %s (latency %d)\n", argv[3], p->expectedLatency());
        return 0;
    }
    if (only == "perf")
    {
        // fixed workload for profiling: multiband 8x, Amp in all bands
        const auto in = pinkNoise (-18.0f, 10.0);
        auto p = makeProc();
        setP (*p, "routing", 1); setP (*p, "oversampling", 3);
        for (int b = 1; b <= 3; ++b) setP (*p, "b" + juce::String (b) + "_type", 4);
        setP (*p, "punch", 80); setP (*p, "tone_lock", 50); setP (*p, "dyn_drive", 30);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        render (*p, in);
        std::printf ("perf workload: %.1f%% of one core\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 100.0);
        return 0;
    }
    if (want ("algo"))     testAlgorithms();
    if (want ("alias"))    testAliasing();
    if (want ("align"))    testParallelAlignment();
    if (want ("punch"))    testPunch();
    if (want ("faithful")) testFaithful();
    if (want ("autogain")) testAutoGain();
    if (want ("harm"))     testHarmonicDesigner();
    if (want ("detail"))   testDetail();
    if (want ("fuzz"))     testFuzzing();
    if (want ("state"))    { testState(); testParameterText(); }
    if (want ("presets"))  testPresets();
    if (want ("rates"))    testSampleRates();
    if (want ("circuits")) testCircuits();
    if (want ("mod"))      testModulation();
    if (want ("features")) testFeatures();
    if (want ("session"))  testSession();
    if (want ("clicks"))   testClicks();
    if (want ("precision")) testHighRatePrecision();
    if (want ("bypass"))   testBypass();
    if (want ("threads"))  testConcurrency();
    if (want ("threads-ui")) testConcurrencyWithEditor();
    if (want ("ui-logic")) testEditorLogic();
    if (want ("golden"))   testGolden();
    if (want ("capture"))  testCaptureModels();
    if (want ("cpu"))      testCpu();
    if (only == "ui")      snapshots (juce::File (argc > 2 ? argv[2] : ".").getFullPathName());   // only on request

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
