#pragma once

#include "Widgets.h"

namespace klinge::ui
{
// Pulls input and output samples from the engine and keeps the latest window of each
class AnalyserTap
{
public:
    explicit AnalyserTap (KlingeProcessor& p) : proc (p) {}
    void setActive (bool a);
    void pull();
    static constexpr int size = 8192;
    std::array<float, size> in {}, out {};
    int pos = 0;                       // next write index (oldest sample)
    double sampleRate() const { return proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0; }
private:
    KlingeProcessor& proc;
    std::array<float, 4096> tmpIn {}, tmpOut {};
};

class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (AnalyserTap& t);
    void update();
    void paint (juce::Graphics&) override;
private:
    AnalyserTap& tap;
    static constexpr int order = 12, N = 1 << order;
    juce::dsp::FFT fft { order };
    std::array<float, N / 2> magIn {}, magOut {};
    std::vector<float> work;
};

class ScopeView : public juce::Component
{
public:
    explicit ScopeView (AnalyserTap& t) : tap (t) {}
    void update() { repaint(); }
    void paint (juce::Graphics&) override;
private:
    AnalyserTap& tap;
};

class AnalyseTab : public juce::Component
{
public:
    explicit AnalyseTab (KlingeProcessor& p);
    ~AnalyseTab() override;
    void refresh();
    void visibilityChanged() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    KlingeProcessor& proc;
    AnalyserTap tap { proc };
    SpectrumView spectrum { tap };
    ScopeView scope { tap };
    std::array<float, numBands> bandLevel {};
    juce::Rectangle<int> bandArea;
    bool active = false;
};

} // namespace klinge::ui
