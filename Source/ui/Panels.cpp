#include "Panels.h"

namespace klinge::ui
{
namespace help
{
    const char* drive     = "How hard the signal hits the distortion. Level stays matched, so you hear character, not volume.";
    const char* bandDrive = "Drive offset for this band, added to the main Drive.";
    const char* focus     = "Tilts the sound before the distortion and back afterwards. Positive drives the highs harder, negative the lows. "
                            "The tonal balance stays the same.";
    const char* punch     = "Puts the dry signal's micro-dynamics back onto the distorted sound. Above 100 % it exaggerates them.";
    const char* speed     = "How quickly Punch follows the dry signal. Short for tight transients, long for groove and swell.";
    const char* dyn       = "Lets the input level move the drive. Positive: hits distort more. Negative: tails and quiet parts distort more.";
    const char* faithful  = "Keeps the low, mid and high balance of the original while you distort.";
    const char* mix       = "Blend between original and processed sound. Phase-aligned, so parallel blending stays clean.";
    const char* output    = "Output level.";
    const char* input     = "Input level into KLINGE. Hotter input means more distortion.";
    const char* preHP     = "Removes lows before the distortion, so they stay clean.";
    const char* preLP     = "Removes highs before the distortion, so fizz is not generated in the first place.";
    const char* postHP    = "Low cut after the distortion.";
    const char* postLP    = "High cut after the distortion. Works like a speaker cabinet and takes off fizz.";
    const char* clip      = "Soft-limits the output just below 0 dBFS.";
    const char* osQ       = "Oversampling filter. Linear phase keeps parallel mixing clean. Low latency suits live playing.";
    const char* render    = "Oversampling used when the host renders or bounces offline. Higher is cleaner and costs only render time.";
    const char* routing   = "Single: one distortion. Multiband: up to five bands, each with its own distortion. "
                            "Mid/Side: separate distortion for the centre and the sides.";
    const char* count     = "Number of bands in Multiband mode.";
    const char* bias      = "Makes the curve asymmetric. Adds even harmonics.";
    const char* harm      = "How much of the harmonic designer this band receives.";
    const char* bmix      = "Blend between this band's clean and distorted signal.";
    const char* level     = "Level of this band after the distortion.";
    const char* sag       = "Loud passages pull this band's gain down and shift its bias, like a sagging power supply. Hits bloom and recover.";
    const char* bdyn      = "Band dynamics. Left compresses above the threshold (up to 8:1), right expands below it, "
                            "which brings back the movement that distortion flattens.";
    const char* thresh    = "Threshold for the band dynamics.";
    const char* dynPos    = "Before: the dynamics shape what goes into the distortion. After: they shape the distorted sound.";
    const char* stereo    = "Which part of the stereo image this band distorts. Mid or Side leaves the other part clean.";
    const char* lock      = "100 %: harmonic ratios stay the same at any level. 0 %: harmonics grow with level, like analogue gear.";
    const char* bandOn    = "Switches this band's distortion on or off. Off passes the band through clean.";
    const char* solo      = "Listen to this band alone.";
    const char* mute      = "Silence this band.";
    const char* fbAmount  = "Feeds the distorted sound back into the input through a short delay. Low settings thicken, "
                            "high settings ring and resonate.";
    const char* fbTime    = "Delay of the feedback loop. Short times act like a resonant body, longer ones like a metallic comb.";
    const char* fbTone    = "Low-pass filter inside the feedback loop. Lower values keep the feedback dark and controlled.";
    const char* analog    = "Analogue imperfections: slightly different left and right channels, drift of the operating point and "
                            "signal-dependent noise.";
}

// ===========================================================================
// helpers
// ===========================================================================
int activeBandCount (int routing, int multibandCount)
{
    return routing == (int) Routing::Single ? 1 : (routing == (int) Routing::MidSide ? 2 : juce::jlimit (2, numBands, multibandCount));
}

juce::String bandName (int routing, int count, int b)
{
    if (routing == (int) Routing::Single) return b == 0 ? "Main" : "Band " + juce::String (b + 1);
    if (routing == (int) Routing::MidSide) return b == 0 ? "Mid" : (b == 1 ? "Side" : "Band " + juce::String (b + 1));
    static const char* names[4][5] = { { "Low", "High", "", "", "" },
                                       { "Low", "Mid", "High", "", "" },
                                       { "Low", "Low mid", "High mid", "High", "" },
                                       { "Low", "Low mid", "Mid", "High mid", "High" } };
    count = juce::jlimit (2, numBands, count);
    return b < count ? juce::String (names[count - 2][b]) : "Band " + juce::String (b + 1);
}

int currentRouting (KlingeProcessor& p)
{
    return juce::jlimit (0, 2, (int) p.apvts.getRawParameterValue (pid::routing)->load());
}

int currentBandCount (KlingeProcessor& p)
{
    return activeBandCount (currentRouting (p), 2 + (int) p.apvts.getRawParameterValue (pid::numBands)->load());
}

void collectKnobs (juce::Component& root, std::vector<Knob*>& out)
{
    for (auto* c : root.getChildren())
    {
        if (auto* k = dynamic_cast<Knob*> (c)) out.push_back (k);
        collectKnobs (*c, out);
    }
}

static int bandAlgo (KlingeProcessor& p, int band)
{
    return juce::jlimit (0, numAlgos - 1, (int) p.apvts.getRawParameterValue (pid::band (band, pid::bType))->load());
}

static void setKnobHelp (Knob& k, const juce::String& h)
{
    const auto full = h + "  Right-click for modulation and lock.";
    setHelp (k, full);
    setHelp (k.slider, full);
}

static const char* groupName (int displayIndex)
{
    return displayIndex == 0 ? "Warm" : (displayIndex == 6 ? "Clip" : (displayIndex == 12 ? "Creative" : nullptr));
}

// ===========================================================================
// AlgoBox
// ===========================================================================
AlgoBox::AlgoBox (KlingeProcessor& p, int band)
{
    const auto& order = algoDisplayOrder();
    for (int i = 0; i < numAlgos; ++i)
    {
        if (auto* g = groupName (i)) addSectionHeading (g);
        const int a = order[(size_t) i];
        addItem (algoNames()[a], a + 1);
    }
    setHelp (*this, "Distortion type for this band. Circuit models (Triode, Diode, Hysteresis, Iron, Stack) react to the "
                    "signal's history, the others are instant curves.");
    if (auto* prm = p.apvts.getParameter (pid::band (band, pid::bType)))
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*prm, [this] (float v)
        {
            setSelectedId (juce::jlimit (0, numAlgos - 1, juce::roundToInt (v)) + 1, juce::dontSendNotification);
        });
        attachment->sendInitialUpdate();
    }
    onChange = [this]
    {
        const int id = getSelectedId();
        if (id > 0 && attachment) attachment->setValueAsCompleteGesture ((float) (id - 1));
    };
}

// ===========================================================================
// AlgoList
// ===========================================================================
AlgoList::AlgoList (KlingeProcessor& p)
{
    setHelp (*this, "Choose the distortion type. The Character knob changes meaning with each type.");
    if (auto* prm = p.apvts.getParameter (pid::band (0, pid::bType)))
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*prm, [this] (float v)
        {
            selected = juce::jlimit (0, numAlgos - 1, juce::roundToInt (v));
            repaint();
        });
        attachment->sendInitialUpdate();
    }
}

std::vector<AlgoList::Row> AlgoList::layoutRows() const
{
    constexpr int headH = 22, descH = 92;
    const int rowH = juce::jlimit (20, 40, (getHeight() - descH - 3 * headH) / numAlgos);
    std::vector<Row> rows;
    int y = 0;
    for (int i = 0; i < numAlgos; ++i)
    {
        if (auto* g = groupName (i))
        {
            rows.push_back ({ -1, g, { 0, y, getWidth(), headH } });
            y += headH;
        }
        rows.push_back ({ algoDisplayOrder()[(size_t) i], {}, { 0, y, getWidth(), rowH } });
        y += rowH;
    }
    return rows;
}

int AlgoList::algoAt (juce::Point<int> pt) const
{
    for (auto& r : layoutRows())
        if (r.algo >= 0 && r.bounds.contains (pt)) return r.algo;
    return -1;
}

void AlgoList::mouseDown (const juce::MouseEvent& e)
{
    const int a = algoAt (e.getPosition());
    if (a >= 0 && attachment) attachment->setValueAsCompleteGesture ((float) a);
}

void AlgoList::mouseMove (const juce::MouseEvent& e)
{
    const int a = algoAt (e.getPosition());
    if (a != hover) { hover = a; repaint(); }
}

void AlgoList::mouseExit (const juce::MouseEvent&) { hover = -1; repaint(); }

void AlgoList::paint (juce::Graphics& g)
{
    const auto rows = layoutRows();
    int displayIndex = 0, bottom = 0;
    for (auto& row : rows)
    {
        bottom = row.bounds.getBottom();
        if (row.algo < 0)
        {
            g.setFont (fonts::ui (11.5f, 2));
            g.setColour (col::textFaint);
            g.drawText (row.heading.toUpperCase(), row.bounds.withTrimmedLeft (6).withTrimmedTop (6), juce::Justification::centredLeft);
            continue;
        }
        auto r = row.bounds.toFloat().reduced (0, 1.0f);
        const auto a = (Algo) row.algo;
        const bool sel = row.algo == selected;
        if (sel || row.algo == hover)
        {
            g.setColour (sel ? col::well : col::panelEdge.withAlpha (0.6f));
            g.fillRoundedRectangle (r, 4.0f);
        }
        const auto heat = temper ((float) displayIndex / (float) (numAlgos - 1));
        ++displayIndex;
        g.setColour (sel ? heat : heat.withAlpha (0.35f));
        g.fillRoundedRectangle (r.withWidth (3.0f).reduced (0, 5.0f).translated (6.0f, 0), 1.5f);

        g.setFont (fonts::ui (14.5f, sel ? 2 : 1));
        g.setColour (sel ? col::text : col::textDim);
        g.drawText (algoNames()[row.algo], r.withTrimmedLeft (20).toNearestInt(), juce::Justification::centredLeft);

        g.setFont (fonts::ui (12.0f, 0));
        g.setColour (sel ? col::textDim : col::textFaint);
        g.drawText (shapeLabel (a), r.withTrimmedRight (10).toNearestInt(), juce::Justification::centredRight);
    }

    auto desc = getLocalBounds().withTop (bottom + 10).reduced (6, 0);
    g.setColour (col::textDim);
    g.setFont (fonts::ui (13.0f, 0));
    g.drawFittedText (algoDescription ((Algo) selected), desc, juce::Justification::topLeft, 5, 1.0f);
}

// ===========================================================================
// CrossoverStrip
// ===========================================================================
CrossoverStrip::CrossoverStrip (KlingeProcessor& p) : proc (p)
{
    const char* ids[4] = { pid::xoverLo, pid::xoverHi, pid::xover3, pid::xover4 };
    for (int i = 0; i < 4; ++i)
        if (auto* prm = p.apvts.getParameter (ids[i]))
        {
            att[(size_t) i] = std::make_unique<juce::ParameterAttachment> (*prm, [this, i] (float v) { hz[(size_t) i] = v; repaint(); });
            att[(size_t) i]->sendInitialUpdate();
        }
    setHelp (*this, "Drag a split point to move it. Click a band to show its settings.");
}

float CrossoverStrip::xForFreq (float f) const
{
    const float t = std::log (f / 20.0f) / std::log (1000.0f);
    return 8.0f + t * ((float) getWidth() - 16.0f);
}

float CrossoverStrip::freqForX (float x) const
{
    const float t = clampf ((x - 8.0f) / ((float) getWidth() - 16.0f), 0.0f, 1.0f);
    return 20.0f * std::pow (1000.0f, t);
}

void CrossoverStrip::refresh()
{
    const int r = currentRouting (proc), c = currentBandCount (proc);
    if (r != routing || c != count) { routing = r; count = c; repaint(); }
}

// effective split frequencies, the same rule the engine applies
static std::array<float, 4> effectiveSplits (const std::array<float, 4>& hz)
{
    std::array<float, 4> f {};
    float prev = 0.0f;
    for (size_t s = 0; s < 4; ++s)
    {
        f[s] = clampf (hz[s], 30.0f, 16000.0f);
        if (s > 0) f[s] = std::max (f[s], prev * 1.2f);
        prev = f[s];
    }
    return f;
}

int CrossoverStrip::splitAt (float x) const
{
    if (routing != (int) Routing::Multiband) return -1;
    const auto f = effectiveSplits (hz);
    int best = -1;
    float bestD = 10.0f;
    for (int s = 0; s < count - 1; ++s)
    {
        const float d = std::abs (x - xForFreq (f[(size_t) s]));
        if (d < bestD) { bestD = d; best = s; }
    }
    return best;
}

void CrossoverStrip::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::well);
    g.fillRoundedRectangle (b, 5.0f);
    g.setFont (fonts::ui (13.0f, 1));

    if (routing != (int) Routing::Multiband)
    {
        const int n = routing == (int) Routing::Single ? 1 : 2;
        for (int i = 0; i < n; ++i)
        {
            auto reg = b.withWidth (b.getWidth() / (float) n).translated (b.getWidth() / (float) n * (float) i, 0);
            if (i == selectedBand)
            {
                g.setColour (col::panelEdge.withAlpha (0.8f));
                g.fillRoundedRectangle (reg.reduced (3), 4.0f);
            }
            g.setColour (i == selectedBand ? col::straw : col::textDim);
            g.drawText (n == 1 ? juce::String ("Full range, one distortion") : (i == 0 ? "Mid  (centre)" : "Side  (stereo width)"),
                        reg, juce::Justification::centred);
        }
        if (n == 2)
        {
            g.setColour (col::track);
            g.drawVerticalLine ((int) b.getCentreX(), b.getY() + 8, b.getBottom() - 8);
        }
        return;
    }

    const auto f = effectiveSplits (hz);
    std::array<float, numBands + 1> edges {};
    edges[0] = b.getX();
    for (int s = 0; s < count - 1; ++s) edges[(size_t) s + 1] = xForFreq (f[(size_t) s]);
    edges[(size_t) count] = b.getRight();
    for (int i = 0; i < count; ++i)
    {
        auto reg = juce::Rectangle<float> (edges[(size_t) i], b.getY(), edges[(size_t) i + 1] - edges[(size_t) i], b.getHeight());
        g.setColour (temper ((float) i / (float) (numBands - 1)).withAlpha (i == selectedBand ? 0.22f : 0.08f));
        g.fillRect (reg.reduced (0, 1));
        g.setColour (i == selectedBand ? col::straw : col::textDim);
        g.setFont (fonts::ui (12.5f, i == selectedBand ? 2 : 1));
        g.drawFittedText (bandName (routing, count, i), reg.withTrimmedTop (b.getHeight() * 0.42f).toNearestInt(),
                          juce::Justification::centred, 1, 0.7f);
    }
    g.setFont (fonts::mono (11.0f));
    for (int s = 0; s < count - 1; ++s)
    {
        const float x = edges[(size_t) s + 1];
        const float fr = f[(size_t) s];
        const bool hot = dragging == s || hover == s;
        g.setColour (hot ? col::straw : col::gold);
        g.fillRect (juce::Rectangle<float> (hot ? 3.0f : 2.0f, b.getHeight() - 6).withCentre ({ x, b.getCentreY() }));
        const juce::String txt = fr >= 1000 ? juce::String (fr / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (fr));
        auto tr = juce::Rectangle<float> (44, 16).withCentre ({ x, b.getY() + 11 });
        tr = tr.withX (clampf (tr.getX(), b.getX() + 2, b.getRight() - 46));
        g.setColour (col::text);
        g.drawText (txt, tr, juce::Justification::centred);
    }
}

void CrossoverStrip::mouseMove (const juce::MouseEvent& e)
{
    const int h = splitAt ((float) e.x);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
}

void CrossoverStrip::mouseExit (const juce::MouseEvent&)
{
    if (hover >= 0 && dragging < 0) { hover = -1; repaint(); }
}

void CrossoverStrip::mouseDown (const juce::MouseEvent& e)
{
    mouseMove (e);
    dragging = hover;
    if (dragging >= 0)
    {
        if (att[(size_t) dragging]) att[(size_t) dragging]->beginGesture();
        return;
    }
    // click on a region selects that band
    int band = 0;
    if (routing == (int) Routing::Multiband)
    {
        const auto f = effectiveSplits (hz);
        for (int s = 0; s < count - 1; ++s)
            if ((float) e.x > xForFreq (f[(size_t) s])) band = s + 1;
    }
    else if (routing == (int) Routing::MidSide)
        band = e.x > getWidth() / 2 ? 1 : 0;
    if (onSelectBand) onSelectBand (band);
}

void CrossoverStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0 || ! att[(size_t) dragging]) return;
    const auto f = effectiveSplits (hz);
    const float lo = dragging > 0 ? f[(size_t) dragging - 1] * 1.25f : 30.0f;
    const float hi = dragging < count - 2 ? f[(size_t) dragging + 1] / 1.25f : 16000.0f;
    att[(size_t) dragging]->setValueAsPartOfGesture (clampf (freqForX ((float) e.x), lo, std::max (lo, hi)));
}

void CrossoverStrip::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0 && att[(size_t) dragging]) att[(size_t) dragging]->endGesture();
    dragging = -1;
    repaint();
}

// ===========================================================================
// BandColumn
// ===========================================================================
BandColumn::BandColumn (KlingeProcessor& p, int b, std::function<void (int)> sel)
    : proc (p), band (b), onSelect (std::move (sel)),
      on (p.apvts, pid::band (b, pid::bOn), "On", help::bandOn),
      solo (p.apvts, pid::band (b, pid::bSolo), "S", help::solo),
      mute (p.apvts, pid::band (b, pid::bMute), "M", help::mute),
      type (p, b),
      drive (p, pid::band (b, pid::bDrive), "Drive", Knob::Small, true, false, help::bandDrive),
      shape (p, pid::band (b, pid::bShape), "Shape", Knob::Small),
      mix (p, pid::band (b, pid::bMix), "Mix", Knob::Small, false, false, help::bmix),
      level (p, pid::band (b, pid::bLevel), "Level", Knob::Small, true, false, help::level)
{
    for (auto* c : std::initializer_list<juce::Component*> { &on, &solo, &mute, &type, &drive, &shape, &mix, &level })
        addAndMakeVisible (c);
}

void BandColumn::update (const juce::String& t, bool a, bool s, float pk)
{
    if (t != title || a != active || s != selected)
    {
        title = t; active = a; selected = s;
        setAlpha (active ? 1.0f : 0.35f);
        repaint();
    }
    if (std::abs (pk - peakDb) > 0.25f) { peakDb = pk; repaint (0, 22, getWidth(), 6); }
    const int algo = bandAlgo (proc, band);
    if (algo != lastAlgo)
    {
        lastAlgo = algo;
        shape.setLabel (shapeLabel ((Algo) algo));
        setKnobHelp (shape, juce::String (shapeLabel ((Algo) algo)) + ": " + algoDescription ((Algo) algo));
    }
}

void BandColumn::mouseDown (const juce::MouseEvent& e)
{
    if (e.y < 28 && onSelect) onSelect (band);
}

void BandColumn::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (selected ? col::well.withAlpha (0.8f) : col::well.withAlpha (0.35f));
    g.fillRoundedRectangle (r, 6.0f);
    if (selected)
    {
        g.setColour (col::gold.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }
    auto head = r.removeFromTop (24).reduced (8, 0);
    g.setFont (fonts::ui (13.5f, 2));
    g.setColour (selected ? col::straw : col::text);
    g.drawFittedText (title, head.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

    // activity bar
    auto bar = juce::Rectangle<float> (8.0f, 23.0f, r.getWidth() - 16.0f, 3.0f);
    g.setColour (col::panelEdge);
    g.fillRoundedRectangle (bar, 1.5f);
    const float t = clampf ((peakDb + 48.0f) / 54.0f, 0.0f, 1.0f);
    if (active && t > 0.0f)
    {
        g.setColour (temper ((float) band / (float) (numBands - 1)));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * t), 1.5f);
    }
}

void BandColumn::resized()
{
    auto r = getLocalBounds().reduced (5, 0);
    r.removeFromTop (30);
    auto chips = r.removeFromTop (22);
    const int cw = (chips.getWidth() - 6) / 3;
    on.setBounds (chips.removeFromLeft (cw + 6)); chips.removeFromLeft (3);
    solo.setBounds (chips.removeFromLeft (cw - 3)); chips.removeFromLeft (3);
    mute.setBounds (chips);
    r.removeFromTop (5);
    type.setBounds (r.removeFromTop (24));
    r.removeFromTop (4);
    const int kh = r.getHeight() / 2, kw = r.getWidth() / 2;
    Knob* ks[] = { &drive, &shape, &mix, &level };
    for (int i = 0; i < 4; ++i)
        ks[i]->setBounds (r.getX() + (i % 2) * kw, r.getY() + (i / 2) * kh, kw, kh);
}

// ===========================================================================
// BandDetail
// ===========================================================================
BandDetail::BandDetail (KlingeProcessor& p, int b)
    : proc (p), band (b),
      shape (p, pid::band (b, pid::bShape), "Shape", Knob::Small),
      bias (p, pid::band (b, pid::bBias), "Bias", Knob::Small, true, false, help::bias),
      harm (p, pid::band (b, pid::bHarm), "Harmonics", Knob::Small, false, false, help::harm),
      sag (p, pid::band (b, pid::bSag), "Sag", Knob::Small, false, false, help::sag),
      dyn (p, pid::band (b, pid::bDyn), "Dynamics", Knob::Small, true, false, help::bdyn),
      thresh (p, pid::band (b, pid::bThresh), "Threshold", Knob::Small, false, false, help::thresh),
      stereo (p.apvts, pid::band (b, pid::bStereo), { "Stereo", "Mid", "Side" }, help::stereo),
      dynPos (p.apvts, pid::band (b, pid::bDynPos), { "Before", "After" }, help::dynPos)
{
    for (auto* c : std::initializer_list<juce::Component*> { &shape, &bias, &harm, &sag, &dyn, &thresh, &stereo, &dynPos })
        addAndMakeVisible (c);
}

void BandDetail::update (const juce::String& t)
{
    if (t != title) { title = t; repaint(); }
    const int algo = bandAlgo (proc, band);
    if (algo != lastAlgo)
    {
        lastAlgo = algo;
        shape.setLabel (shapeLabel ((Algo) algo));
        setKnobHelp (shape, juce::String (shapeLabel ((Algo) algo)) + ": " + algoDescription ((Algo) algo));
        repaint();
    }
}

void BandDetail::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (col::well.withAlpha (0.55f));
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    auto head = r.removeFromTop (30).reduced (12, 0);
    g.setFont (fonts::ui (14.0f, 2));
    g.setColour (col::straw);
    const auto t = title + "  /  " + algoNames()[juce::jmax (0, lastAlgo)];
    g.drawText (t, head, juce::Justification::centredLeft);

    g.setFont (fonts::ui (12.0f, 1));
    g.setColour (col::textDim);
    g.drawText ("Stereo", stereo.getBounds().withY (stereo.getY() - 18).withHeight (16), juce::Justification::centredLeft);
    g.drawText ("Dynamics position", dynPos.getBounds().withY (dynPos.getY() - 18).withHeight (16).withWidth (200),
                juce::Justification::centredLeft);

    g.setFont (fonts::ui (12.5f, 0));
    g.setColour (col::textFaint);
    auto desc = getLocalBounds().reduced (12, 0).withTop (stereo.getBottom() + 8).withTrimmedBottom (6);
    g.drawFittedText (algoDescription ((Algo) juce::jmax (0, lastAlgo)), desc, juce::Justification::topLeft, 2, 1.0f);

    // divider between the tone and the dynamics group
    g.setColour (col::panelEdge);
    const int x = dyn.getX() - 4;
    g.drawVerticalLine (x, 36.0f, (float) stereo.getBottom());
}

void BandDetail::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromTop (30);
    auto knobs = r.removeFromTop (100);
    const int w = knobs.getWidth() / 6;
    for (auto* k : { &shape, &bias, &harm, &sag, &dyn, &thresh }) k->setBounds (knobs.removeFromLeft (w));
    r.removeFromTop (22);
    auto row = r.removeFromTop (26);
    stereo.setBounds (row.getX() + 6, row.getY(), w * 3, 26);
    dynPos.setBounds (dyn.getX() + 8, row.getY(), w * 2 - 16, 26);
}

// ===========================================================================
// BandsTab
// ===========================================================================
BandsTab::BandsTab (KlingeProcessor& p)
    : proc (p),
      routing (p.apvts, pid::routing, { "Single", "Multiband", "Mid / Side" }, help::routing),
      count (p.apvts, pid::numBands, { "2", "3", "4", "5" }, help::count),
      strip (p)
{
    addAndMakeVisible (routing);
    addAndMakeVisible (count);
    addAndMakeVisible (strip);
    strip.onSelectBand = [this] (int b) { selectBand (b); };
    for (int b = 0; b < numBands; ++b)
    {
        columns[(size_t) b] = std::make_unique<BandColumn> (p, b, [this] (int i) { selectBand (i); });
        addAndMakeVisible (*columns[(size_t) b]);
        details[(size_t) b] = std::make_unique<BandDetail> (p, b);
        addChildComponent (*details[(size_t) b]);
    }
    peakDb.fill (-100.0f);
    selectBand (p.ui.band.load());
}

void BandsTab::selectBand (int b)
{
    const int n = currentBandCount (proc);
    selected = juce::jlimit (0, n - 1, b);
    proc.ui.band.store (selected);
    for (int i = 0; i < numBands; ++i) details[(size_t) i]->setVisible (i == selected);
    strip.selectedBand = selected;
    strip.repaint();
    if (onBandSelected) onBandSelected (selected);
}

void BandsTab::refresh()
{
    const int r = currentRouting (proc);
    const int mbCount = 2 + (int) proc.apvts.getRawParameterValue (pid::numBands)->load();
    const int n = activeBandCount (r, mbCount);
    if (selected >= n) selectBand (0);

    const bool mb = r == (int) Routing::Multiband;
    if (count.isEnabled() != mb) { count.setEnabled (mb); count.setAlpha (mb ? 1.0f : 0.4f); }

    auto& m = proc.getMeters();
    for (int b = 0; b < numBands; ++b)
    {
        const float d = gainToDb (m.bandPeak[(size_t) b].exchange (0.0f));
        peakDb[(size_t) b] = d > peakDb[(size_t) b] ? d : std::max (d, peakDb[(size_t) b] - 1.5f);
        columns[(size_t) b]->update (bandName (r, mbCount, b), b < n, b == selected, peakDb[(size_t) b]);
    }
    details[(size_t) selected]->update (bandName (r, mbCount, selected));
    strip.refresh();
}

void BandsTab::paint (juce::Graphics& g)
{
    g.setFont (fonts::ui (12.5f, 1));
    g.setColour (col::textDim);
    g.drawText ("Bands", count.getX() - 52, count.getY(), 46, count.getHeight(), juce::Justification::centredRight);
}

void BandsTab::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto top = r.removeFromTop (28);
    routing.setBounds (top.removeFromLeft (300));
    count.setBounds (top.removeFromRight (176));
    r.removeFromTop (8);
    strip.setBounds (r.removeFromTop (50));
    r.removeFromTop (10);
    auto cols = r.removeFromTop (262);
    const int cw = (cols.getWidth() - 4 * 6) / numBands;
    for (int b = 0; b < numBands; ++b)
    {
        columns[(size_t) b]->setBounds (cols.removeFromLeft (cw));
        cols.removeFromLeft (6);
    }
    r.removeFromTop (10);
    for (auto& d : details) d->setBounds (r);
}

// ===========================================================================
// HarmonicsTab
// ===========================================================================
HarmonicsTab::HarmonicsTab (KlingeProcessor& p)
    : proc (p), faders (p), lock (p, pid::harmLock, "Lock", Knob::Medium, false, false, help::lock)
{
    addAndMakeVisible (faders);
    addAndMakeVisible (lock);
    setHelp (faders, "Adds or removes single harmonics 2 to 8 on top of the distortion type.");
    for (int b = 0; b < numBands; ++b)
    {
        bandHarm[(size_t) b] = std::make_unique<Knob> (p, pid::band (b, pid::bHarm), "Band " + juce::String (b + 1), Knob::Small,
                                                       false, false, help::harm);
        addAndMakeVisible (*bandHarm[(size_t) b]);
    }
}

void HarmonicsTab::refresh()
{
    const int r = currentRouting (proc);
    const int mbCount = 2 + (int) proc.apvts.getRawParameterValue (pid::numBands)->load();
    const int n = activeBandCount (r, mbCount);
    const int key = r * 16 + mbCount;
    if (key == lastActive) return;
    lastActive = key;
    for (int b = 0; b < numBands; ++b)
    {
        auto& k = *bandHarm[(size_t) b];
        k.setLabel (bandName (r, mbCount, b));
        k.setEnabled (b < n);
        k.setAlpha (b < n ? 1.0f : 0.35f);
    }
}

void HarmonicsTab::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (12, 10);
    g.setColour (col::well.withAlpha (0.55f));
    g.fillRoundedRectangle (faders.getBounds().expanded (10, 8).toFloat(), 6.0f);

    g.setFont (fonts::ui (13.0f, 0));
    g.setColour (col::textDim);
    auto text = juce::Rectangle<int> (lock.getX() - 6, lock.getBottom() + 8, r.getRight() - lock.getX() + 6, 120);
    g.drawFittedText ("Lock keeps the harmonic ratios constant at any level. Turn it down and the harmonics grow with "
                      "the level, like analogue gear.", text, juce::Justification::topLeft, 6);

    auto bandsTitle = juce::Rectangle<int> (r.getX(), bandHarm[0]->getY() - 26, r.getWidth(), 20);
    g.setFont (fonts::ui (14.0f, 2));
    g.setColour (col::text);
    g.drawText ("Amount per band", bandsTitle, juce::Justification::centredLeft);

    g.setFont (fonts::ui (12.5f, 0));
    g.setColour (col::textFaint);
    auto tip = r.withTop (bandHarm[0]->getBottom() + 14);
    g.drawFittedText ("Odd harmonics (3, 5, 7) sound harder and more aggressive, even ones (2, 4, 6) fuller and warmer. "
                      "Pull a fader below zero to remove what the distortion type adds on that harmonic. "
                      "The display on the right shows the result for the selected band.",
                      tip, juce::Justification::topLeft, 4);
}

void HarmonicsTab::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto top = r.removeFromTop (300);
    lock.setBounds (top.removeFromRight (130).withHeight (150).translated (0, 20));
    top.removeFromRight (16);
    faders.setBounds (top.reduced (12, 10));
    r.removeFromTop (44);
    auto row = r.removeFromTop (110);
    const int w = row.getWidth() / numBands;
    for (auto& k : bandHarm) k->setBounds (row.removeFromLeft (w).reduced (8, 0));
}

// ===========================================================================
// CaptureTab
// ===========================================================================
CaptureTab::CaptureTab (KlingeProcessor& p)
    : proc (p),
      on (p.apvts, pid::capOn, "Capture on", "Switches the loaded profile on or off."),
      stereo (p.apvts, pid::capStereo, "True stereo",
              "Off: one model processes the centre, which is lighter on the CPU. On: left and right run through their own model."),
      position (p.apvts, pid::capPos, { "Before bands", "After bands" },
                "Before: the profile colours the sound before KLINGE's bands distort it, like a pedal into an amp. "
                "After: it processes the distorted sound, like an amp after a pedal."),
      in (p, pid::capIn, "Input", Knob::Small, true, false, "Level into the profile. Profiles of amps react strongly to this."),
      out (p, pid::capOut, "Output", Knob::Small, true, false, "Level after the profile."),
      mix (p, pid::capMix, "Mix", Knob::Small, false, false, "Blend between the sound before and after the profile.")
{
    for (auto* c : std::initializer_list<juce::Component*> { &load, &clear, &on, &stereo, &position, &in, &out, &mix })
        addAndMakeVisible (c);
    setHelp (load, "Load a Neural Amp Modeler profile (.nam file) of a real amp, pedal or piece of gear.");
    setHelp (clear, "Remove the loaded profile.");
    load.onClick = [this]
    {
        auto start = juce::File (proc.capture.getModelPath()).getParentDirectory();
        if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        chooser = std::make_unique<juce::FileChooser> ("Load a NAM profile", start, "*.nam");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<CaptureTab> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto f = fc.getResult();
            if (f.existsAsFile())
            {
                safe->proc.capture.loadAsync (f);
                if (auto* prm = safe->proc.apvts.getParameter (pid::capOn); prm != nullptr && prm->getValue() < 0.5f)
                {
                    prm->beginChangeGesture();
                    prm->setValueNotifyingHost (1.0f);
                    prm->endChangeGesture();
                }
                safe->proc.session.markUserEdit();
            }
        });
    };
    clear.onClick = [this] { proc.capture.clearModel(); proc.session.markUserEdit(); };
    clear.setEnabled (p.capture.hasModel());
}

void CaptureTab::refresh()
{
    const auto n = proc.capture.getModelName(), s = proc.capture.getStatus();
    if (n != shownName || s != shownStatus)
    {
        shownName = n; shownStatus = s;
        clear.setEnabled (proc.capture.hasModel());
        repaint();
    }
}

void CaptureTab::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (16, 12);
    g.setFont (fonts::ui (13.5f, 0));
    g.setColour (col::textDim);
    g.drawFittedText ("Capture runs a profile of real gear inside KLINGE, made with Neural Amp Modeler. Use it to put the "
                      "character of your own amp, pedal or the profiled sound of another plugin in front of or after the bands.",
                      r.removeFromTop (40), juce::Justification::topLeft, 2);

    auto well = juce::Rectangle<int> (r.getX(), load.getBottom() + 12, r.getWidth(), 64);
    g.setColour (col::well);
    g.fillRoundedRectangle (well.toFloat(), 6.0f);
    auto w = well.reduced (12, 8);
    g.setFont (fonts::ui (15.0f, 2));
    g.setColour (shownName.isEmpty() ? col::textFaint : col::straw);
    g.drawText (shownName.isEmpty() ? juce::String ("No profile loaded") : shownName, w.removeFromTop (24), juce::Justification::centredLeft);
    g.setFont (fonts::ui (12.5f, 0));
    g.setColour (shownStatus.startsWith ("Could not") || shownStatus.startsWith ("Trained at") ? col::bronze : col::textDim);
    g.drawFittedText (shownStatus, w, juce::Justification::centredLeft, 2);

    auto notes = getLocalBounds().reduced (16, 12).withTop (in.getBottom() + 16);
    g.setFont (fonts::ui (12.5f, 0));
    g.setColour (col::textFaint);
    g.drawFittedText ("Profiles are trained at one sample rate, usually 48 kHz. Run your session at that rate so the profile "
                      "sounds as captured; KLINGE tells you above when the rates differ.\n"
                      "Profiles are normalised to the same level when they load, so switching between them does not jump in volume. "
                      "The file path is stored with your session; keep the file where it is.",
                      notes, juce::Justification::topLeft, 6);
}

void CaptureTab::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    r.removeFromTop (48);
    auto row = r.removeFromTop (30);
    load.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (8);
    clear.setBounds (row.removeFromLeft (90));
    r.removeFromTop (12 + 64 + 16);
    auto chips = r.removeFromTop (28);
    on.setBounds (chips.removeFromLeft (120));
    chips.removeFromLeft (8);
    stereo.setBounds (chips.removeFromLeft (120));
    chips.removeFromLeft (16);
    position.setBounds (chips.removeFromLeft (240));
    r.removeFromTop (14);
    auto knobs = r.removeFromTop (110);
    for (auto* k : { &in, &out, &mix }) { k->setBounds (knobs.removeFromLeft (96)); knobs.removeFromLeft (8); }
}

// ===========================================================================
// ShaperMini
// ===========================================================================
ShaperMini::ShaperMini (KlingeProcessor& p)
    : proc (p),
      rate (p.apvts, pid::shpRate, "Length of one shaper cycle in note values. It follows the host position."),
      canvas (p),
      amount (p, pid::shpAmount, "Amount", Knob::Small, false, false,
              "How strongly the curve shapes the volume of the processed sound. 0 % switches the shaper off.")
{
    patterns.addItemList (ShaperCurve::presetNames(), 1);
    patterns.setTextWhenNothingSelected ("Patterns");
    patterns.onChange = [this]
    {
        const int i = patterns.getSelectedItemIndex();
        if (i >= 0) { proc.shaper.setPoints (ShaperCurve::preset (i)); proc.session.markUserEdit(); }
        patterns.setSelectedId (0, juce::dontSendNotification);
    };
    setHelp (patterns, "Start from a ready-made pattern. Edit the curve in Advanced, Modulation, Shaper.");
    setHelp (canvas, "Volume curve over one cycle, in time with the host. Drag points to edit, double-click to add or remove.");
    for (auto* c : std::initializer_list<juce::Component*> { &patterns, &rate, &canvas, &amount }) addAndMakeVisible (c);
}

void ShaperMini::refresh()
{
    canvas.refresh();
    const bool on = proc.apvts.getRawParameterValue (pid::shpAmount)->load() > 0.05f;
    canvas.setAlpha (on ? 1.0f : 0.45f);
}

void ShaperMini::paint (juce::Graphics& g)
{
    g.setFont (fonts::ui (14.5f, 2));
    g.setColour (col::text);
    g.drawText ("Shaper", 0, 0, 80, 26, juce::Justification::centredLeft);
}

void ShaperMini::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (26);
    top.removeFromLeft (76);
    patterns.setBounds (top.removeFromLeft (130));
    top.removeFromLeft (8);
    rate.setBounds (top.removeFromLeft (92));
    r.removeFromTop (8);
    amount.setBounds (r.removeFromRight (84).withHeight (juce::jmin (r.getHeight(), 104)));
    r.removeFromRight (8);
    canvas.setBounds (r);
}

// ===========================================================================
// BasicView
// ===========================================================================
BasicView::BasicView (KlingeProcessor& p)
    : proc (p), algos (p), curve (p), harmonics (p),
      drive     (p, pid::drive, "Drive", Knob::Large, false, true, help::drive),
      character (p, pid::band (0, pid::bShape), "Character", Knob::Medium),
      focus     (p, pid::focus, "Focus", Knob::Medium, true, false, help::focus),
      punch     (p, pid::punch, "Punch", Knob::Medium, false, false, help::punch),
      faithful  (p, pid::toneLock, "Faithful", Knob::Medium, false, false, help::faithful),
      mix       (p, pid::mix, "Mix", Knob::Medium, false, false, help::mix),
      output    (p, pid::outGain, "Output", Knob::Medium, true, false, help::output),
      shaper (p)
{
    for (auto* c : std::initializer_list<juce::Component*> { &algos, &curve, &harmonics, &drive, &character, &focus, &punch,
                                                             &faithful, &mix, &output, &shaper, &inMeter, &outMeter })
        addAndMakeVisible (c);
    setHelp (curve, "Transfer curve: input level left to right, output level bottom to top. Circuit models show a loop, "
                    "because their output also depends on what came before.");
    setHelp (harmonics, "Which harmonics the current setting adds to a sine wave. Bar 1 is the original tone.");
    setHelp (inMeter, "Input peak level.");
    setHelp (outMeter, "Output peak level.");
    curve.setBand (0);
    harmonics.setBand (0);
}

void BasicView::refresh()
{
    curve.refresh();
    harmonics.refresh();
    shaper.refresh();
    const auto a = (Algo) bandAlgo (proc, 0);
    character.setLabel (juce::String ("Character: ") + shapeLabel (a));
    setKnobHelp (character, juce::String (shapeLabel (a)) + ": " + algoDescription (a));

    const int routing = currentRouting (proc);
    if (routing != lastRouting)
    {
        lastRouting = routing;
        curve.setBandName (routing == (int) Routing::Multiband ? "Low band" : (routing == (int) Routing::MidSide ? "Mid" : ""));
        repaint();
    }
}

void BasicView::paint (juce::Graphics& g)
{
    drawPanel (g, { 16, 12, 300, 656 }, "Type");
    drawPanel (g, { 332, 440, 460, 228 }, {});
    drawPanel (g, { 808, 236, 416, 432 }, {});

    if (lastRouting > 0)
    {
        g.setFont (fonts::ui (12.5f, 0));
        g.setColour (col::textDim);
        g.drawFittedText (lastRouting == (int) Routing::Multiband
                              ? "Multiband is on. Type and Character set the low band. Open Advanced for all bands."
                              : "Mid/Side is on. Type and Character set the mid. Open Advanced for the sides.",
                          juce::Rectangle<int> (348, 640, 428, 24), juce::Justification::centred, 2);
    }
    g.setColour (col::panelEdge);
    g.drawHorizontalLine (380, 826.0f, 1206.0f);
}

void BasicView::resized()
{
    algos.setBounds (28, 48, 276, 612);
    curve.setBounds (332, 12, 460, 416);
    character.setBounds (352, 470, 124, 150);
    drive.setBounds (490, 450, 150, 206);
    focus.setBounds (652, 470, 124, 150);
    harmonics.setBounds (808, 12, 416, 212);
    punch.setBounds (824, 248, 96, 124);
    faithful.setBounds (922, 248, 96, 124);
    mix.setBounds (1020, 248, 96, 124);
    output.setBounds (1118, 248, 96, 124);
    shaper.setBounds (826, 392, 382, 194);
    inMeter.setBounds (826, 604, 382, 24);
    outMeter.setBounds (826, 634, 382, 24);
}

// ===========================================================================
// AdvancedView
// ===========================================================================
AdvancedView::AdvancedView (KlingeProcessor& p)
    : proc (p),
      inGain (p, pid::inGain, "Input", Knob::Small, true, false, help::input),
      drive  (p, pid::drive, "Drive", Knob::Small, false, true, help::drive),
      preHP  (p, pid::preHP, "Low cut", Knob::Small, false, false, help::preHP),
      preLP  (p, pid::preLP, "High cut", Knob::Small, false, false, help::preLP),
      focus  (p, pid::focus, "Focus", Knob::Small, true, false, help::focus),
      dyn (p, pid::dynDrive, "Dynamics", Knob::Small, true, false, help::dyn),
      punch (p, pid::punch, "Punch", Knob::Small, false, false, help::punch),
      punchSpeed (p, pid::punchSpeed, "Speed", Knob::Small, false, false, help::speed),
      fbAmount (p, pid::fbAmount, "Feedback", Knob::Small, false, false, help::fbAmount),
      fbTime (p, pid::fbTime, "Time", Knob::Small, false, false, help::fbTime),
      fbTone (p, pid::fbTone, "Tone", Knob::Small, false, false, help::fbTone),
      analog (p, pid::analog, "Analog", Knob::Small, false, false, help::analog),
      postHP (p, pid::postHP, "Low cut", Knob::Small, false, false, help::postHP),
      postLP (p, pid::postLP, "High cut", Knob::Small, false, false, help::postLP),
      faithful (p, pid::toneLock, "Faithful", Knob::Small, false, false, help::faithful),
      mix (p, pid::mix, "Mix", Knob::Small, false, false, help::mix),
      output (p, pid::outGain, "Output", Knob::Small, true, false, help::output),
      clip (p.apvts, pid::clipOut, "Clip", help::clip),
      osQuality (p.apvts, pid::osQuality, help::osQ),
      renderOs (p.apvts, pid::renderOs, help::render),
      bands (p), harmonicsTab (p), modulation (p), captureTab (p), analyse (p),
      curve (p), harmonics (p)
{
    for (auto* c : std::initializer_list<juce::Component*> { &inGain, &drive, &preHP, &preLP, &focus, &dyn, &punch, &punchSpeed,
                                                             &fbAmount, &fbTime, &fbTone, &analog, &postHP, &postLP, &faithful,
                                                             &mix, &output, &clip, &osQuality, &renderOs, &curve, &harmonics,
                                                             &inMeter, &outMeter })
        addAndMakeVisible (c);

    const char* tabNames[] = { "Bands", "Harmonics", "Modulation", "Capture", "Analyse" };
    const char* tabHelp[] = { "Routing, crossovers and the distortion of every band.",
                              "Harmonic designer: add or remove single harmonics.",
                              "LFOs, envelope followers, the shaper and MIDI. Drag a source onto any knob.",
                              "Load a profile of real gear (Neural Amp Modeler).",
                              "Spectrum, waveform and band levels." };
    for (int i = 0; i < 5; ++i)
    {
        auto* b = tabButtons.add (new juce::TextButton (tabNames[i]));
        b->getProperties().set ("tab", true);
        b->onClick = [this, i] { showTab (i); };
        setHelp (*b, tabHelp[i]);
        addAndMakeVisible (b);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &bands, &harmonicsTab, &modulation, &captureTab, &analyse })
        addChildComponent (c);

    bands.onBandSelected = [this] (int b) { bandSelected (b); };
    setHelp (curve, "Transfer curve of the selected band. Circuit models show a loop, because their output also depends on "
                    "what came before.");
    setHelp (harmonics, "Which harmonics the selected band adds to a sine wave. Bar 1 is the original tone.");
    setHelp (inMeter, "Input peak level.");
    setHelp (outMeter, "Output peak level.");
    bandSelected (bands.getSelectedBand());
    showTab (juce::jlimit (0, 4, p.ui.tab.load()));
}

void AdvancedView::showTab (int t)
{
    tab = juce::jlimit (0, 4, t);
    proc.ui.tab.store (tab);
    juce::Component* pages[] = { &bands, &harmonicsTab, &modulation, &captureTab, &analyse };
    for (int i = 0; i < 5; ++i)
    {
        pages[i]->setVisible (i == tab);
        tabButtons[i]->setToggleState (i == tab, juce::dontSendNotification);
    }
}

void AdvancedView::bandSelected (int b)
{
    curve.setBand (b);
    harmonics.setBand (b);
}

void AdvancedView::refresh()
{
    bands.refresh();          // also keeps the selected band valid when routing changes
    switch (tab)
    {
        case 1: harmonicsTab.refresh(); break;
        case 2: modulation.refresh(); break;
        case 3: captureTab.refresh(); break;
        case 4: analyse.refresh(); break;
        default: break;
    }
    const int b = bands.getSelectedBand();
    curve.setBandName (bandName (currentRouting (proc), 2 + (int) proc.apvts.getRawParameterValue (pid::numBands)->load(), b));
    curve.refresh();
    harmonics.refresh();

    auto& m = proc.getMeters();
    const float a = m.autoGainDb.load(), pu = m.punchDb.load(), l = m.lockLowDb.load(), h = m.lockHighDb.load();
    const float s = gainToDb (m.shaperGain.load());
    if (std::abs (a - agDb) > 0.05f || std::abs (pu - punchDb) > 0.05f || std::abs (l - lowDb) > 0.05f
        || std::abs (h - highDb) > 0.05f || std::abs (s - shpDb) > 0.1f)
    {
        agDb = a; punchDb = pu; lowDb = l; highDb = h; shpDb = s;
        repaint (956, 540, 268, 128);
    }
}

void AdvancedView::paint (juce::Graphics& g)
{
    drawPanel (g, { 16, 12, 316, 136 }, "Input");
    drawPanel (g, { 16, 156, 316, 136 }, "Dynamics");
    drawPanel (g, { 16, 300, 316, 136 }, "Feedback & analog");
    drawPanel (g, { 16, 444, 316, 224 }, "Output");
    drawPanel (g, { 344, 48, 600, 620 }, {});
    drawPanel (g, { 956, 452, 268, 216 }, "Levels");

    g.setFont (fonts::ui (12.5f, 1));
    g.setColour (col::textDim);
    g.drawText ("OS filter", 30, osQuality.getY(), 80, osQuality.getHeight(), juce::Justification::centredLeft);
    g.drawText ("Offline", 30, renderOs.getY(), 80, renderOs.getHeight(), juce::Justification::centredLeft);

    auto line = [&] (int y, const juce::String& name, const juce::String& value)
    {
        g.setFont (fonts::ui (12.5f, 1));
        g.setColour (col::textDim);
        g.drawText (name, 970, y, 120, 18, juce::Justification::centredLeft);
        g.setFont (fonts::mono (12.0f));
        g.setColour (col::text);
        g.drawText (value, 1060, y, 150, 18, juce::Justification::centredRight);
    };
    auto db = [] (float v) { return (v >= 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; };
    const bool agOn = proc.apvts.getRawParameterValue (pid::autoGain)->load() > 0.5f;
    const bool shpOn = proc.apvts.getRawParameterValue (pid::shpAmount)->load() > 0.05f;
    line (548, "Auto gain", agOn ? db (agDb) : juce::String ("off"));
    line (568, "Punch", db (punchDb));
    line (588, "Faithful low", db (lowDb));
    line (608, "Faithful high", db (highDb));
    line (628, "Shaper", shpOn ? db (shpDb) : juce::String ("off"));
}

void AdvancedView::resized()
{
    Knob* in[] = { &inGain, &drive, &preHP, &preLP, &focus };
    for (int i = 0; i < 5; ++i) in[i]->setBounds (22 + i * 61, 44, 60, 100);
    Knob* d[] = { &dyn, &punch, &punchSpeed };
    for (int i = 0; i < 3; ++i) d[i]->setBounds (30 + i * 98, 188, 94, 100);
    Knob* f[] = { &fbAmount, &fbTime, &fbTone, &analog };
    for (int i = 0; i < 4; ++i) f[i]->setBounds (24 + i * 76, 332, 74, 100);

    clip.setBounds (256, 450, 64, 22);
    osQuality.setBounds (104, 482, 216, 24);
    renderOs.setBounds (104, 512, 216, 24);
    Knob* o[] = { &postHP, &postLP, &faithful, &mix, &output };
    for (int i = 0; i < 5; ++i) o[i]->setBounds (22 + i * 61, 548, 60, 112);

    for (int i = 0; i < tabButtons.size(); ++i) tabButtons[i]->setBounds (344 + i * 120, 12, 116, 30);
    for (auto* c : std::initializer_list<juce::Component*> { &bands, &harmonicsTab, &modulation, &captureTab, &analyse })
        c->setBounds (344, 48, 600, 620);

    curve.setBounds (956, 12, 268, 250);
    harmonics.setBounds (956, 272, 268, 170);
    inMeter.setBounds (970, 486, 240, 24);
    outMeter.setBounds (970, 514, 240, 24);
}

} // namespace klinge::ui
