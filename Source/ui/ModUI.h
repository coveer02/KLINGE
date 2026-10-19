#pragma once

#include "Widgets.h"

namespace klinge::ui
{
// Draggable source: click selects its editor, drag onto any knob to modulate it
class SourceChip : public juce::Component
{
public:
    SourceChip (KlingeProcessor& p, int source, std::function<void (int)> onSelect);
    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
private:
    KlingeProcessor& proc;
    int source;
    std::function<void (int)> onSelect;
    bool selected = false;
    float value = 0.0f;
    int uses = 0, lastVersion = -1;
};

class LfoEditor : public juce::Component
{
public:
    LfoEditor (KlingeProcessor& p, int index);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    KlingeProcessor& proc;
    int lfo;
    ChoiceBox shape, division;
    Chip sync, retrig;
    Knob rate, phase, smooth;
    juce::Rectangle<int> preview;
    float phaseNow = 0.0f;
};

class EnvEditor : public juce::Component
{
public:
    EnvEditor (KlingeProcessor& p, int index);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    KlingeProcessor& proc;
    int env;
    ChoiceSegments source;
    Knob attack, release, gain;
    juce::Rectangle<int> meterArea;
    float value = 0.0f;
    std::array<float, 120> history {};
    int histPos = 0;
};

// Drawable curve for the shaper
class ShaperCanvas : public juce::Component
{
public:
    explicit ShaperCanvas (KlingeProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool editable = true;
private:
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen (const ShaperPoint& p) const;
    ShaperPoint fromScreen (juce::Point<float> s) const;
    int hitPoint (juce::Point<float> s) const;
    int hitSegment (juce::Point<float> s) const;
    KlingeProcessor& proc;
    std::vector<ShaperPoint> pts;
    int lastVersion = -1, dragging = -1, curving = -1, hover = -1;
    float curveStart = 0, dragStartY = 0, phase = 0;
};

class ShaperEditor : public juce::Component
{
public:
    explicit ShaperEditor (KlingeProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    ShaperCanvas canvas;
private:
    KlingeProcessor& proc;
    juce::ComboBox presets;
    ChoiceBox rate;
    Knob amount, smooth, phase;
    Chip retrig;
};

// List of active modulation slots with depth sliders
class MatrixList : public juce::Component
{
public:
    explicit MatrixList (KlingeProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    struct Row : public juce::Component
    {
        Row (MatrixList& o, int slot);
        void paint (juce::Graphics&) override;
        void resized() override;
        MatrixList& owner;
        int slot;
        juce::Slider depth;
        IconButton remove { IconButton::Close, "Remove this modulation." };
    };
    void rebuild();
    juce::String targetName (const juce::String& id) const;
    KlingeProcessor& proc;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<Row> rows;
    int lastVersion = -1;
};

class ModulationTab : public juce::Component
{
public:
    explicit ModulationTab (KlingeProcessor& p);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void select (int source);
    KlingeProcessor& proc;
    juce::OwnedArray<SourceChip> chips;
    LfoEditor lfo1 { proc, 0 }, lfo2 { proc, 1 };
    EnvEditor env1 { proc, 0 }, env2 { proc, 1 };
    ShaperEditor shaper { proc };
    MatrixList matrix { proc };
    int selected = 1;
};

} // namespace klinge::ui
