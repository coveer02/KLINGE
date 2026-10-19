// KlingeProfiler: measures how a distortion (plugin or hardware) behaves, so KLINGE models can be
// tuned against it.
//
//   KlingeProfiler make-signal  KLINGE_Profiling_Signal.wav
//   KlingeProfiler analyze      rendered.wav  [out-folder]
//   KlingeProfiler compare      reference.wav  candidate.wav  [out-folder]
//
// The rendered file is the test signal sent through the device under test (same length or longer,
// any latency, mono or stereo). The analyser regenerates the test signal internally.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
constexpr double fs = 48000.0;
constexpr double pi = 3.141592653589793;

// ---------------------------------------------------------------------------
// Test signal layout
// ---------------------------------------------------------------------------
struct Section { juce::String name; int start = 0, length = 0; };

struct Layout
{
    std::vector<Section> sections;
    int total = 0;
    static constexpr double sweepSecs = 4.0, f1 = 20.0, f2 = 20000.0;
    static constexpr double sweepLevels[] = { -36.0, -24.0, -18.0, -12.0, -6.0, 0.0 };
};

double sweepSample (int n, double amp)
{
    const double T = Layout::sweepSecs, L = T / std::log (Layout::f2 / Layout::f1);
    const double t = n / fs;
    const double fadeIn = std::min (1.0, t / 0.02), fadeOut = std::min (1.0, (T - t) / 0.02);
    return amp * std::sin (2 * pi * Layout::f1 * L * (std::exp (t / L) - 1.0)) * std::max (0.0, std::min (fadeIn, fadeOut));
}

std::vector<float> makeSignal (Layout& lay)
{
    std::vector<float> s;
    auto gap = [&s] (double secs) { s.insert (s.end(), (size_t) (secs * fs), 0.0f); };
    auto begin = [&s, &lay] (const juce::String& name) { lay.sections.push_back ({ name, (int) s.size(), 0 }); };
    auto end = [&s, &lay] { lay.sections.back().length = (int) s.size() - lay.sections.back().start; };

    gap (0.5);
    for (double lv : Layout::sweepLevels)
    {
        begin ("sweep " + juce::String ((int) lv) + " dBFS");
        const int N = (int) (Layout::sweepSecs * fs);
        const double a = std::pow (10.0, lv / 20.0);
        for (int n = 0; n < N; ++n) s.push_back ((float) sweepSample (n, a));
        end();
        gap (1.0);
    }
    begin ("level ramp 100 Hz");
    {
        const int N = (int) (8.0 * fs);
        for (int n = 0; n < N; ++n)
        {
            const double db = -48.0 + 48.0 * n / N;
            s.push_back ((float) (std::pow (10.0, db / 20.0) * std::sin (2 * pi * 100.0 * n / fs)));
        }
    }
    end(); gap (1.0);
    begin ("level ramp 1 kHz");
    {
        const int N = (int) (8.0 * fs);
        for (int n = 0; n < N; ++n)
        {
            const double db = -48.0 + 48.0 * n / N;
            s.push_back ((float) (std::pow (10.0, db / 20.0) * std::sin (2 * pi * 1000.0 * n / fs)));
        }
    }
    end(); gap (1.0);
    begin ("SMPTE IMD 60 Hz + 7 kHz");
    for (int n = 0; n < (int) (3.0 * fs); ++n)
        s.push_back ((float) (0.8 * 0.25 * std::sin (2 * pi * 60 * n / fs) + 0.2 * 0.25 * std::sin (2 * pi * 7000 * n / fs)));
    end(); gap (1.0);
    begin ("CCIF IMD 19 + 20 kHz");
    for (int n = 0; n < (int) (3.0 * fs); ++n)
        s.push_back ((float) (0.125 * std::sin (2 * pi * 19000 * n / fs) + 0.125 * std::sin (2 * pi * 20000 * n / fs)));
    end(); gap (1.0);
    for (double burstMs : { 50.0, 400.0 })
    {
        begin ("burst " + juce::String ((int) burstMs) + " ms");
        const int pre = (int) (1.0 * fs), burst = (int) (burstMs * 0.001 * fs), post = (int) (2.0 * fs);
        for (int n = 0; n < pre + burst + post; ++n)
        {
            const double a = (n >= pre && n < pre + burst) ? 1.0 : 0.0316;
            s.push_back ((float) (a * std::sin (2 * pi * 220.0 * n / fs)));
        }
        end(); gap (1.0);
    }
    lay.total = (int) s.size();
    return s;
}

// ---------------------------------------------------------------------------
// I/O
// ---------------------------------------------------------------------------
bool writeWav (const juce::File& f, const std::vector<float>& x)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return false;
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (fs).withNumChannels (2).withBitsPerSample (32)
                                          .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (w == nullptr) return false;
    juce::AudioBuffer<float> b (2, (int) x.size());
    b.copyFrom (0, 0, x.data(), (int) x.size());
    b.copyFrom (1, 0, x.data(), (int) x.size());
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

std::vector<float> readMono (const juce::File& f, double& rate)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return {};
    rate = r->sampleRate;
    juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, b.getNumSamples(), 0, true, true);
    std::vector<float> m ((size_t) b.getNumSamples());
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        float v = 0;
        for (int c = 0; c < b.getNumChannels(); ++c) v += b.getSample (c, i);
        m[(size_t) i] = v / (float) b.getNumChannels();
    }
    return m;
}

// ---------------------------------------------------------------------------
// Analysis helpers
// ---------------------------------------------------------------------------
int nextPow2 (int n) { int p = 1; while (p < n) p <<= 1; return p; }

// Linear cross-correlation via FFT, returns lag of the maximum (|corr|)
int findLag (const std::vector<float>& ref, const std::vector<float>& sig, int refStart, int len, int maxLag)
{
    const int N = nextPow2 (len + maxLag) * 2;
    const int order = (int) std::log2 (N);
    juce::dsp::FFT fft (order);
    std::vector<std::complex<float>> a ((size_t) N), b ((size_t) N), ca ((size_t) N), cb ((size_t) N);
    for (int i = 0; i < len; ++i) a[(size_t) i] = ref[(size_t) (refStart + i)];
    for (int i = 0; i < len + maxLag && refStart + i < (int) sig.size(); ++i) b[(size_t) i] = sig[(size_t) (refStart + i)];
    fft.perform (a.data(), ca.data(), false);
    fft.perform (b.data(), cb.data(), false);
    for (int i = 0; i < N; ++i) ca[(size_t) i] = std::conj (ca[(size_t) i]) * cb[(size_t) i];
    fft.perform (ca.data(), a.data(), true);
    int best = 0; float bv = -1;
    for (int k = 0; k < maxLag; ++k) if (std::abs (a[(size_t) k].real()) > bv) { bv = std::abs (a[(size_t) k].real()); best = k; }
    return best;
}

// Farina deconvolution of one sweep section: returns the impulse response (linear part at index N)
std::vector<float> deconvolve (const std::vector<float>& y, int start, int lag, double amp)
{
    const int Ns = (int) (Layout::sweepSecs * fs);
    const int N = nextPow2 (Ns) * 2;
    const int order = (int) std::log2 (N);
    juce::dsp::FFT fft (order);
    const double T = Layout::sweepSecs, L = T / std::log (Layout::f2 / Layout::f1);
    std::vector<std::complex<float>> inv ((size_t) N), out ((size_t) N), Fi ((size_t) N), Fo ((size_t) N);
    // inverse filter: time-reversed sweep, amplitude rising with the instantaneous frequency (+6 dB/octave),
    // which flattens the pink spectrum of the exponential sweep
    for (int n = 0; n < Ns; ++n)
    {
        const double t = n / fs;
        inv[(size_t) (Ns - 1 - n)] = (float) (sweepSample (n, 1.0) * std::exp ((t - T) / L));
    }
    for (int n = 0; n < Ns + (int) (0.5 * fs) && start + lag + n < (int) y.size(); ++n)
        if (n < N) out[(size_t) n] = y[(size_t) (start + lag + n)];
    fft.perform (inv.data(), Fi.data(), false);
    fft.perform (out.data(), Fo.data(), false);
    // normalise so that a unity system gives a unit impulse
    std::vector<std::complex<float>> ref ((size_t) N), Fr ((size_t) N);
    for (int n = 0; n < Ns; ++n) ref[(size_t) n] = (float) sweepSample (n, 1.0);
    fft.perform (ref.data(), Fr.data(), false);
    double peak = 0;
    {
        std::vector<std::complex<float>> tmp ((size_t) N), r ((size_t) N);
        for (int i = 0; i < N; ++i) tmp[(size_t) i] = Fr[(size_t) i] * Fi[(size_t) i];
        fft.perform (tmp.data(), r.data(), true);
        for (auto& v : r) peak = std::max (peak, (double) std::abs (v.real()));
    }
    for (int i = 0; i < N; ++i) Fo[(size_t) i] *= Fi[(size_t) i];
    fft.perform (Fo.data(), out.data(), true);
    std::vector<float> ir ((size_t) N);
    for (int i = 0; i < N; ++i) ir[(size_t) i] = (float) (out[(size_t) i].real() / (peak * amp));
    return ir;
}

// magnitude response (dB) of an IR segment at given frequencies
std::vector<float> responseAt (const std::vector<float>& ir, int centre, int pre, int len, const std::vector<double>& freqs)
{
    std::vector<float> r;
    for (double f : freqs)
    {
        double re = 0, im = 0;
        for (int i = -pre; i < len; ++i)
        {
            const int k = centre + i;
            if (k < 0 || k >= (int) ir.size()) continue;
            double w = 1.0;
            if (i < 0) w = 0.5 - 0.5 * std::cos (pi * (i + pre) / pre);
            else if (i > len / 2) w = 0.5 + 0.5 * std::cos (pi * (i - len / 2) / (len / 2));
            re += w * ir[(size_t) k] * std::cos (2 * pi * f * i / fs);
            im -= w * ir[(size_t) k] * std::sin (2 * pi * f * i / fs);
        }
        r.push_back ((float) (10.0 * std::log10 (re * re + im * im + 1e-24)));
    }
    return r;
}

double goertzel (const std::vector<float>& x, int start, int n, double f)
{
    double s1 = 0, s2 = 0;
    const double c = 2.0 * std::cos (2 * pi * f / fs);
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2 * pi * i / (n - 1));
        const double s0 = x[(size_t) (start + i)] * w + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (n * 0.25);
}

double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-12)); }

// ---------------------------------------------------------------------------
// Profile
// ---------------------------------------------------------------------------
struct Profile
{
    int latency = 0;
    double gainDb = 0;
    std::vector<double> freqs;
    std::vector<std::vector<float>> h1;                 // per sweep level, dB re. input
    std::vector<std::array<std::vector<float>, 4>> hn;  // H2..H5 per level, dB re. input
    std::vector<std::array<double, 5>> ramp100, ramp1k; // per input level step: in dB, out dB, H2, H3, H5 (dB re fundamental)
    double smpte = 0, ccif = 0;
    struct Burst { double before2, after2, recoveryMs, levelBefore, levelAfter; };
    std::vector<Burst> bursts;
};

Profile analyse (const std::vector<float>& y)
{
    Layout lay;
    const auto x = makeSignal (lay);
    Profile p;
    const auto& first = lay.sections[0];
    p.latency = findLag (x, y, first.start, first.length, (int) (0.25 * fs));

    for (double f = 20.0; f <= 20000.0; f *= std::pow (2.0, 1.0 / 6.0)) p.freqs.push_back (f);
    const double T = Layout::sweepSecs, Lr = std::log (Layout::f2 / Layout::f1);
    for (size_t s = 0; s < 6; ++s)
    {
        const auto& sec = lay.sections[s];
        const double amp = std::pow (10.0, Layout::sweepLevels[s] / 20.0);
        const auto ir = deconvolve (y, sec.start, p.latency, amp);
        const int Ns = (int) (T * fs);
        const int centre = Ns - 1;                            // linear IR position after correlation with the reversed sweep
        p.h1.push_back (responseAt (ir, centre, 64, 8192, p.freqs));
        std::array<std::vector<float>, 4> hh;
        for (int n = 2; n <= 5; ++n)
        {
            const int dt = (int) std::lround (T * std::log ((double) n) / Lr * fs);
            // harmonic n's response is indexed by the excitation frequency, so read it at f / 1 (frequency axis = input frequency)
            std::vector<double> fn;
            for (double f : p.freqs) fn.push_back (f * n);
            auto r = responseAt (ir, centre - dt, 64, std::min (8192, dt - (int) std::lround (T * std::log ((double) n - 1) / Lr * fs) - 128), fn);
            hh[(size_t) (n - 2)] = r;
        }
        p.hn.push_back (hh);
    }
    {
        // reference level of the deconvolved responses (the quiet sweep at 1 kHz); only relative values are reported
        size_t k = 0;
        while (k + 1 < p.freqs.size() && p.freqs[k] < 1000.0) ++k;
        p.gainDb = p.h1[0][k];
    }

    auto ramp = [&] (const Section& sec, double f, std::vector<std::array<double, 5>>& out)
    {
        const int win = (int) (fs / f * 20);
        for (int st = 0; st + win < sec.length; st += (int) (0.25 * fs))
        {
            const int a = sec.start + st, b = a + p.latency;
            if (b + win >= (int) y.size()) break;
            const double inF = goertzel (x, a, win, f);
            const double outF = goertzel (y, b, win, f);
            out.push_back ({ db (inF), db (outF), db (goertzel (y, b, win, 2 * f) / outF), db (goertzel (y, b, win, 3 * f) / outF),
                             db (goertzel (y, b, win, 5 * f) / outF) });
        }
    };
    ramp (lay.sections[6], 100.0, p.ramp100);
    ramp (lay.sections[7], 1000.0, p.ramp1k);

    {
        // SMPTE: sidebands of 7 kHz at +-60 / +-120 Hz relative to the 7 kHz carrier
        const auto& sec = lay.sections[8];
        const int a = sec.start + p.latency + (int) (0.5 * fs), n = (int) (2.0 * fs);
        const double carrier = goertzel (y, a, n, 7000.0);
        double side = 0;
        for (double d : { 60.0, 120.0 }) for (double sgn : { -1.0, 1.0 }) side += std::pow (goertzel (y, a, n, 7000.0 + sgn * d), 2);
        p.smpte = 100.0 * std::sqrt (side) / std::max (carrier, 1e-12);
        const auto& sec2 = lay.sections[9];
        const int a2 = sec2.start + p.latency + (int) (0.5 * fs);
        const double tones = std::hypot (goertzel (y, a2, n, 19000.0), goertzel (y, a2, n, 20000.0));
        p.ccif = 100.0 * goertzel (y, a2, n, 1000.0) / std::max (tones, 1e-12);
    }

    for (size_t s = 10; s < 12; ++s)
    {
        const auto& sec = lay.sections[s];
        const int pre = (int) fs;
        const int burst = s == 10 ? (int) (0.05 * fs) : (int) (0.4 * fs);
        const int win = (int) (fs / 220.0 * 8);
        auto h2At = [&] (int pos) { const int b = sec.start + p.latency + pos; return db (goertzel (y, b, win, 440.0) / goertzel (y, b, win, 220.0)); };
        auto lvAt = [&] (int pos) { return db (goertzel (y, sec.start + p.latency + pos, win, 220.0)); };
        Profile::Burst bu;
        bu.before2 = h2At (pre - win - 100);
        bu.levelBefore = lvAt (pre - win - 100);
        const int afterPos = pre + burst + 200;
        bu.after2 = h2At (afterPos);
        bu.levelAfter = lvAt (afterPos);
        bu.recoveryMs = -1;
        auto same2 = [&bu] (double h) { return (h < -80.0 && bu.before2 < -80.0) || std::abs (h - bu.before2) < 1.0; };
        for (int pos = afterPos; pos + win < sec.length - 100; pos += win / 4)
            if (same2 (h2At (pos)) && std::abs (lvAt (pos) - bu.levelBefore) < 0.5) { bu.recoveryMs = (pos - pre - burst) / fs * 1000.0; break; }
        p.bursts.push_back (bu);
    }
    return p;
}

// ---------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------
juce::String fmt (double v, int d = 1) { return juce::String (v, d); }

void plot (const juce::File& f, const juce::String& title, const std::vector<double>& xs, const std::vector<std::pair<juce::String, std::vector<float>>>& series,
           float yMin, float yMax, bool logX)
{
    juce::Image img (juce::Image::RGB, 900, 420, true);
    juce::Graphics g (img);
    g.fillAll (juce::Colour (0xff1a1d22));
    const juce::Rectangle<float> area (60, 40, 820, 330);
    g.setColour (juce::Colour (0xff3c434d));
    g.drawRect (area);
    auto mx = [&] (double x) { return area.getX() + (float) (logX ? std::log (x / xs.front()) / std::log (xs.back() / xs.front()) : (x - xs.front()) / (xs.back() - xs.front())) * area.getWidth(); };
    auto my = [&] (float v) { return area.getBottom() - (std::min (std::max (v, yMin), yMax) - yMin) / (yMax - yMin) * area.getHeight(); };
    g.setFont (13.0f);
    g.setColour (juce::Colour (0xffdde2e8));
    g.drawText (title, 60, 8, 820, 24, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff8d97a3));
    for (float v = yMin; v <= yMax + 0.01f; v += (yMax - yMin) / 6)
    {
        g.drawText (fmt (v, 0), 5, (int) my (v) - 8, 50, 16, juce::Justification::centredRight);
        g.setColour (juce::Colour (0x223c434d).withAlpha (0.6f));
        g.drawHorizontalLine ((int) my (v), area.getX(), area.getRight());
        g.setColour (juce::Colour (0xff8d97a3));
    }
    if (logX)
        for (double t : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
            if (t >= xs.front() && t <= xs.back())
                g.drawText (t >= 1000 ? fmt (t / 1000, 0) + "k" : fmt (t, 0), (int) mx (t) - 20, (int) area.getBottom() + 4, 40, 16, juce::Justification::centred);
    const juce::Colour cols[] = { juce::Colour (0xffe9d59a), juce::Colour (0xffd9a84e), juce::Colour (0xffb8743a),
                                  juce::Colour (0xff8a4e86), juce::Colour (0xff4c6fb3), juce::Colour (0xff6fb36f) };
    int li = 0;
    for (auto& [name, ys] : series)
    {
        juce::Path pth;
        for (size_t i = 0; i < ys.size() && i < xs.size(); ++i)
        {
            const juce::Point<float> pt (mx (xs[i]), my (ys[i]));
            if (i == 0) pth.startNewSubPath (pt); else pth.lineTo (pt);
        }
        const auto c = cols[li % 6];
        g.setColour (c);
        g.strokePath (pth, juce::PathStrokeType (1.8f));
        g.drawText (name, (int) area.getX() + 10 + 140 * li, (int) area.getBottom() + 22, 140, 16, juce::Justification::centredLeft);
        ++li;
    }
    juce::PNGImageFormat png;
    f.deleteFile();
    juce::FileOutputStream os (f);
    png.writeImageToStream (img, os);
}

juce::String report (const Profile& p, const juce::String& name, const juce::File& dir)
{
    juce::String r;
    r << "# Profile: " << name << "\n\n";
    r << "Latency " << p.latency << " samples";
    if (! p.ramp1k.empty()) r << ", small-signal gain at 1 kHz " << fmt (p.ramp1k.front()[1] - p.ramp1k.front()[0]) << " dB";
    r << "\n\n";
    r << "## Linear response (-36 dBFS sweep), dB re. 1 kHz\n\n| Hz | ";
    const double show[] = { 30, 60, 100, 200, 500, 1000, 2000, 4000, 8000, 12000, 16000 };
    auto idx = [&p] (double f) { size_t k = 0; while (k + 1 < p.freqs.size() && p.freqs[k] < f) ++k; return k; };
    for (double f : show) r << (f >= 1000 ? fmt (f / 1000, 0) + "k" : fmt (f, 0)) << " | ";
    r << "\n|---|"; for (size_t i = 0; i < std::size (show); ++i) r << "---|"; r << "\n|  | ";
    for (double f : show) r << fmt (p.h1[0][idx (f)] - p.gainDb) << " | ";
    {
        const size_t k1 = idx (1000.0), k100 = idx (100.0);
        r << "\n\n## Swept harmonics at -12 dBFS (dB re. fundamental)\n\n| | H2 | H3 | H4 | H5 |\n|---|---|---|---|---|\n";
        for (auto [label, k] : { std::pair<const char*, size_t> { "100 Hz", k100 }, { "1 kHz", k1 } })
        {
            r << "| " << label << " | ";
            for (int n = 0; n < 4; ++n) r << fmt (p.hn[3][(size_t) n][k] - p.h1[3][k]) << " | ";
            r << "\n";
        }
    }
    r << "\n## Harmonics vs level (100 Hz tone): dB re. fundamental\n\n| in dBFS | out dB | H2 | H3 | H5 |\n|---|---|---|---|---|\n";
    for (size_t i = 0; i < p.ramp100.size(); i += 4)
        r << "| " << fmt (p.ramp100[i][0]) << " | " << fmt (p.ramp100[i][1]) << " | " << fmt (p.ramp100[i][2]) << " | " << fmt (p.ramp100[i][3]) << " | " << fmt (p.ramp100[i][4]) << " |\n";
    r << "\n## Harmonics vs level (1 kHz tone)\n\n| in dBFS | out dB | H2 | H3 | H5 |\n|---|---|---|---|---|\n";
    for (size_t i = 0; i < p.ramp1k.size(); i += 4)
        r << "| " << fmt (p.ramp1k[i][0]) << " | " << fmt (p.ramp1k[i][1]) << " | " << fmt (p.ramp1k[i][2]) << " | " << fmt (p.ramp1k[i][3]) << " | " << fmt (p.ramp1k[i][4]) << " |\n";
    r << "\n## Intermodulation\n\nSMPTE (60 Hz + 7 kHz): " << fmt (p.smpte, 2) << " %   CCIF (19 + 20 kHz, 1 kHz product): " << fmt (p.ccif, 2) << " %\n";
    r << "\n## Dynamics (quiet 220 Hz tone around a 0 dBFS burst)\n\n| burst | H2 before | H2 after | level before | level after | recovery |\n|---|---|---|---|---|---|\n";
    const char* bn[] = { "50 ms", "400 ms" };
    for (size_t i = 0; i < p.bursts.size(); ++i)
    {
        const auto& b = p.bursts[i];
        r << "| " << bn[i] << " | " << fmt (b.before2) << " | " << fmt (b.after2) << " | " << fmt (b.levelBefore) << " | " << fmt (b.levelAfter) << " | "
          << (b.recoveryMs < 0 ? juce::String ("> 2 s") : fmt (b.recoveryMs, 0) + " ms") << " |\n";
    }

    // plots
    std::vector<std::pair<juce::String, std::vector<float>>> lin, thd;
    for (size_t s = 0; s < p.h1.size(); ++s)
    {
        std::vector<float> rel;
        for (auto v : p.h1[s]) rel.push_back (v - (float) p.gainDb);
        lin.push_back ({ juce::String ((int) Layout::sweepLevels[s]) + " dBFS", rel });
    }
    plot (dir.getChildFile (name + "_response.png"), name + ": response per input level (dB re. quiet 1 kHz)", p.freqs, lin, -36, 12, true);
    for (int n = 0; n < 4; ++n)
    {
        std::vector<float> rel;
        for (size_t k = 0; k < p.freqs.size(); ++k) rel.push_back (p.hn[3][(size_t) n][k] - p.h1[3][k]);
        thd.push_back ({ "H" + juce::String (n + 2) + " at -12 dBFS", rel });
    }
    plot (dir.getChildFile (name + "_harmonics.png"), name + ": harmonics re. fundamental at -12 dBFS", p.freqs, thd, -90, 0, true);
    std::vector<double> lv; std::vector<float> h2, h3, comp;
    for (auto& a : p.ramp100) { lv.push_back (a[0]); h2.push_back ((float) a[2]); h3.push_back ((float) a[3]); comp.push_back ((float) (a[1] - a[0])); }
    if (lv.size() > 2)
        plot (dir.getChildFile (name + "_level.png"), name + ": 100 Hz tone, harmonics and gain vs input level", lv,
              { { "H2 dB", h2 }, { "H3 dB", h3 }, { "gain dB", comp } }, -80, 20, false);
    return r;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String cmd = argc > 1 ? argv[1] : "";
    if (cmd == "make-signal" && argc > 2)
    {
        Layout lay;
        const auto s = makeSignal (lay);
        const juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
        if (! writeWav (f, s)) { std::printf ("could not write %s\n", argv[2]); return 1; }
        std::printf ("wrote %s: %.1f s at 48 kHz, 32-bit float stereo\n", f.getFullPathName().toRawUTF8(), s.size() / fs);
        for (auto& sec : lay.sections) std::printf ("  %6.2f s  %s\n", sec.start / fs, sec.name.toRawUTF8());
        return 0;
    }
    if ((cmd == "analyze" && argc > 2) || (cmd == "compare" && argc > 3))
    {
        const int nFiles = cmd == "compare" ? 2 : 1;
        const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 + nFiles ? argv[2 + nFiles] : "profile");
        dir.createDirectory();
        std::vector<Profile> ps;
        juce::String all;
        for (int i = 0; i < nFiles; ++i)
        {
            const juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2 + i]);
            double rate = 0;
            const auto y = readMono (f, rate);
            if (y.empty()) { std::printf ("cannot read %s\n", argv[2 + i]); return 1; }
            if (std::abs (rate - fs) > 1) { std::printf ("%s is at %.0f Hz; please render at 48 kHz\n", argv[2 + i], rate); return 1; }
            ps.push_back (analyse (y));
            all << report (ps.back(), f.getFileNameWithoutExtension(), dir) << "\n\n";
        }
        if (nFiles == 2)
        {
            const auto& a = ps[0]; const auto& b = ps[1];
            all << "# Differences (candidate - reference)\n\n| level | max response diff dB | H2 diff dB (1 kHz) | H3 diff dB (1 kHz) |\n|---|---|---|---|\n";
            size_t k1k = 0; while (k1k + 1 < a.freqs.size() && a.freqs[k1k] < 1000.0) ++k1k;
            for (size_t s = 0; s < 6; ++s)
            {
                float md = 0;
                for (size_t k = 0; k < a.freqs.size(); ++k)
                    if (a.freqs[k] > 30 && a.freqs[k] < 16000)
                        md = std::max (md, std::abs ((b.h1[s][k] - (float) b.gainDb) - (a.h1[s][k] - (float) a.gainDb)));
                all << "| " << (int) Layout::sweepLevels[s] << " dBFS | " << fmt (md) << " | "
                    << fmt ((b.hn[s][0][k1k] - b.h1[s][k1k]) - (a.hn[s][0][k1k] - a.h1[s][k1k])) << " | "
                    << fmt ((b.hn[s][1][k1k] - b.h1[s][k1k]) - (a.hn[s][1][k1k] - a.h1[s][k1k])) << " |\n";
            }
        }
        dir.getChildFile ("report.md").replaceWithText (all);
        std::printf ("%s\nreport and plots written to %s\n", all.toRawUTF8(), dir.getFullPathName().toRawUTF8());
        return 0;
    }
    std::printf ("usage:\n  KlingeProfiler make-signal out.wav\n  KlingeProfiler analyze rendered.wav [out-folder]\n"
                 "  KlingeProfiler compare reference.wav candidate.wav [out-folder]\n");
    return 2;
}
