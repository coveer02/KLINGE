#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeel.h"
#include "../PluginProcessor.h"

namespace klinge::ui
{
using APVTS = juce::AudioProcessorValueTreeState;

inline void setHelp (juce::Component& c, const juce::String& text) { c.getProperties().set ("help", text); }

// True when the component and all its parents are visible. Unlike isShowing() this does not need a
// window peer, so it also holds while a host is still attaching the editor (and in offscreen tests).
inline bool visibleInTree (const juce::Component& c)
{
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (! p->isVisible()) return false;
    return true;
}
void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title);

// ---------------------------------------------------------------------------
// Knob with modulation ring, lock indicator, context menu and drop target for mod sources
// ---------------------------------------------------------------------------
class Knob : public juce::Component,
             public juce::DragAndDropTarget
{
public:
    enum Size { Small = 0, Medium = 1, Large = 2 };
    Knob (KlingeProcessor&, const juce::String& paramID, const juce::String& label, Size size,
          bool bipolar = false, bool heat = false, const juce::String& help = {});

    void setLabel (const juce::String& l) { if (label != l) { label = l; repaint(); } }
    void refreshMod();                        // poll modulation / lock state (owner timer)
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    struct Dial : public juce::Slider
    {
        std::function<void()> onPopup;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { if (onPopup) onPopup(); return; }
            juce::Slider::mouseDown (e);
        }
    } slider;

    const juce::String paramID;

private:
    void showMenu();
    KlingeProcessor& proc;
    juce::String label;
    Size size;
    int paramIndex = -1;
    int lastModVersion = -1;
    float modLo = 0, modHi = 0, lastNow = -2.0f;
    bool modActive = false, lastLocked = false;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

// Segmented control bound to a choice (or bool) parameter
class ChoiceSegments : public juce::Component
{
public:
    ChoiceSegments (APVTS&, const juce::String& paramID, const juce::StringArray& labels, const juce::String& help = {});
    void resized() override;
private:
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// Toggle chip bound to a bool parameter
class Chip : public juce::TextButton
{
public:
    Chip (APVTS&, const juce::String& paramID, const juce::String& label, const juce::String& help = {});
private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

// Combo box bound to a choice parameter
class ChoiceBox : public juce::ComboBox
{
public:
    ChoiceBox (APVTS&, const juce::String& paramID, const juce::String& help = {});
private:
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

// Small square button with a drawn icon
class IconButton : public juce::Button
{
public:
    enum Icon { Undo, Redo, Dice, Star, StarFilled, Close, Folder, Search, Prev, Next };
    IconButton (Icon i, const juce::String& help = {}) : juce::Button ({}), icon (i) { setHelp (*this, help); }
    void setIcon (Icon i) { if (i != icon) { icon = i; repaint(); } }
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    Icon icon;
};

// Horizontal level meter with peak hold
class Meter : public juce::Component
{
public:
    explicit Meter (juce::String l) : label (std::move (l)) {}
    void push (float peak);
    void paint (juce::Graphics&) override;
private:
    juce::String label;
    float levelDb = -100, holdDb = -100;
    int holdTicks = 0;
};

// Seven bipolar faders for the harmonic designer
class HarmonicFaders : public juce::Component
{
public:
    explicit HarmonicFaders (KlingeProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    std::array<juce::Slider, numHarmonics> faders;
    std::vector<std::unique_ptr<APVTS::SliderAttachment>> attachments;
};

// ---------------------------------------------------------------------------
// Band displays, computed by simulating the band with its current settings
// ---------------------------------------------------------------------------
struct BandView
{
    BandParams band;
    float globalDriveDb = 0;
    HarmonicRecipe recipe;
    bool operator== (const BandView& o) const;
    static BandView read (KlingeProcessor&, int band);

    // What the curve display shows:
    //  Curve: memoryless transfer curve (exact)
    //  Loop:  200 Hz Lissajous figure from the real processor (models with memory)
    //  Wave:  one 200 Hz cycle of the output over time (Amp and Stack, whose cabinet filters make a
    //         transfer figure meaningless)
    enum class Plot { Curve, Loop, Wave };
    void transfer (std::vector<juce::Point<float>>& pts, Plot& plot) const;
    // harmonics 1..10 of a 1 kHz sine at -6 dBFS, run through the real band processor
    std::array<float, 10> harmonicsDb (float& thd) const;
};

class CurveDisplay : public juce::Component
{
public:
    explicit CurveDisplay (KlingeProcessor& p) : proc (p) {}
    void setBand (int b) { if (b != band) { band = b; valid = false; } }
    void setBandName (const juce::String& n) { if (bandName != n) { bandName = n; repaint(); } }
    void refresh();
    void paint (juce::Graphics&) override;
private:
    KlingeProcessor& proc;
    int band = 0;
    juce::String bandName;
    BandView view;
    bool valid = false;
    BandView::Plot plot = BandView::Plot::Curve;
    std::vector<juce::Point<float>> pts;
};

class HarmonicDisplay : public juce::Component
{
public:
    explicit HarmonicDisplay (KlingeProcessor& p) : proc (p) {}
    void setBand (int b) { if (b != band) { band = b; valid = false; } }
    void refresh();
    void paint (juce::Graphics&) override;
private:
    KlingeProcessor& proc;
    int band = 0;
    BandView view;
    bool valid = false;
    std::array<float, 10> db {};
    float thd = 0;
};

} // namespace klinge::ui
