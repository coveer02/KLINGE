#include "LookAndFeel.h"
#include "BinaryData.h"

namespace klinge::ui
{
juce::Colour temper (float t)
{
    static const juce::Colour stops[] = { col::straw, col::gold, col::bronze, col::purple, col::blue };
    t = juce::jlimit (0.0f, 1.0f, t) * 4.0f;
    const int i = juce::jmin (3, (int) t);
    return stops[i].interpolatedWith (stops[i + 1], t - (float) i);
}

juce::ColourGradient temperGradient (juce::Point<float> from, juce::Point<float> to)
{
    juce::ColourGradient g (col::straw, from, col::blue, to, false);
    g.addColour (0.25, col::gold);
    g.addColour (0.5, col::bronze);
    g.addColour (0.75, col::purple);
    return g;
}

namespace fonts
{
    static juce::Typeface::Ptr face (int which)
    {
        static juce::Typeface::Ptr f[4] = {
            juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceGroteskRegular_ttf, BinaryData::SpaceGroteskRegular_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceGroteskMedium_ttf, BinaryData::SpaceGroteskMedium_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceGroteskBold_ttf, BinaryData::SpaceGroteskBold_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize)
        };
        return f[juce::jlimit (0, 3, which)];
    }

    juce::Font ui (float height, int weight)  { return juce::Font (juce::FontOptions (face (juce::jlimit (0, 2, weight))).withHeight (height)); }
    juce::Font mono (float height)            { return juce::Font (juce::FontOptions (face (3)).withHeight (height)); }
}

KlingeLookAndFeel::KlingeLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);
    setColour (juce::PopupMenu::backgroundColourId, col::well);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::headerTextColourId, col::textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::track);
    setColour (juce::PopupMenu::highlightedTextColourId, col::straw);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::arrowColourId, col::textDim);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextEditor::backgroundColourId, col::well);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::outlineColourId, col::track);
    setColour (juce::TextEditor::focusedOutlineColourId, col::gold);
    setColour (juce::TextEditor::highlightColourId, col::bronze.withAlpha (0.5f));
    setColour (juce::CaretComponent::caretColourId, col::straw);
    setColour (juce::AlertWindow::backgroundColourId, col::panel);
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::AlertWindow::outlineColourId, col::panelEdge);
    setColour (juce::TooltipWindow::backgroundColourId, col::well);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TextButton::buttonColourId, col::panelEdge);
    setColour (juce::TextButton::textColourOffId, col::text);
}

juce::Typeface::Ptr KlingeLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()
        || f.getTypefaceName() == "<Sans-Serif>")
        return fonts::ui (f.getHeight(), f.isBold() ? 2 : 0).getTypefacePtr();
    return LookAndFeel_V4::getTypefaceForFont (f);
}

// ---------------------------------------------------------------------------
void KlingeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float a0, float a1, juce::Slider& s)
{
    const bool bipolar = s.getProperties()["bipolar"];
    const bool heat    = s.getProperties()["heat"];
    const int  size    = s.getProperties().getWithDefault ("size", 0);
    const float alpha  = s.isEnabled() ? 1.0f : 0.35f;

    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float side = juce::jmin (b.getWidth(), b.getHeight());
    b = b.withSizeKeepingCentre (side, side);
    const auto c = b.getCentre();
    const float thick = size == 2 ? 7.0f : (size == 1 ? 5.0f : 3.5f);
    const float r = side * 0.5f - thick * 0.5f - 1.0f;
    const float ang = a0 + pos * (a1 - a0);

    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a1, true);
    g.setColour (col::track.withMultipliedAlpha (alpha));
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    const bool fromEnd = s.getProperties()["fromEnd"];
    const float from = bipolar ? (a0 + a1) * 0.5f : (fromEnd ? a1 : a0);
    if (std::abs (ang - from) > 0.001f)
    {
        if (heat)
        {
            const int segs = 64;
            const float lo = juce::jmin (from, ang), hi = juce::jmax (from, ang);
            for (int i = 0; i < segs; ++i)
            {
                const float s0 = lo + (hi - lo) * (float) i / segs;
                const float s1 = lo + (hi - lo) * (float) (i + 1) / segs + 0.004f;
                juce::Path seg;
                seg.addCentredArc (c.x, c.y, r, r, 0.0f, s0, juce::jmin (s1, hi), true);
                g.setColour (temper ((s0 - a0) / (a1 - a0)).withMultipliedAlpha (alpha));
                g.strokePath (seg, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            }
        }
        else
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, r, r, 0.0f, from, ang, true);
            g.setColour (col::straw.withMultipliedAlpha (alpha));
            g.strokePath (arc, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // modulation: range ring outside the track, dot at the live position
    if ((bool) s.getProperties()["modActive"])
    {
        const float lo = (float) (double) s.getProperties()["modLo"], hi = (float) (double) s.getProperties()["modHi"];
        const float rr = r + thick * 0.5f + 3.5f;
        juce::Path ring;
        ring.addCentredArc (c.x, c.y, rr, rr, 0.0f, a0 + juce::jlimit (0.0f, 1.0f, lo) * (a1 - a0),
                            a0 + juce::jlimit (0.0f, 1.0f, hi) * (a1 - a0), true);
        g.setColour (col::mod.withMultipliedAlpha (0.55f * alpha));
        g.strokePath (ring, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float now = (float) (double) s.getProperties()["modNow"];
        if (now >= 0.0f)
        {
            const auto dot = c.getPointOnCircumference (rr, a0 + juce::jlimit (0.0f, 1.0f, now) * (a1 - a0));
            g.setColour (col::mod.withMultipliedAlpha (alpha));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (dot));
        }
    }
    if ((bool) s.getProperties()["dropHover"])
    {
        g.setColour (col::mod.withAlpha (0.35f));
        g.drawEllipse (b.reduced (1.0f), 2.0f);
    }

    // ticks on large knobs
    if (size == 2)
    {
        g.setColour (col::textFaint.withMultipliedAlpha (alpha));
        for (int i = 0; i <= 12; ++i)
        {
            const float ta = a0 + (a1 - a0) * (float) i / 12.0f;
            const auto p0 = c.getPointOnCircumference (r + thick * 0.5f + 3.0f, ta);
            const auto p1 = c.getPointOnCircumference (r + thick * 0.5f + (i % 3 == 0 ? 8.0f : 5.5f), ta);
            g.drawLine ({ p0, p1 }, 1.0f);
        }
    }

    // body
    const float br = r - thick * 0.5f - (size == 2 ? 7.0f : 4.0f);
    auto body = juce::Rectangle<float> (br * 2, br * 2).withCentre (c);
    juce::ColourGradient grad (juce::Colour (0xff3d444f), c.x, body.getY(), juce::Colour (0xff23272d), c.x, body.getBottom(), false);
    g.setGradientFill (grad);
    g.setOpacity (alpha);
    g.fillEllipse (body);
    g.setColour (juce::Colour (0xff4b535e).withMultipliedAlpha (alpha));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    // pointer
    const auto p0 = c.getPointOnCircumference (br * 0.30f, ang);
    const auto p1 = c.getPointOnCircumference (br * 0.86f, ang);
    g.setColour (col::text.withMultipliedAlpha (alpha));
    g.drawLine ({ p0, p1 }, size == 2 ? 3.0f : 2.0f);

    // lock: a small padlock in the corner
    if ((bool) s.getProperties()["locked"])
    {
        const auto lk = juce::Rectangle<float> (8.0f, 6.0f).withPosition ((float) x + (float) w - 10.0f, (float) y + 6.0f);
        g.setColour (col::gold);
        g.fillRoundedRectangle (lk, 1.0f);
        juce::Path shackle;
        shackle.addCentredArc (lk.getCentreX(), lk.getY(), 2.5f, 2.8f, 0.0f, -juce::MathConstants<float>::halfPi,
                               juce::MathConstants<float>::halfPi, true);
        g.strokePath (shackle, juce::PathStrokeType (1.2f));
    }
}

void KlingeLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                          float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, minPos, maxPos, style, s);
        return;
    }

    const float t = (float) (double) s.getProperties().getWithDefault ("heatT", 0.0);
    const float alpha = s.isEnabled() ? 1.0f : 0.35f;
    auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    auto trackR = area.withSizeKeepingCentre (8.0f, area.getHeight());
    g.setColour (col::well.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (trackR, 3.0f);

    const float zeroY = (float) s.getPositionOfValue (0.0);
    g.setColour (col::track);
    g.drawHorizontalLine ((int) zeroY, trackR.getX() - 6.0f, trackR.getRight() + 6.0f);

    const float top = juce::jmin (zeroY, sliderPos), bot = juce::jmax (zeroY, sliderPos);
    if (bot - top > 0.5f)
    {
        g.setColour ((s.getValue() < 0 ? temper (t).withSaturation (0.15f).darker (0.1f) : temper (t)).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (trackR.withY (top).withHeight (bot - top), 2.0f);
    }
    g.setColour (col::text.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (juce::Rectangle<float> (18.0f, 3.0f).withCentre ({ trackR.getCentreX(), sliderPos }), 1.5f);
}

// ---------------------------------------------------------------------------
void KlingeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const int seg = b.getProperties().getWithDefault ("segment", -1);
    const bool on = b.getToggleState();
    const float rad = 4.0f;

    if (b.getProperties()["chip"])
    {
        // toggles: dark well with a lit indicator, so they never read as a selected tab
        g.setColour ((on ? col::well : col::panelEdge).brighter (over && ! on ? 0.12f : 0.0f)
                         .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.fillRoundedRectangle (r, rad);
        g.setColour (on ? col::gold.withAlpha (0.9f) : col::track);
        g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
        if (b.getHeight() >= 26 && b.getWidth() >= 50)
        {
            const auto led = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ r.getX() + 12.0f, r.getCentreY() });
            g.setColour (on ? col::straw : col::textFaint);
            g.fillEllipse (led);
        }
        return;
    }

    if (b.getProperties()["tab"])
    {
        if (on)
        {
            g.setColour (col::straw);
            g.fillRect (r.removeFromBottom (2.0f).reduced (6.0f, 0.0f));
        }
        else if (over)
        {
            g.setColour (col::track);
            g.fillRect (r.removeFromBottom (2.0f).reduced (6.0f, 0.0f));
        }
        return;
    }

    juce::Colour fill = on ? col::straw : col::panelEdge;
    if (! on && over) fill = fill.brighter (0.12f);
    if (down) fill = fill.darker (0.1f);
    if (! b.isEnabled()) fill = fill.withMultipliedAlpha (0.4f);
    g.setColour (fill);

    if (seg < 0)
    {
        g.fillRoundedRectangle (r, rad);
        return;
    }
    juce::Path p;
    const bool left = seg == 0, right = seg == 2;
    p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), rad, rad,
                           left, right, left, right);
    g.fillPath (p);
    if (! right)
    {
        g.setColour (col::bg.withAlpha (0.6f));
        g.drawVerticalLine ((int) r.getRight(), r.getY() + 4, r.getBottom() - 4);
    }
}

void KlingeLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    const bool chip = b.getProperties()["chip"];
    const bool tab = b.getProperties()["tab"];
    const bool on = b.getToggleState();
    auto c = chip || tab ? (on ? col::straw : col::textDim) : (on ? col::ink : col::text);
    if (tab) g.setFont (fonts::ui (14.5f, on ? 2 : 1));
    g.setColour (c.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    auto area = b.getLocalBounds().reduced (4, 0);
    if (chip && b.getHeight() >= 26 && b.getWidth() >= 50) area.removeFromLeft (12);
    g.drawFittedText (b.getButtonText(), area, juce::Justification::centred, 1);
}

juce::Font KlingeLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return fonts::ui (juce::jmin (14.0f, (float) h * 0.52f), 1);
}

void KlingeLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& cb)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (col::well.withMultipliedAlpha (cb.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (cb.isMouseOver (true) ? col::textFaint : col::track);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    juce::Path chevron;
    const float cx = (float) w - 13.0f, cy = (float) h * 0.5f;
    chevron.startNewSubPath (cx - 4, cy - 2);
    chevron.lineTo (cx, cy + 2);
    chevron.lineTo (cx + 4, cy - 2);
    g.setColour (col::textDim);
    g.strokePath (chevron, juce::PathStrokeType (1.5f));
}

juce::Font KlingeLookAndFeel::getComboBoxFont (juce::ComboBox& cb)
{
    return fonts::ui (juce::jmin (14.5f, (float) cb.getHeight() * 0.55f), 1);
}

void KlingeLookAndFeel::positionComboBoxText (juce::ComboBox& cb, juce::Label& l)
{
    l.setBounds (6, 1, cb.getWidth() - 26, cb.getHeight() - 2);
    l.setFont (getComboBoxFont (cb));
}

juce::Font KlingeLookAndFeel::getPopupMenuFont() { return fonts::ui (14.5f, 0); }

void KlingeLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                           bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                           const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (col::track);
        g.fillRect (area.withSizeKeepingCentre (area.getWidth() - 16, 1));
        return;
    }
    auto r = area.reduced (4, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (col::track);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
    }
    g.setColour (! isActive ? col::textFaint : (isHighlighted ? col::straw : (textColour != nullptr ? *textColour : col::text)));
    g.setFont (getPopupMenuFont());
    auto textArea = r.reduced (10, 0);
    if (isTicked)
    {
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) textArea.getX() + 3.0f, (float) textArea.getCentreY() }));
    }
    textArea.removeFromLeft (14);
    if (hasSubMenu)
    {
        juce::Path arrow;
        const float ax = (float) textArea.getRight() - 6.0f, ay = (float) textArea.getCentreY();
        arrow.startNewSubPath (ax - 3, ay - 4); arrow.lineTo (ax + 1, ay); arrow.lineTo (ax - 3, ay + 4);
        g.strokePath (arrow, juce::PathStrokeType (1.5f));
    }
    g.drawFittedText (text, textArea.withTrimmedRight (hasSubMenu ? 16 : 0), juce::Justification::centredLeft, 1);
    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (col::textFaint);
        g.drawText (shortcutKeyText, textArea, juce::Justification::centredRight);
    }
    juce::ignoreUnused (icon);
}

void KlingeLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                       int thumbStart, int thumbSize, bool over, bool down)
{
    auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, w, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, h);
    g.setColour ((over || down) ? col::textFaint : col::track);
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.0f), 3.0f);
}

void KlingeLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (col::well);
    g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 4.0f);
}

void KlingeLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& te)
{
    g.setColour (te.hasKeyboardFocus (true) ? col::gold : col::track);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, 4.0f, 1.0f);
}

void KlingeLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::well);
    g.setColour (col::track);
    g.drawRect (0, 0, w, h);
}

} // namespace klinge::ui
