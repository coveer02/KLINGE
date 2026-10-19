#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace klinge::ui
{
// ---------------------------------------------------------------------------
// Palette: forged steel, with steel temper colours as the "heat" scale
// ---------------------------------------------------------------------------
namespace col
{
    const juce::Colour bg        { 0xff23272e };   // steel graphite
    const juce::Colour panel     { 0xff2b3038 };
    const juce::Colour panelEdge { 0xff363c46 };
    const juce::Colour well      { 0xff1a1d22 };   // display wells
    const juce::Colour track     { 0xff3c434d };
    const juce::Colour text      { 0xffdde2e8 };
    const juce::Colour textDim   { 0xff8d97a3 };
    const juce::Colour textFaint { 0xff5f6874 };
    const juce::Colour straw     { 0xffe9d59a };
    const juce::Colour gold      { 0xffd9a84e };
    const juce::Colour bronze    { 0xffb8743a };
    const juce::Colour purple    { 0xff8a4e86 };
    const juce::Colour blue      { 0xff4c6fb3 };
    const juce::Colour ink       { 0xff16181c };   // text on light fills
    const juce::Colour mod       { 0xff7fa4e6 };   // modulation (cool steel blue, outside the heat scale)
}

// Temper colour for t in [0, 1]: straw -> gold -> bronze -> purple -> blue
juce::Colour temper (float t);
juce::ColourGradient temperGradient (juce::Point<float> from, juce::Point<float> to);

// ---------------------------------------------------------------------------
// Fonts (embedded so the plugin looks the same on every system)
// ---------------------------------------------------------------------------
namespace fonts
{
    juce::Font ui (float height, int weight = 1);   // 0 regular, 1 medium, 2 bold
    juce::Font mono (float height);
}

class KlingeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KlingeLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos,
                           float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;

    juce::Font getAlertWindowTitleFont() override  { return fonts::ui (17, 2); }
    juce::Font getAlertWindowMessageFont() override { return fonts::ui (14, 0); }
    juce::Font getAlertWindowFont() override        { return fonts::ui (14, 0); }
    juce::Font getLabelFont (juce::Label&) override { return fonts::ui (13, 1); }
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart,
                        int thumbSize, bool over, bool down) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
};

} // namespace klinge::ui
