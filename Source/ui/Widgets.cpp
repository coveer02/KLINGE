#include "Widgets.h"

namespace klinge::ui
{
void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    g.setColour (col::panel);
    g.fillRoundedRectangle (r.toFloat(), 8.0f);
    if (title.isNotEmpty())
    {
        g.setColour (col::text);
        g.setFont (fonts::ui (14.5f, 2));
        g.drawText (title, r.reduced (14, 0).withHeight (34), juce::Justification::centredLeft);
    }
}

// ===========================================================================
// Knob
// ===========================================================================
Knob::Knob (KlingeProcessor& p, const juce::String& id, const juce::String& l, Size sz, bool bipolar, bool heat,
            const juce::String& help)
    : paramID (id), proc (p), label (l), size (sz)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    slider.setMouseDragSensitivity (sz == Large ? 320 : 220);
    slider.getProperties().set ("bipolar", bipolar);
    slider.getProperties().set ("heat", heat);
    slider.getProperties().set ("size", (int) sz);
    slider.getProperties().set ("fromEnd", id == pid::preLP || id == pid::postLP);
    addAndMakeVisible (slider);
    attachment = std::make_unique<APVTS::SliderAttachment> (p.apvts, id, slider);
    if (auto* prm = p.apvts.getParameter (id))
        slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
    slider.onPopup = [this] { showMenu(); };
    paramIndex = p.paramIndexOf (id);
    const juce::String fullHelp = help.isEmpty() ? help : help + "  Right-click for modulation and lock.";
    setHelp (*this, fullHelp);
    setHelp (slider, fullHelp);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (size == Large ? 22 : 18);
    r.removeFromBottom (size == Large ? 26 : 18);
    slider.setBounds (r);
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    g.setColour ((modActive ? col::mod : col::textDim).withMultipliedAlpha (alpha));
    g.setFont (fonts::ui (size == Large ? 15.0f : 13.0f, 1));
    g.drawFittedText (label, r.removeFromTop (size == Large ? 22 : 18), juce::Justification::centred, 1);
    g.setColour (col::text.withMultipliedAlpha (alpha));
    g.setFont (fonts::mono (size == Large ? 16.0f : 12.0f));
    g.drawFittedText (slider.getTextFromValue (slider.getValue()), r.removeFromBottom (size == Large ? 26 : 18),
                      juce::Justification::centred, 1);
}

void Knob::refreshMod()
{
    const bool locked = proc.session.isLocked (paramID);
    if (locked != lastLocked)
    {
        lastLocked = locked;
        slider.getProperties().set ("locked", locked);
        slider.repaint();
    }

    const int v = proc.modMatrix.version.load();
    if (v != lastModVersion)
    {
        lastModVersion = v;
        const auto slots = proc.modMatrix.slotsForTarget (paramID);
        modActive = ! slots.empty();
        modLo = modHi = 0;
        for (auto& s : slots)
        {
            if (isBipolar ((ModSource) s.source)) { modLo -= std::abs (s.depth); modHi += std::abs (s.depth); }
            else if (s.depth > 0) modHi += s.depth;
            else modLo += s.depth;
        }
        slider.getProperties().set ("modActive", modActive);
        lastNow = -2.0f;
        repaint();
    }
    if (! modActive) return;
    auto* prm = proc.apvts.getParameter (paramID);
    const float base = prm != nullptr ? prm->getValue() : 0.0f;
    const float now = proc.modulatedValue (paramIndex);
    if (std::abs (now - lastNow) > 0.002f || ! same ((float) (double) slider.getProperties()["modLo"], base + modLo))
    {
        lastNow = now;
        slider.getProperties().set ("modLo", base + modLo);
        slider.getProperties().set ("modHi", base + modHi);
        slider.getProperties().set ("modNow", now);
        slider.repaint();
    }
}

void Knob::showMenu()
{
    juce::PopupMenu m;
    const bool modulatable = proc.isModulatable (paramIndex);
    if (modulatable)
    {
        juce::PopupMenu add;
        for (int s = 1; s < numModSources; ++s)
            add.addItem (modSourceNames()[s], [this, s] { proc.modMatrix.assign (s, paramID, 0.25f); proc.session.markUserEdit(); });
        m.addSubMenu ("Modulate with", add);

        for (auto& slot : proc.modMatrix.slotsForTarget (paramID))
        {
            juce::PopupMenu d;
            for (int pct : { -100, -50, -25, -10, 10, 25, 50, 100 })
            {
                const float depth = (float) pct / 100.0f;
                d.addItem ((pct > 0 ? "+" : "") + juce::String (pct) + " %", true, std::abs (slot.depth - depth) < 0.005f,
                           [this, src = slot.source, depth] { proc.modMatrix.assign (src, paramID, depth); proc.session.markUserEdit(); });
            }
            d.addSeparator();
            d.addItem ("Remove", [this, src = slot.source]
            {
                for (int i = 0; i < numModSlots; ++i)
                {
                    auto sd = proc.modMatrix.getSlot (i);
                    if (sd.source == src && sd.target == paramID) proc.modMatrix.setSlot (i, {});
                }
                proc.session.markUserEdit();
            });
            m.addSubMenu (modSourceNames()[slot.source] + " depth (" + juce::String (juce::roundToInt (slot.depth * 100)) + " %)", d);
        }
        if (modActive)
            m.addItem ("Remove all modulation", [this] { proc.modMatrix.removeTarget (paramID); proc.session.markUserEdit(); });
        m.addSeparator();
    }
    const bool locked = proc.session.isLocked (paramID);
    m.addItem ("Keep when presets load or randomise", true, locked, [this, locked] { proc.session.setLocked (paramID, ! locked); });
    m.addItem ("Reset to default", [this]
    {
        if (auto* prm = proc.apvts.getParameter (paramID))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->getDefaultValue());
            prm->endChangeGesture();
        }
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider));
}

bool Knob::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith ("mod:") && proc.isModulatable (paramIndex);
}

void Knob::itemDragEnter (const SourceDetails&) { slider.getProperties().set ("dropHover", true); slider.repaint(); }
void Knob::itemDragExit (const SourceDetails&)  { slider.getProperties().set ("dropHover", false); slider.repaint(); }

void Knob::itemDropped (const SourceDetails& d)
{
    slider.getProperties().set ("dropHover", false);
    const int src = d.description.toString().fromFirstOccurrenceOf ("mod:", false, false).getIntValue();
    if (src > 0 && src < numModSources)
    {
        if (proc.modMatrix.assign (src, paramID, 0.25f) < 0)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "All 16 modulation slots are in use",
                                                    "Remove a modulation in the Modulation tab to add a new one.");
        proc.session.markUserEdit();
    }
    slider.repaint();
}

// ===========================================================================
// ChoiceSegments / Chip / ChoiceBox
// ===========================================================================
ChoiceSegments::ChoiceSegments (APVTS& s, const juce::String& paramID, const juce::StringArray& labels, const juce::String& help)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->getProperties().set ("segment", labels.size() == 1 ? -1 : (i == 0 ? 0 : (i == labels.size() - 1 ? 2 : 1)));
        b->onClick = [this, i] { if (attachment) attachment->setValueAsCompleteGesture ((float) i); };
        setHelp (*b, help);
        addAndMakeVisible (b);
    }
    setHelp (*this, help);
    if (auto* p = s.getParameter (paramID))
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v)
        {
            const int idx = juce::roundToInt (v);
            for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
        });
        attachment->sendInitialUpdate();
    }
}

void ChoiceSegments::resized()
{
    auto r = getLocalBounds();
    const int w = r.getWidth() / juce::jmax (1, buttons.size());
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds (i == buttons.size() - 1 ? r : r.removeFromLeft (w));
}

Chip::Chip (APVTS& s, const juce::String& paramID, const juce::String& label, const juce::String& help)
    : juce::TextButton (label)
{
    setClickingTogglesState (true);
    getProperties().set ("chip", true);
    attachment = std::make_unique<APVTS::ButtonAttachment> (s, paramID, *this);
    setHelp (*this, help);
}

ChoiceBox::ChoiceBox (APVTS& s, const juce::String& paramID, const juce::String& help)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (paramID)))
        addItemList (p->choices, 1);
    attachment = std::make_unique<APVTS::ComboBoxAttachment> (s, paramID, *this);
    setHelp (*this, help);
}

// ===========================================================================
// IconButton
// ===========================================================================
void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour ((over ? col::panelEdge.brighter (0.12f) : col::panelEdge).darker (down ? 0.1f : 0.0f));
    if (icon != Star && icon != StarFilled) g.fillRoundedRectangle (r, 4.0f);
    const float alpha = isEnabled() ? 1.0f : 0.35f;
    auto c = r.getCentre();
    const float s = std::min (r.getWidth(), r.getHeight()) * 0.28f;
    juce::Path p;
    switch (icon)
    {
        case Undo:
        case Redo:
        {
            const float dir = icon == Undo ? 1.0f : -1.0f;      // undo: arrow head on the left end
            p.addCentredArc (c.x, c.y + s * 0.2f, s, s, 0.0f, dir * -1.9f, dir * 1.3f, true);
            g.setColour (col::text.withMultipliedAlpha (alpha));
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const auto tip = c.getPointOnCircumference (s, dir * -1.9f).translated (0.0f, s * 0.2f);
            juce::Path head;
            head.addTriangle (tip.translated (-3.5f, -2.0f), tip.translated (3.5f, -2.0f), tip.translated (0.0f, 3.5f));
            g.fillPath (head, juce::AffineTransform::rotation (dir * -1.9f + juce::MathConstants<float>::halfPi * (icon == Undo ? -1.0f : 1.0f), tip.x, tip.y));
            return;
        }
        case Dice:
        {
            auto box = juce::Rectangle<float> (s * 2.2f, s * 2.2f).withCentre (c);
            g.setColour (col::text.withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (box, 2.5f, 1.4f);
            for (auto pt : { box.getTopLeft() + juce::Point<float> (box.getWidth() * 0.28f, box.getHeight() * 0.28f), box.getCentre(),
                             box.getBottomRight() - juce::Point<float> (box.getWidth() * 0.28f, box.getHeight() * 0.28f) })
                g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (pt));
            return;
        }
        case Star:
        case StarFilled:
        {
            p.addStar (c, 5, s * 0.55f, s * 1.3f, juce::MathConstants<float>::pi * 0.2f);
            g.setColour ((icon == StarFilled ? col::gold : (over ? col::textDim : col::textFaint)).withMultipliedAlpha (alpha));
            if (icon == StarFilled) g.fillPath (p); else g.strokePath (p, juce::PathStrokeType (1.3f));
            return;
        }
        case Close:
            p.startNewSubPath (c.x - s, c.y - s); p.lineTo (c.x + s, c.y + s);
            p.startNewSubPath (c.x + s, c.y - s); p.lineTo (c.x - s, c.y + s);
            break;
        case Folder:
            p.startNewSubPath (c.x - s * 1.2f, c.y - s * 0.6f); p.lineTo (c.x - s * 0.4f, c.y - s * 0.6f);
            p.lineTo (c.x - s * 0.1f, c.y - s * 0.3f); p.lineTo (c.x + s * 1.2f, c.y - s * 0.3f);
            p.lineTo (c.x + s * 1.2f, c.y + s * 0.9f); p.lineTo (c.x - s * 1.2f, c.y + s * 0.9f); p.closeSubPath();
            break;
        case Search:
            p.addEllipse (juce::Rectangle<float> (s * 1.4f, s * 1.4f).withCentre (c.translated (-s * 0.25f, -s * 0.25f)));
            p.startNewSubPath (c.x + s * 0.3f, c.y + s * 0.3f); p.lineTo (c.x + s, c.y + s);
            break;
        case Prev:
        case Next:
        {
            const float d = icon == Prev ? 1.0f : -1.0f;
            p.startNewSubPath (c.x + d * s * 0.5f, c.y - s); p.lineTo (c.x - d * s * 0.5f, c.y); p.lineTo (c.x + d * s * 0.5f, c.y + s);
            break;
        }
    }
    g.setColour (col::text.withMultipliedAlpha (alpha));
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ===========================================================================
// Meter
// ===========================================================================
void Meter::push (float peak)
{
    const float d = gainToDb (peak);
    levelDb = d > levelDb ? d : std::max (d, levelDb - 1.2f);
    if (d >= holdDb) { holdDb = d; holdTicks = 45; }
    else if (--holdTicks <= 0) holdDb = std::max (d, holdDb - 0.8f);
    repaint();
}

void Meter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setFont (fonts::ui (12.5f, 1));
    g.setColour (col::textDim);
    g.drawText (label, r.removeFromLeft (32), juce::Justification::centredLeft);
    auto read = r.removeFromRight (46);
    g.setFont (fonts::mono (11.5f));
    g.setColour (holdDb > -0.1f ? col::bronze : col::text);
    g.drawText (holdDb < -99 ? juce::String ("-inf") : juce::String (holdDb, 1), read, juce::Justification::centredRight);
    auto bar = r.reduced (0, r.getHeight() * 0.3f);
    g.setColour (col::well);
    g.fillRoundedRectangle (bar, 2.0f);
    auto toX = [&] (float d) { return bar.getX() + clampf ((d + 48.0f) / 54.0f, 0.0f, 1.0f) * bar.getWidth(); };
    const float x = toX (levelDb);
    if (x > bar.getX() + 0.5f)
    {
        g.saveState();
        g.reduceClipRegion (bar.withRight (x).toNearestInt());
        g.setGradientFill (temperGradient (bar.getTopLeft(), bar.getTopRight()));
        g.fillRoundedRectangle (bar, 2.0f);
        g.restoreState();
    }
    g.setColour (col::text);
    g.fillRect (juce::Rectangle<float> (1.5f, bar.getHeight() + 4).withCentre ({ toX (holdDb), bar.getCentreY() }));
    g.setColour (col::textFaint);
    g.fillRect (juce::Rectangle<float> (1.0f, bar.getHeight()).withCentre ({ toX (0.0f), bar.getCentreY() }));
}

// ===========================================================================
// HarmonicFaders
// ===========================================================================
HarmonicFaders::HarmonicFaders (KlingeProcessor& p)
{
    for (int n = 0; n < numHarmonics; ++n)
    {
        auto& f = faders[(size_t) n];
        f.setSliderStyle (juce::Slider::LinearVertical);
        f.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        f.getProperties().set ("heatT", (double) n / (numHarmonics - 1));
        f.setDoubleClickReturnValue (true, 0.0);
        f.onValueChange = [this] { repaint(); };
        addAndMakeVisible (f);
        attachments.push_back (std::make_unique<APVTS::SliderAttachment> (p.apvts, pid::harm (n + firstHarmonic), f));
        setHelp (f, "Adds harmonic " + juce::String (n + firstHarmonic)
                    + ". Pull below zero to subtract it instead. Double-click resets.");
    }
}

void HarmonicFaders::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (18);
    r.removeFromBottom (18);
    const int w = r.getWidth() / numHarmonics;
    for (auto& f : faders) f.setBounds (r.removeFromLeft (w));
}

void HarmonicFaders::paint (juce::Graphics& g)
{
    for (int n = 0; n < numHarmonics; ++n)
    {
        const auto fb = faders[(size_t) n].getBounds();
        g.setFont (fonts::mono (11.0f));
        g.setColour (col::text);
        const int v = juce::roundToInt (faders[(size_t) n].getValue());
        g.drawText ((v > 0 ? "+" : "") + juce::String (v), fb.getX(), 0, fb.getWidth(), 16, juce::Justification::centred);
        g.setFont (fonts::ui (12.5f, 1));
        g.setColour (col::textDim);
        g.drawText (juce::String (n + firstHarmonic), fb.getX(), getHeight() - 16, fb.getWidth(), 16, juce::Justification::centred);
    }
}

// ===========================================================================
// Band displays
// ===========================================================================
bool BandView::operator== (const BandView& o) const
{
    const auto& a = band;
    const auto& b = o.band;
    if (a.type != b.type || ! same (a.driveDb, b.driveDb) || ! same (a.shape, b.shape) || ! same (a.bias, b.bias)
        || ! same (a.harm, b.harm) || ! same (a.sag, b.sag) || ! same (globalDriveDb, o.globalDriveDb)
        || ! same (recipe.lock, o.recipe.lock))
        return false;
    for (size_t i = 0; i < recipe.h.size(); ++i)
        if (! same (recipe.h[i], o.recipe.h[i])) return false;
    return true;
}

BandView BandView::read (KlingeProcessor& p, int b)
{
    std::vector<float> v;
    p.readBaseValues (v);
    EngineParams ep;
    buildEngineParams (v.data(), p.ix, ep);
    BandView bv;
    bv.band = ep.bands[(size_t) juce::jlimit (0, numBands - 1, b)];
    bv.band.mix = 1.0f; bv.band.levelDb = 0.0f; bv.band.on = true; bv.band.mute = false; bv.band.solo = false;
    bv.band.dyn = 0.0f; bv.band.stereo = BandStereo::Stereo;
    bv.globalDriveDb = ep.driveDb;
    bv.recipe = ep.recipe;
    return bv;
}

static void simulate (const BandView& v, double freq, float amp, int settleCycles, int cycles, std::vector<float>& xs, std::vector<float>& ys)
{
    constexpr double fsSim = 192000.0;
    BandProcessor bp;
    bp.prepare (fsSim, 4);
    bp.setParams (v.band, v.globalDriveDb, 0.0f, v.recipe, 0.0f, 0, 0);
    const int per = (int) std::lround (fsSim / freq);
    const int total = per * (settleCycles + cycles);
    xs.clear(); ys.clear();
    for (int i = 0; i < total; ++i)
    {
        bp.tick();
        const float x = amp * (float) std::sin (2.0 * kPiD * i / per);
        const float y = bp.processSample (x, 0);
        if (i >= per * settleCycles) { xs.push_back (x); ys.push_back (y); }
    }
}

void BandView::transfer (std::vector<juce::Point<float>>& pts, Plot& plot) const
{
    pts.clear();
    const Algo t = band.type;
    if (t == Algo::Stack || t == Algo::Amp)
    {
        plot = Plot::Wave;
        std::vector<float> xs, ys;
        simulate (*this, 200.0, 1.0f, 6, 1, xs, ys);
        float peak = 1.0e-9f;
        for (auto y : ys) peak = std::max (peak, std::abs (y));
        const int per = (int) ys.size();
        for (int i = 0; i < per; i += 2)
            pts.push_back ({ -1.0f + 2.0f * (float) i / (float) per, ys[(size_t) i] / peak });
        return;
    }
    plot = isCircuit ((int) t) || t == Algo::Slew || t == Algo::Tape ? Plot::Loop : Plot::Curve;
    if (plot == Plot::Loop)
    {
        std::vector<float> xs, ys;
        simulate (*this, 200.0, 1.0f, 6, 1, xs, ys);
        for (size_t i = 0; i < xs.size(); i += 2) pts.push_back ({ xs[i], ys[i] });
        if (! pts.empty()) pts.push_back (pts.front());
        return;
    }
    const float totalDb = clampf (band.driveDb + globalDriveDb, -24.0f, maxTotalDrive);
    const float g = dbToGain (totalDb);
    const float bo = clampf (band.bias, -1.0f, 1.0f) * 0.6f;
    const float bd = staticCurve (t, bo, band.shape);
    const float comp = compensationFor (t, g, totalDb, band.shape, bo);
    float q = 32768.0f;
    if (t == Algo::Crush)
    {
        const float intensity = clampf (totalDb / 48.0f, 0.0f, 1.0f);
        const float bitsAmt = intensity * std::min (1.0f, 2.0f * (1.0f - band.shape));
        q = std::exp2 (16.0f - 13.5f * std::pow (bitsAmt, 0.6f) - 1.0f);
    }
    const bool harmOn = band.harm > 0.0005f && ! recipe.isZero();
    for (int i = 0; i <= 400; ++i)
    {
        const float x = -1.0f + 2.0f * (float) i / 400.0f;
        float y;
        if (t == Algo::Crush)
        {
            auto qf = [q] (float vv) { return std::round (clampf (vv, -1.0f, 1.0f) * q) / q; };
            y = qf (x + bo) - qf (bo);
        }
        else y = comp * (staticCurve (t, g * x + bo, band.shape) - bd);
        if (harmOn) y += band.harm * (harmonicStatic (recipe, x, 1.0f) - harmonicStatic (recipe, 0.0f, 1.0f));
        pts.push_back ({ x, y });
    }
}

std::array<float, 10> BandView::harmonicsDb (float& thd) const
{
    std::vector<float> xs, ys;
    simulate (*this, 1000.0, 0.5f, 25, 20, xs, ys);
    const int N = (int) ys.size();
    std::array<float, 10> mag {};
    for (int k = 1; k <= 10; ++k)
    {
        double re = 0, im = 0;
        for (int i = 0; i < N; ++i)
        {
            const double ph = 2.0 * kPiD * k * 20.0 * i / N;
            re += ys[(size_t) i] * std::cos (ph);
            im += ys[(size_t) i] * std::sin (ph);
        }
        mag[(size_t) (k - 1)] = (float) (2.0 * std::sqrt (re * re + im * im) / N);
    }
    const float ref = std::max (mag[0], 1.0e-9f);
    std::array<float, 10> out {};
    double sum = 0;
    for (size_t k = 0; k < 10; ++k)
    {
        out[k] = 20.0f * std::log10 (std::max (mag[k], 1.0e-9f) / ref);
        if (k > 0) sum += (double) mag[k] * mag[k];
    }
    thd = (float) (100.0 * std::sqrt (sum) / ref);
    return out;
}

void CurveDisplay::refresh()
{
    auto v = BandView::read (proc, band);
    if (valid && v == view) return;
    view = v;
    valid = true;
    view.transfer (pts, plot);
    repaint();
}

void CurveDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (bounds, 6.0f);

    auto header = bounds.removeFromTop (34).reduced (12, 0);
    g.setFont (fonts::ui (16.0f, 2));
    g.setColour (col::text);
    const juce::String algo = algoNames()[(int) view.band.type];
    g.drawText (bandName.isEmpty() ? algo : bandName + ": " + algo, header, juce::Justification::centredLeft);
    const float totalDb = clampf (view.band.driveDb + view.globalDriveDb, -24.0f, maxTotalDrive);
    g.setFont (fonts::mono (13.0f));
    g.setColour (col::textDim);
    g.drawText ((totalDb >= 0 ? "+" : "") + juce::String (totalDb, 1) + " dB", header, juce::Justification::centredRight);

    const bool caption = plot != BandView::Plot::Curve;
    auto area = bounds.reduced (14, 10).withTrimmedBottom (caption ? 16 : 4);
    const bool wave = plot == BandView::Plot::Wave;
    const float side = wave ? area.getWidth() : std::min (area.getWidth(), area.getHeight() * 1.25f);
    area = area.withSizeKeepingCentre (side, area.getHeight());
    const float range = wave ? 1.1f : 1.25f;
    auto mapX = [&] (float x) { return area.getX() + (x + 1.0f) * 0.5f * area.getWidth(); };
    auto mapY = [&] (float y) { return area.getCentreY() - clampf (y / range, -1.0f, 1.0f) * area.getHeight() * 0.5f; };

    g.setColour (col::track.withAlpha (0.55f));
    g.drawHorizontalLine ((int) mapY (0), area.getX(), area.getRight());
    if (! wave) g.drawVerticalLine ((int) mapX (0), area.getY(), area.getBottom());
    g.setColour (col::track.withAlpha (0.25f));
    for (float v : { -1.0f, -0.5f, 0.5f, 1.0f })
    {
        g.drawHorizontalLine ((int) mapY (v), area.getX(), area.getRight());
        g.drawVerticalLine ((int) mapX (v), area.getY(), area.getBottom());
    }

    // reference: unity line, or the input sine for the waveform view
    juce::Path reference;
    if (wave)
    {
        for (int i = 0; i <= 200; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / 200.0f;
            const juce::Point<float> p (mapX (x), mapY (std::sin (kPi * (x + 1.0f))));
            if (i == 0) reference.startNewSubPath (p); else reference.lineTo (p);
        }
    }
    else
    {
        reference.startNewSubPath (mapX (-1), mapY (-1));
        reference.lineTo (mapX (1), mapY (1));
    }
    juce::Path dashed;
    const float dash[] = { 4.0f, 5.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, reference, dash, 2);
    g.setColour (col::textFaint.withAlpha (0.6f));
    g.fillPath (dashed);

    juce::Path curve;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const juce::Point<float> p (mapX (pts[i].x), mapY (pts[i].y));
        if (i == 0) curve.startNewSubPath (p); else curve.lineTo (p);
    }
    const auto heat = temper (clampf (totalDb / 54.0f, 0.0f, 1.0f));
    g.setColour (heat.withAlpha (0.16f));
    g.strokePath (curve, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (heat);
    g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (caption)
    {
        g.setFont (fonts::ui (11.5f, 0));
        g.setColour (col::textFaint);
        g.drawText (wave ? "One 200 Hz cycle at 0 dBFS. Dashed: input."
                         : "200 Hz at 0 dBFS. An open loop shows memory.",
                    bounds.reduced (12, 4).removeFromBottom (14), juce::Justification::centredRight);
    }
}

void HarmonicDisplay::refresh()
{
    auto v = BandView::read (proc, band);
    if (valid && v == view) return;
    view = v;
    valid = true;
    db = view.harmonicsDb (thd);
    repaint();
}

void HarmonicDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (bounds, 6.0f);
    auto header = bounds.removeFromTop (30).reduced (12, 0);
    g.setFont (fonts::ui (13.5f, 1));
    g.setColour (col::textDim);
    g.drawText ("Harmonics, 1 kHz at -6 dBFS", header, juce::Justification::centredLeft);
    g.setFont (fonts::mono (12.0f));
    g.setColour (col::text);
    g.drawText (thd > 999.0f ? juce::String ("THD > 999 %") : "THD " + juce::String (thd, thd < 10 ? 2 : 1) + " %", header,
                juce::Justification::centredRight);

    auto area = bounds.reduced (12, 6);
    auto labels = area.removeFromBottom (16);
    const float bw = area.getWidth() / 10.0f;
    const float floorDb = -84.0f;
    g.setColour (col::track.withAlpha (0.3f));
    for (float d : { -24.0f, -48.0f, -72.0f })
        g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * (d / floorDb)), area.getX(), area.getRight());
    for (int k = 0; k < 10; ++k)
    {
        const float t = clampf ((db[(size_t) k] - floorDb) / -floorDb, 0.0f, 1.0f);
        auto bar = juce::Rectangle<float> (area.getX() + bw * (float) k, area.getY(), bw, area.getHeight()).reduced (bw * 0.22f, 0);
        bar = bar.withTop (bar.getBottom() - t * bar.getHeight());
        g.setColour (k == 0 ? col::textDim : temper ((float) (k - 1) / 8.0f));
        if (bar.getHeight() > 0.5f) g.fillRoundedRectangle (bar, 2.0f);
        g.setFont (fonts::mono (10.5f));
        g.setColour (col::textFaint);
        g.drawText (juce::String (k + 1), labels.withX (area.getX() + bw * (float) k).withWidth (bw).toNearestInt(), juce::Justification::centred);
    }
}

} // namespace klinge::ui
