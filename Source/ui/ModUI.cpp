#include "ModUI.h"

namespace klinge::ui
{
namespace
{
    const char* sourceHelp (int s)
    {
        switch ((ModSource) s)
        {
            case ModSource::Lfo1:
            case ModSource::Lfo2:     return "Repeating movement, free or in time with the host. Drag onto any knob to modulate it.";
            case ModSource::Env1:
            case ModSource::Env2:     return "Follows the loudness of the input or the sidechain. Drag onto any knob to modulate it.";
            case ModSource::Shaper:   return "Your drawn rhythm. Shapes the volume of the processed sound and can also be dragged onto any knob.";
            case ModSource::Velocity: return "Velocity of the last MIDI note sent to KLINGE. Drag onto any knob to modulate it.";
            case ModSource::ModWheel: return "MIDI mod wheel (CC 1). Drag onto any knob to modulate it.";
            case ModSource::None:
            case ModSource::Count:    break;
        }
        return "";
    }

    float lfoPreview (int shape, float p)
    {
        p -= std::floor (p);
        switch (shape)
        {
            case 0: return std::sin (2.0f * kPi * p);
            case 1: return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
            case 2: return 2.0f * p - 1.0f;
            case 3: return 1.0f - 2.0f * p;
            case 4: return p < 0.5f ? 1.0f : -1.0f;
            case 5: { const int st = (int) (p * 8); static const float v[] = { 0.3f, -0.7f, 0.9f, -0.2f, 0.5f, -0.9f, 0.1f, 0.7f }; return v[st & 7]; }
            default:
            {
                static const float v[] = { 0.3f, -0.7f, 0.9f, -0.2f, 0.5f, -0.9f, 0.1f, 0.7f, 0.3f };
                const float x = p * 8; const int i = (int) x; const float t = 0.5f - 0.5f * std::cos (kPi * (x - (float) i));
                return v[i] + t * (v[i + 1] - v[i]);
            }
        }
    }
}

// ===========================================================================
SourceChip::SourceChip (KlingeProcessor& p, int s, std::function<void (int)> sel)
    : proc (p), source (s), onSelect (std::move (sel))
{
    setHelp (*this, sourceHelp (s));
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void SourceChip::refresh()
{
    const float v = proc.getModEngine().display[(size_t) source].load (std::memory_order_relaxed);
    const int ver = proc.modMatrix.version.load();
    if (ver != lastVersion)
    {
        lastVersion = ver;
        uses = 0;
        for (int i = 0; i < numModSlots; ++i)
        {
            auto s = proc.modMatrix.getSlot (i);
            if (! s.isEmpty() && s.source == source) ++uses;
        }
        repaint();
    }
    if (std::abs (v - value) > 0.005f) { value = v; repaint(); }
}

void SourceChip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (selected ? col::well : col::panelEdge);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (selected ? col::mod : col::track);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    auto text = r.reduced (8, 3);
    auto bar = text.removeFromBottom (4);
    g.setColour (col::well.brighter (0.05f));
    g.fillRoundedRectangle (bar, 2.0f);
    const bool bip = isBipolar ((ModSource) source);
    g.setColour (col::mod);
    if (bip)
    {
        const float c = bar.getCentreX(), x = c + value * bar.getWidth() * 0.5f;
        g.fillRoundedRectangle (juce::Rectangle<float> (std::min (c, x), bar.getY(), std::abs (x - c) + 1.0f, bar.getHeight()), 2.0f);
    }
    else g.fillRoundedRectangle (bar.withWidth (std::max (1.0f, clampf (value, 0.0f, 1.0f) * bar.getWidth())), 2.0f);
    g.setFont (fonts::ui (13.0f, selected ? 2 : 1));
    g.setColour (selected ? col::text : col::textDim);
    g.drawText (modSourceNames()[source], text, juce::Justification::centredLeft);
    if (uses > 0)
    {
        g.setFont (fonts::mono (11.0f));
        g.setColour (col::mod);
        g.drawText (juce::String (uses), text, juce::Justification::centredRight);
    }
}

void SourceChip::mouseDown (const juce::MouseEvent&) { if (onSelect) onSelect (source); }

void SourceChip::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 4) return;
    if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! dc->isDragAndDropActive())
        {
            juce::Image img (juce::Image::ARGB, getWidth(), getHeight(), true);
            juce::Graphics g (img);
            paint (g);
            dc->startDragging ("mod:" + juce::String (source), this, juce::ScaledImage (img), true);
        }
}

// ===========================================================================
LfoEditor::LfoEditor (KlingeProcessor& p, int index)
    : proc (p), lfo (index),
      shape (p.apvts, pid::lfo (index, pid::lShape), "Waveform of the LFO."),
      division (p.apvts, pid::lfo (index, pid::lDiv), "Length of one cycle in note values (when Sync is on)."),
      sync (p.apvts, pid::lfo (index, pid::lSync), "Sync", "Locks the LFO to the host tempo and position."),
      retrig (p.apvts, pid::lfo (index, pid::lRetrig), "MIDI retrig", "Restarts the LFO on every MIDI note and ignores the host position."),
      rate (p, pid::lfo (index, pid::lRate), "Rate", Knob::Small, false, false, "Speed in Hz when Sync is off."),
      phase (p, pid::lfo (index, pid::lPhase), "Phase", Knob::Small, false, false, "Shifts the waveform within its cycle."),
      smooth (p, pid::lfo (index, pid::lSmooth), "Smooth", Knob::Small, false, false, "Rounds off hard edges such as square and random steps.")
{
    for (auto* c : std::initializer_list<juce::Component*> { &shape, &division, &sync, &retrig, &rate, &phase, &smooth })
        addAndMakeVisible (c);
}

void LfoEditor::refresh()
{
    const bool synced = proc.apvts.getRawParameterValue (pid::lfo (lfo, pid::lSync))->load() > 0.5f;
    division.setVisible (synced);
    rate.setEnabled (! synced);
    for (auto* k : { &rate, &phase, &smooth }) k->refreshMod();
    const float ph = proc.getModEngine().lfoPhaseDisplay[(size_t) lfo].load (std::memory_order_relaxed);
    if (std::abs (ph - phaseNow) > 0.002f) { phaseNow = ph; repaint (preview); }
}

void LfoEditor::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    auto top = r.removeFromTop (28);
    shape.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (8);
    sync.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (8);
    division.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (8);
    retrig.setBounds (top.removeFromLeft (120));
    r.removeFromTop (10);
    auto knobs = r.removeFromRight (240);
    rate.setBounds (knobs.getX(), knobs.getY() + 10, 76, 100);
    phase.setBounds (knobs.getX() + 80, knobs.getY() + 10, 76, 100);
    smooth.setBounds (knobs.getX() + 160, knobs.getY() + 10, 76, 100);
    preview = r.reduced (0, 4).withTrimmedRight (12);
}

void LfoEditor::paint (juce::Graphics& g)
{
    auto pr = preview.toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (pr, 5.0f);
    auto a = pr.reduced (10, 12);
    g.setColour (col::track.withAlpha (0.5f));
    g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
    const int shp = (int) proc.apvts.getRawParameterValue (pid::lfo (lfo, pid::lShape))->load();
    const float off = proc.apvts.getRawParameterValue (pid::lfo (lfo, pid::lPhase))->load() / 360.0f;
    juce::Path p;
    for (int i = 0; i <= 200; ++i)
    {
        const float t = (float) i / 200.0f;
        const float v = lfoPreview (shp, t + off);
        const juce::Point<float> pt (a.getX() + t * a.getWidth(), a.getCentreY() - v * a.getHeight() * 0.45f);
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    g.setColour (col::mod);
    g.strokePath (p, juce::PathStrokeType (2.0f));
    float local = phaseNow - off;
    local -= std::floor (local);
    g.setColour (col::text.withAlpha (0.6f));
    g.drawVerticalLine ((int) (a.getX() + local * a.getWidth()), a.getY(), a.getBottom());
}

// ===========================================================================
EnvEditor::EnvEditor (KlingeProcessor& p, int index)
    : proc (p), env (index),
      source (p.apvts, pid::env (index, pid::eSrc), { "Input", "Sidechain" },
              "What the follower listens to. Sidechain needs a signal routed to KLINGE's sidechain input in your host."),
      attack (p, pid::env (index, pid::eAtt), "Attack", Knob::Small, false, false, "How fast the follower rises."),
      release (p, pid::env (index, pid::eRel), "Release", Knob::Small, false, false, "How fast it falls back."),
      gain (p, pid::env (index, pid::eGain), "Sensitivity", Knob::Small, true, false, "Boosts the follower so quieter signals reach full range.")
{
    for (auto* c : std::initializer_list<juce::Component*> { &source, &attack, &release, &gain })
        addAndMakeVisible (c);
}

void EnvEditor::refresh()
{
    for (auto* k : { &attack, &release, &gain }) k->refreshMod();
    value = proc.getModEngine().display[(size_t) ModSource::Env1 + (size_t) env].load (std::memory_order_relaxed);
    history[(size_t) histPos] = value;
    histPos = (histPos + 1) % (int) history.size();
    repaint (meterArea);
}

void EnvEditor::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    source.setBounds (r.removeFromTop (28).withWidth (220));
    r.removeFromTop (10);
    auto knobs = r.removeFromRight (240);
    attack.setBounds (knobs.getX(), knobs.getY() + 10, 76, 100);
    release.setBounds (knobs.getX() + 80, knobs.getY() + 10, 76, 100);
    gain.setBounds (knobs.getX() + 160, knobs.getY() + 10, 76, 100);
    meterArea = r.reduced (0, 4).withTrimmedRight (12);
}

void EnvEditor::paint (juce::Graphics& g)
{
    auto pr = meterArea.toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (pr, 5.0f);
    auto a = pr.reduced (10, 12);
    juce::Path p;
    const int n = (int) history.size();
    for (int i = 0; i < n; ++i)
    {
        const float v = history[(size_t) ((histPos + i) % n)];
        const juce::Point<float> pt (a.getX() + (float) i / (float) (n - 1) * a.getWidth(), a.getBottom() - clampf (v, 0.0f, 1.0f) * a.getHeight());
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    g.setColour (col::mod);
    g.strokePath (p, juce::PathStrokeType (1.8f));
    const bool sc = proc.apvts.getRawParameterValue (pid::env (env, pid::eSrc))->load() > 0.5f;
    if (sc)
    {
        g.setFont (fonts::ui (12.0f, 0));
        g.setColour (col::textDim);
        g.drawText ("Route a track to KLINGE's sidechain input in your host.", a.removeFromTop (16), juce::Justification::centredLeft);
    }
}

// ===========================================================================
ShaperCanvas::ShaperCanvas (KlingeProcessor& p) : proc (p)
{
    setHelp (*this, "Drag points to shape the rhythm. Double-click adds or removes a point. Drag a line up or down to curve it. "
                    "Hold Shift to place points freely; otherwise they snap to 1/32 steps.");
}

juce::Rectangle<float> ShaperCanvas::plotArea() const { return getLocalBounds().toFloat().reduced (10, 12); }

juce::Point<float> ShaperCanvas::toScreen (const ShaperPoint& p) const
{
    const auto a = plotArea();
    return { a.getX() + p.x * a.getWidth(), a.getBottom() - p.y * a.getHeight() };
}

ShaperPoint ShaperCanvas::fromScreen (juce::Point<float> s) const
{
    const auto a = plotArea();
    return { clampf ((s.x - a.getX()) / a.getWidth(), 0.0f, 1.0f), clampf ((a.getBottom() - s.y) / a.getHeight(), 0.0f, 1.0f), 0.0f };
}

int ShaperCanvas::hitPoint (juce::Point<float> s) const
{
    for (int i = 0; i < (int) pts.size(); ++i)
        if (toScreen (pts[(size_t) i]).getDistanceFrom (s) < 8.0f) return i;
    return -1;
}

int ShaperCanvas::hitSegment (juce::Point<float> s) const
{
    const auto p = fromScreen (s);
    for (int i = 0; i + 1 < (int) pts.size(); ++i)
        if (p.x >= pts[(size_t) i].x && p.x <= pts[(size_t) i + 1].x && pts[(size_t) i + 1].x - pts[(size_t) i].x > 1.0e-4f)
        {
            const float y = ShaperCurve::evaluate (pts, p.x);
            if (std::abs (toScreen ({ p.x, y, 0 }).y - s.y) < 10.0f) return i;
        }
    return -1;
}

void ShaperCanvas::refresh()
{
    const int v = proc.shaper.version.load();
    if (v != lastVersion && dragging < 0 && curving < 0)
    {
        lastVersion = v;
        pts = proc.shaper.getPoints();
        repaint();
    }
    const float ph = proc.getModEngine().shaperPhaseDisplay.load (std::memory_order_relaxed);
    if (std::abs (ph - phase) > 0.001f) { phase = ph; repaint(); }
}

void ShaperCanvas::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (b, 5.0f);
    const auto a = plotArea();
    for (int i = 0; i <= 16; ++i)
    {
        g.setColour (col::track.withAlpha (i % 4 == 0 ? 0.6f : 0.25f));
        g.drawVerticalLine ((int) (a.getX() + a.getWidth() * (float) i / 16.0f), a.getY(), a.getBottom());
    }
    for (float y : { 0.25f, 0.5f, 0.75f })
    {
        g.setColour (col::track.withAlpha (0.25f));
        g.drawHorizontalLine ((int) (a.getBottom() - y * a.getHeight()), a.getX(), a.getRight());
    }
    juce::Path fill, line;
    for (int i = 0; i <= 400; ++i)
    {
        const float x = (float) i / 400.0f;
        const auto s = toScreen ({ x, ShaperCurve::evaluate (pts, x), 0 });
        if (i == 0) { line.startNewSubPath (s); fill.startNewSubPath (a.getX(), a.getBottom()); }
        else line.lineTo (s);
        fill.lineTo (s);
    }
    fill.lineTo (a.getRight(), a.getBottom());
    fill.closeSubPath();
    g.setColour (col::mod.withAlpha (0.14f));
    g.fillPath (fill);
    g.setColour (col::mod);
    g.strokePath (line, juce::PathStrokeType (2.0f));
    if (editable)
        for (int i = 0; i < (int) pts.size(); ++i)
        {
            const auto s = toScreen (pts[(size_t) i]);
            g.setColour (i == dragging || i == hover ? col::straw : col::text);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (s));
        }
    g.setColour (col::text.withAlpha (0.55f));
    g.drawVerticalLine ((int) (a.getX() + phase * a.getWidth()), a.getY(), a.getBottom());
}

void ShaperCanvas::mouseMove (const juce::MouseEvent& e)
{
    if (! editable) return;
    const int h = hitPoint (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor
                           : (hitSegment (e.position) >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor));
}

void ShaperCanvas::mouseDown (const juce::MouseEvent& e)
{
    if (! editable) return;
    dragging = hitPoint (e.position);
    curving = dragging < 0 ? hitSegment (e.position) : -1;
    if (curving >= 0) { curveStart = pts[(size_t) curving].curve; dragStartY = e.position.y; }
}

void ShaperCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging >= 0)
    {
        auto p = fromScreen (e.position);
        if (! e.mods.isShiftDown()) p.x = std::round (p.x * 32.0f) / 32.0f;
        auto& q = pts[(size_t) dragging];
        const float lo = dragging == 0 ? 0.0f : pts[(size_t) dragging - 1].x;
        const float hi = dragging == (int) pts.size() - 1 ? 1.0f : pts[(size_t) dragging + 1].x;
        q.x = (dragging == 0) ? 0.0f : (dragging == (int) pts.size() - 1 ? 1.0f : clampf (p.x, lo, hi));
        q.y = p.y;
        proc.shaper.setPoints (pts);
        lastVersion = proc.shaper.version.load();
        repaint();
    }
    else if (curving >= 0)
    {
        const float dy = (dragStartY - e.position.y) / 80.0f;
        const bool rising = pts[(size_t) curving + 1].y >= pts[(size_t) curving].y;
        pts[(size_t) curving].curve = clampf (curveStart + (rising ? -dy : dy), -1.0f, 1.0f);
        proc.shaper.setPoints (pts);
        lastVersion = proc.shaper.version.load();
        repaint();
    }
}

void ShaperCanvas::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0 || curving >= 0) proc.session.markUserEdit();
    dragging = curving = -1;
    pts = proc.shaper.getPoints();
    repaint();
}

void ShaperCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! editable) return;
    const int h = hitPoint (e.position);
    if (h > 0 && h < (int) pts.size() - 1)
        pts.erase (pts.begin() + h);
    else if (h < 0)
    {
        auto p = fromScreen (e.position);
        if (! e.mods.isShiftDown()) p.x = std::round (p.x * 32.0f) / 32.0f;
        pts.push_back (p);
    }
    proc.shaper.setPoints (pts);
    pts = proc.shaper.getPoints();
    lastVersion = proc.shaper.version.load();
    proc.session.markUserEdit();
    repaint();
}

// ===========================================================================
ShaperEditor::ShaperEditor (KlingeProcessor& p)
    : canvas (p), proc (p),
      rate (p.apvts, pid::shpRate, "Length of one shaper cycle in note values. It follows the host position."),
      amount (p, pid::shpAmount, "Amount", Knob::Small, false, false, "How strongly the curve shapes the volume of the processed sound."),
      smooth (p, pid::shpSmooth, "Smooth", Knob::Small, false, false, "Softens hard steps so gates do not click."),
      phase (p, pid::shpPhase, "Offset", Knob::Small, false, false, "Moves the pattern against the beat."),
      retrig (p.apvts, pid::shpRetrig, "MIDI retrig", "Restarts the pattern on every MIDI note instead of following the host.")
{
    presets.addItemList (ShaperCurve::presetNames(), 1);
    presets.setTextWhenNothingSelected ("Patterns");
    presets.onChange = [this]
    {
        const int i = presets.getSelectedItemIndex();
        if (i >= 0) { proc.shaper.setPoints (ShaperCurve::preset (i)); proc.session.markUserEdit(); }
        presets.setSelectedId (0, juce::dontSendNotification);
    };
    setHelp (presets, "Start from a ready-made pattern, then edit it.");
    for (auto* c : std::initializer_list<juce::Component*> { &canvas, &presets, &rate, &amount, &smooth, &phase, &retrig })
        addAndMakeVisible (c);
}

void ShaperEditor::refresh()
{
    canvas.refresh();
    for (auto* k : { &amount, &smooth, &phase }) k->refreshMod();
}

void ShaperEditor::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    auto top = r.removeFromTop (28);
    presets.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (8);
    rate.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (8);
    retrig.setBounds (top.removeFromLeft (120));
    r.removeFromTop (10);
    auto knobs = r.removeFromRight (84);
    amount.setBounds (knobs.removeFromTop (100));
    smooth.setBounds (knobs.removeFromTop (100));
    phase.setBounds (knobs.removeFromTop (100));
    canvas.setBounds (r.withTrimmedRight (10));
}

void ShaperEditor::paint (juce::Graphics&) {}

// ===========================================================================
MatrixList::Row::Row (MatrixList& o, int s) : owner (o), slot (s)
{
    depth.setSliderStyle (juce::Slider::LinearBar);
    depth.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    depth.setRange (-100.0, 100.0, 0.1);
    depth.setDoubleClickReturnValue (true, 25.0);
    depth.setColour (juce::Slider::trackColourId, col::mod.withAlpha (0.55f));
    depth.setColour (juce::Slider::backgroundColourId, col::well);
    depth.setColour (juce::Slider::textBoxTextColourId, col::text);
    depth.setValue (owner.proc.modMatrix.getSlot (slot).depth * 100.0, juce::dontSendNotification);
    depth.onValueChange = [this]
    {
        auto d = owner.proc.modMatrix.getSlot (slot);
        d.depth = (float) depth.getValue() / 100.0f;
        owner.proc.modMatrix.setSlot (slot, d);
        owner.lastVersion = owner.proc.modMatrix.version.load();
        repaint();
    };
    depth.onDragEnd = [this] { owner.proc.session.markUserEdit(); };
    setHelp (depth, "Modulation depth. Negative values move the parameter the other way. Double-click resets to +25 %.");
    remove.onClick = [this]
    {
        owner.proc.modMatrix.setSlot (slot, {});
        owner.proc.session.markUserEdit();
    };
    addAndMakeVisible (depth);
    addAndMakeVisible (remove);
}

void MatrixList::Row::resized()
{
    auto r = getLocalBounds().reduced (0, 3);
    remove.setBounds (r.removeFromRight (24));
    r.removeFromRight (8);
    depth.setBounds (r.removeFromRight (180));
}

void MatrixList::Row::paint (juce::Graphics& g)
{
    const auto d = owner.proc.modMatrix.getSlot (slot);
    auto r = getLocalBounds().withTrimmedRight (220);
    g.setFont (fonts::ui (13.0f, 1));
    g.setColour (col::mod);
    g.drawText (modSourceNames()[d.source], r.removeFromLeft (90), juce::Justification::centredLeft);
    g.setColour (col::text);
    g.drawText (owner.targetName (d.target), r, juce::Justification::centredLeft);
    g.setFont (fonts::mono (11.5f));
    g.setColour (col::textDim);
    const int pct = juce::roundToInt (d.depth * 100.0f);
    g.drawText ((pct > 0 ? "+" : "") + juce::String (pct) + " %", getLocalBounds().withTrimmedRight (32).removeFromRight (180),
                juce::Justification::centred);
}

MatrixList::MatrixList (KlingeProcessor& p) : proc (p)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

juce::String MatrixList::targetName (const juce::String& id) const
{
    if (auto* prm = proc.apvts.getParameter (id)) return prm->getName (40);
    return id;
}

void MatrixList::rebuild()
{
    rows.clear();
    for (int i = 0; i < numModSlots; ++i)
        if (! proc.modMatrix.getSlot (i).isEmpty())
            content.addAndMakeVisible (rows.add (new Row (*this, i)));
    resized();
    repaint();
}

void MatrixList::refresh()
{
    const int v = proc.modMatrix.version.load();
    if (v != lastVersion) { lastVersion = v; rebuild(); }
}

void MatrixList::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (30);
    viewport.setBounds (r);
    const int w = r.getWidth() - 10;
    content.setSize (w, std::max (r.getHeight(), rows.size() * 30));
    for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, i * 30, w, 30);
}

void MatrixList::paint (juce::Graphics& g)
{
    g.setFont (fonts::ui (13.5f, 2));
    g.setColour (col::text);
    g.drawText ("Active modulations", 0, 0, 300, 24, juce::Justification::centredLeft);
    g.setFont (fonts::ui (12.5f, 0));
    g.setColour (col::textFaint);
    g.drawText (juce::String (rows.size()) + " of " + juce::String (numModSlots), getLocalBounds().removeFromTop (24),
                juce::Justification::centredRight);
    if (rows.isEmpty())
    {
        g.setColour (col::textDim);
        g.setFont (fonts::ui (13.5f, 0));
        g.drawFittedText ("Nothing is modulated yet. Drag a source from the top row onto any knob, or right-click a knob and choose "
                          "Modulate with.", getLocalBounds().withTrimmedTop (40).reduced (0, 10), juce::Justification::topLeft, 3);
    }
}

// ===========================================================================
ModulationTab::ModulationTab (KlingeProcessor& p) : proc (p)
{
    for (int s = 1; s < numModSources; ++s)
        addAndMakeVisible (chips.add (new SourceChip (p, s, [this] (int src) { select (src); })));
    for (auto* c : std::initializer_list<juce::Component*> { &lfo1, &lfo2, &env1, &env2, &shaper, &matrix })
        addChildComponent (c);
    matrix.setVisible (true);
    select (juce::jlimit (1, numModSources - 1, p.ui.modSource.load()));
}

void ModulationTab::select (int source)
{
    selected = source;
    proc.ui.modSource.store (source);
    for (auto* c : chips) c->setSelected (false);
    if (source - 1 < chips.size()) chips[source - 1]->setSelected (true);
    lfo1.setVisible (source == (int) ModSource::Lfo1);
    lfo2.setVisible (source == (int) ModSource::Lfo2);
    env1.setVisible (source == (int) ModSource::Env1);
    env2.setVisible (source == (int) ModSource::Env2);
    shaper.setVisible (source == (int) ModSource::Shaper);
    repaint();
}

void ModulationTab::refresh()
{
    for (auto* c : chips) c->refresh();
    if (lfo1.isVisible()) lfo1.refresh();
    if (lfo2.isVisible()) lfo2.refresh();
    if (env1.isVisible()) env1.refresh();
    if (env2.isVisible()) env2.refresh();
    if (shaper.isVisible()) shaper.refresh();
    matrix.refresh();
}

void ModulationTab::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto top = r.removeFromTop (40);
    const int w = (top.getWidth() - 6 * 6) / 7;
    for (auto* c : chips) { c->setBounds (top.removeFromLeft (w)); top.removeFromLeft (6); }
    r.removeFromTop (8);
    auto editor = r.removeFromTop (340);
    for (auto* c : std::initializer_list<juce::Component*> { &lfo1, &lfo2, &env1, &env2, &shaper }) c->setBounds (editor);
    r.removeFromTop (10);
    matrix.setBounds (r);
}

void ModulationTab::paint (juce::Graphics& g)
{
    if (selected == (int) ModSource::Velocity || selected == (int) ModSource::ModWheel)
    {
        auto r = getLocalBounds().reduced (24, 10).withTrimmedTop (60).withHeight (120);
        g.setColour (col::textDim);
        g.setFont (fonts::ui (14.0f, 0));
        g.drawFittedText (selected == (int) ModSource::Velocity
                              ? "Velocity follows the last MIDI note that reaches KLINGE. Route a MIDI track to it in your host, "
                                "then drag Velocity onto any knob."
                              : "Mod wheel follows MIDI controller 1. Route a MIDI track or controller to KLINGE in your host, "
                                "then drag Mod wheel onto any knob.",
                          r, juce::Justification::topLeft, 4);
    }
}

} // namespace klinge::ui
