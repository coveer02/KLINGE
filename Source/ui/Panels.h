#pragma once

#include "Widgets.h"
#include "ModUI.h"
#include "Analyzer.h"

namespace klinge::ui
{
// Band naming shared by the band columns, the crossover strip and the displays
int activeBandCount (int routing, int multibandCount);
juce::String bandName (int routing, int multibandCount, int band);
int currentRouting (KlingeProcessor&);
int currentBandCount (KlingeProcessor&);       // bands that are actually processing

// Calls refreshMod() on every knob below root that is on screen
void collectKnobs (juce::Component& root, std::vector<Knob*>& out);

// ---------------------------------------------------------------------------
// Algorithm picker as a combo box, listed by character group
// ---------------------------------------------------------------------------
class AlgoBox : public juce::ComboBox
{
public:
    AlgoBox (KlingeProcessor&, int band);
private:
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// Algorithm list for the Basic page (band 1)
class AlgoList : public juce::Component
{
public:
    explicit AlgoList (KlingeProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
private:
    struct Row { int algo = -1; juce::String heading; juce::Rectangle<int> bounds; };
    std::vector<Row> layoutRows() const;
    int algoAt (juce::Point<int>) const;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int selected = 0, hover = -1;
};

// ---------------------------------------------------------------------------
// Crossover strip with up to four draggable split points
// ---------------------------------------------------------------------------
class CrossoverStrip : public juce::Component
{
public:
    explicit CrossoverStrip (KlingeProcessor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    std::function<void (int)> onSelectBand;
    int selectedBand = 0;
private:
    float xForFreq (float f) const;
    float freqForX (float x) const;
    int splitAt (float x) const;
    KlingeProcessor& proc;
    std::array<std::unique_ptr<juce::ParameterAttachment>, 4> att;
    std::array<float, 4> hz { 200, 3000, 6000, 10000 };
    int routing = 0, count = 3, dragging = -1, hover = -1;
};

// ---------------------------------------------------------------------------
// Bands tab
// ---------------------------------------------------------------------------
class BandColumn : public juce::Component
{
public:
    BandColumn (KlingeProcessor&, int band, std::function<void (int)> onSelect);
    void update (const juce::String& title, bool active, bool selected, float peakDb);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    KlingeProcessor& proc;
    int band;
    std::function<void (int)> onSelect;
    juce::String title;
    bool active = true, selected = false;
    float peakDb = -100.0f;
    Chip on, solo, mute;
    AlgoBox type;
    Knob drive, shape, mix, level;
    int lastAlgo = -1;
};

class BandDetail : public juce::Component
{
public:
    BandDetail (KlingeProcessor&, int band);
    void update (const juce::String& bandTitle);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    KlingeProcessor& proc;
    int band;
    juce::String title;
    int lastAlgo = -1;
    Knob shape, bias, harm, sag, dyn, thresh;
    ChoiceSegments stereo, dynPos;
};

class BandsTab : public juce::Component
{
public:
    explicit BandsTab (KlingeProcessor&);
    void refresh();
    void selectBand (int b);
    int getSelectedBand() const { return selected; }
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void (int)> onBandSelected;
private:
    KlingeProcessor& proc;
    ChoiceSegments routing, count;
    CrossoverStrip strip;
    std::array<std::unique_ptr<BandColumn>, numBands> columns;
    std::array<std::unique_ptr<BandDetail>, numBands> details;
    std::array<float, numBands> peakDb {};
    int selected = 0;
};

// ---------------------------------------------------------------------------
class HarmonicsTab : public juce::Component
{
public:
    explicit HarmonicsTab (KlingeProcessor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    KlingeProcessor& proc;
    HarmonicFaders faders;
    Knob lock;
    std::array<std::unique_ptr<Knob>, numBands> bandHarm;
    int lastActive = -1;
};

class CaptureTab : public juce::Component
{
public:
    explicit CaptureTab (KlingeProcessor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    KlingeProcessor& proc;
    juce::TextButton load { "Load profile..." }, clear { "Remove" };
    Chip on, stereo;
    ChoiceSegments position;
    Knob in, out, mix;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String shownName, shownStatus;
};

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------
class ShaperMini : public juce::Component
{
public:
    explicit ShaperMini (KlingeProcessor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    KlingeProcessor& proc;
    juce::ComboBox patterns;
    ChoiceBox rate;
    ShaperCanvas canvas;
    Knob amount;
};

class BasicView : public juce::Component
{
public:
    explicit BasicView (KlingeProcessor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    Meter inMeter { "In" }, outMeter { "Out" };
private:
    KlingeProcessor& proc;
    AlgoList algos;
    CurveDisplay curve;
    HarmonicDisplay harmonics;
    Knob drive, character, focus, punch, faithful, mix, output;
    ShaperMini shaper;
    int lastRouting = -1;
};

class AdvancedView : public juce::Component
{
public:
    explicit AdvancedView (KlingeProcessor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void showTab (int t);
    void visibilityChanged() override { analyse.visibilityChanged(); }   // analyser runs only while visible
    Meter inMeter { "In" }, outMeter { "Out" };
private:
    void bandSelected (int b);
    KlingeProcessor& proc;

    // left column
    Knob inGain, drive, preHP, preLP, focus;
    Knob dyn, punch, punchSpeed;
    Knob fbAmount, fbTime, fbTone, analog;
    Knob postHP, postLP, faithful, mix, output;
    Chip clip;
    ChoiceBox osQuality, renderOs;

    // centre
    juce::OwnedArray<juce::TextButton> tabButtons;
    BandsTab bands;
    HarmonicsTab harmonicsTab;
    ModulationTab modulation;
    CaptureTab captureTab;
    AnalyseTab analyse;
    int tab = 0;

    // right column
    CurveDisplay curve;
    HarmonicDisplay harmonics;
    float agDb = 0, punchDb = 0, lowDb = 0, highDb = 0, shpDb = 0;
};

} // namespace klinge::ui
