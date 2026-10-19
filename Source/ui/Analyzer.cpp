#include "Analyzer.h"

namespace klinge::ui
{
void AnalyserTap::setActive (bool a)
{
    proc.getEngine().scopeEnabled.store (a);
    if (! a) { in.fill (0.0f); out.fill (0.0f); }
}

void AnalyserTap::pull()
{
    auto& e = proc.getEngine();
    for (;;)
    {
        // read equal amounts from both FIFOs so input and output stay sample-aligned
        const int n = std::min ({ e.scopeIn.ready(), e.scopeOut.ready(), (int) tmpIn.size() });
        if (n <= 0) break;
        e.scopeIn.pull (tmpIn.data(), n);
        e.scopeOut.pull (tmpOut.data(), n);
        for (int i = 0; i < n; ++i)
        {
            in[(size_t) pos] = tmpIn[(size_t) i];
            out[(size_t) pos] = tmpOut[(size_t) i];
            pos = (pos + 1) % size;
        }
    }
}

// ===========================================================================
SpectrumView::SpectrumView (AnalyserTap& t) : tap (t)
{
    work.resize ((size_t) N * 2);
    magIn.fill (-120.0f);
    magOut.fill (-120.0f);
    setHelp (*this, "Spectrum of the input (grey) and the output (gold), so you can see where the distortion adds energy.");
}

void SpectrumView::update()
{
    auto analyse = [this] (const std::array<float, AnalyserTap::size>& src, std::array<float, N / 2>& mag)
    {
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int idx = (tap.pos - N + i + AnalyserTap::size) % AnalyserTap::size;
            const float w = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) (N - 1));
            work[(size_t) i] = src[(size_t) idx] * w;
        }
        fft.performFrequencyOnlyForwardTransform (work.data());
        for (int k = 0; k < N / 2; ++k)
        {
            const float db = gainToDb (work[(size_t) k] * 4.0f / (float) N);
            mag[(size_t) k] = db > mag[(size_t) k] ? db : mag[(size_t) k] + 0.25f * (db - mag[(size_t) k]);
        }
    };
    analyse (tap.in, magIn);
    analyse (tap.out, magOut);
    repaint();
}

void SpectrumView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (b, 6.0f);
    auto a = b.reduced (40, 14).withTrimmedBottom (14).withTrimmedLeft (-8);
    const double fs = tap.sampleRate();
    auto xOf = [&] (double f) { return a.getX() + (float) (std::log (f / 20.0) / std::log (20000.0 / 20.0)) * a.getWidth(); };
    auto yOf = [&] (float db) { return a.getY() + clampf ((0.0f - db) / 96.0f, 0.0f, 1.0f) * a.getHeight(); };
    g.setFont (fonts::mono (10.5f));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (col::track.withAlpha (0.35f));
        g.drawVerticalLine ((int) xOf (f), a.getY(), a.getBottom());
        g.setColour (col::textFaint);
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) xOf (f) - 16, (int) a.getBottom() + 2, 32, 12,
                    juce::Justification::centred);
    }
    for (float db : { -24.0f, -48.0f, -72.0f })
    {
        g.setColour (col::track.withAlpha (0.35f));
        g.drawHorizontalLine ((int) yOf (db), a.getX(), a.getRight());
        g.setColour (col::textFaint);
        g.drawText (juce::String ((int) db), (int) a.getX() - 34, (int) yOf (db) - 6, 30, 12, juce::Justification::centredRight);
    }
    const double binHz = fs / N;
    // one point every 2 px, power-averaged over 1/6 octave (or interpolated where bins are wider than that)
    auto curve = [&] (const std::array<float, N / 2>& mag)
    {
        juce::Path p;
        bool started = false;
        const double fMax = std::min (20000.0, fs * 0.5 * 0.98);
        for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
        {
            const double f = 20.0 * std::pow (1000.0, (double) ((x - a.getX()) / a.getWidth()));
            if (f > fMax) break;
            const double k0 = f * std::pow (2.0, -1.0 / 12.0) / binHz, k1 = f * std::pow (2.0, 1.0 / 12.0) / binHz;
            float db;
            if (k1 - k0 < 2.0)
            {
                const double k = juce::jlimit (1.0, (double) (N / 2 - 2), f / binHz);
                const int i = (int) k;
                const float t = (float) (k - i);
                db = mag[(size_t) i] + t * (mag[(size_t) i + 1] - mag[(size_t) i]);
            }
            else
            {
                double pw = 0.0;
                int n = 0;
                for (int i = std::max (1, (int) std::ceil (k0)); i <= std::min (N / 2 - 1, (int) std::floor (k1)); ++i, ++n)
                    pw += std::pow (10.0, mag[(size_t) i] / 10.0);
                db = n > 0 ? (float) (10.0 * std::log10 (pw / n + 1.0e-30)) : -120.0f;
            }
            const juce::Point<float> pt (x, yOf (db));
            if (! started) { p.startNewSubPath (pt); started = true; } else p.lineTo (pt);
        }
        return p;
    };
    g.setColour (col::textFaint);
    g.strokePath (curve (magIn), juce::PathStrokeType (1.2f));
    g.setColour (col::gold);
    g.strokePath (curve (magOut), juce::PathStrokeType (1.6f));
}

// ===========================================================================
void ScopeView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (b, 6.0f);
    auto a = b.reduced (12, 10);
    g.setColour (col::track.withAlpha (0.5f));
    g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());

    // trigger on a rising zero crossing of the input, about 25 ms back
    const int span = 1200;
    int start = (tap.pos - span - 2400 + AnalyserTap::size) % AnalyserTap::size;
    for (int i = 0; i < 1200; ++i)
    {
        const int i0 = (start + i) % AnalyserTap::size, i1 = (start + i + 1) % AnalyserTap::size;
        if (tap.in[(size_t) i0] <= 0.0f && tap.in[(size_t) i1] > 0.0f) { start = i1; break; }
    }
    auto wave = [&] (const std::array<float, AnalyserTap::size>& src)
    {
        juce::Path p;
        for (int i = 0; i < span; ++i)
        {
            const float v = src[(size_t) ((start + i) % AnalyserTap::size)];
            const juce::Point<float> pt (a.getX() + (float) i / (float) (span - 1) * a.getWidth(), a.getCentreY() - clampf (v, -1.2f, 1.2f) / 1.2f * a.getHeight() * 0.5f);
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        return p;
    };
    g.setColour (col::textFaint);
    g.strokePath (wave (tap.in), juce::PathStrokeType (1.2f));
    g.setColour (col::gold);
    g.strokePath (wave (tap.out), juce::PathStrokeType (1.6f));
    g.setFont (fonts::ui (11.5f, 0));
    g.setColour (col::textFaint);
    g.drawText ("Input grey, output gold. 25 ms window.", a.removeFromBottom (14), juce::Justification::centredRight);
}

// ===========================================================================
AnalyseTab::AnalyseTab (KlingeProcessor& p) : proc (p)
{
    addAndMakeVisible (spectrum);
    addAndMakeVisible (scope);
    setHelp (scope, "Waveform of the input (grey) and the output (gold), aligned to the input.");
}

AnalyseTab::~AnalyseTab() { tap.setActive (false); }

void AnalyseTab::visibilityChanged()
{
    active = visibleInTree (*this);
    tap.setActive (active);
}

void AnalyseTab::refresh()
{
    // the editor may be put on screen after this tab became visible, so activation is also checked here
    const bool showing = visibleInTree (*this);
    if (showing != active) { active = showing; tap.setActive (active); }
    if (! showing) return;
    tap.pull();
    spectrum.update();
    scope.update();
    auto& m = proc.getMeters();
    for (int b = 0; b < numBands; ++b)
    {
        const float d = gainToDb (m.bandPeak[(size_t) b].exchange (0.0f));
        bandLevel[(size_t) b] = d > bandLevel[(size_t) b] ? d : std::max (d, bandLevel[(size_t) b] - 1.0f);
    }
    repaint (bandArea);
}

void AnalyseTab::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    spectrum.setBounds (r.removeFromTop (300));
    r.removeFromTop (10);
    scope.setBounds (r.removeFromTop (180));
    r.removeFromTop (10);
    bandArea = r;
}

void AnalyseTab::paint (juce::Graphics& g)
{
    auto r = bandArea.toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (r, 6.0f);
    auto a = r.reduced (12, 8);
    g.setFont (fonts::ui (12.5f, 1));
    g.setColour (col::textDim);
    g.drawText ("Band output peaks", a.removeFromTop (16), juce::Justification::centredLeft);
    const int routing = (int) proc.apvts.getRawParameterValue (pid::routing)->load();
    const int nb = routing == 0 ? 1 : (routing == 2 ? 2 : 2 + (int) proc.apvts.getRawParameterValue (pid::numBands)->load());
    const float w = a.getWidth() / (float) numBands;
    for (int b = 0; b < numBands; ++b)
    {
        auto cell = juce::Rectangle<float> (a.getX() + w * (float) b, a.getY() + 4, w - 10, a.getHeight() - 8);
        auto bar = cell.removeFromBottom (8);
        g.setColour (col::panelEdge);
        g.fillRoundedRectangle (bar, 2.0f);
        if (b < nb)
        {
            const float t = clampf ((bandLevel[(size_t) b] + 48.0f) / 54.0f, 0.0f, 1.0f);
            g.setColour (temper ((float) b / (float) (numBands - 1)));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * t), 2.0f);
        }
        g.setColour (b < nb ? col::text : col::textFaint);
        g.setFont (fonts::ui (12.5f, 1));
        const juce::String names[3][5] = { { "Main", "", "", "", "" }, { "Low", "2", "3", "4", "High" }, { "Mid", "Side", "", "", "" } };
        juce::String name = routing == 1 ? (b == 0 ? "Low" : (b == nb - 1 ? "High" : "Band " + juce::String (b + 1))) : names[routing][b];
        if (b >= nb) name = "off";
        g.drawText (name, cell, juce::Justification::centredLeft);
        g.setFont (fonts::mono (11.0f));
        if (b < nb) g.drawText (bandLevel[(size_t) b] < -99 ? juce::String ("-inf") : juce::String (bandLevel[(size_t) b], 1), cell, juce::Justification::centredRight);
    }
}

} // namespace klinge::ui
