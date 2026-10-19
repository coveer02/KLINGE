#include "PluginEditor.h"

using namespace klinge;
using namespace klinge::ui;

namespace
{
    constexpr int headerH = 56, footerH = 26;
    const char* snapNames[] = { "A", "B", "C", "D" };
}

KlingeEditor::KlingeEditor (KlingeProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      autoGain (p.apvts, pid::autoGain, "Auto gain",
                "Matches the output loudness to the input, so you compare character instead of volume."),
      delta (p.apvts, pid::delta, "Delta", "Plays only what KLINGE adds to the signal."),
      osBox (p.apvts, pid::oversample,
             "Runs the distortion at a higher sample rate to reduce aliasing. Higher is cleaner and uses more CPU."),
      basic (p), advanced (p), browser (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.setBounds (0, 0, designW, designH);

    for (int i = 0; i < Session::numSnapshots; ++i)
    {
        auto* b = snapButtons.add (new SnapButton (snapNames[i]));
        b->getProperties().set ("segment", i == 0 ? 0 : (i == Session::numSnapshots - 1 ? 2 : 1));
        b->onClick = [this, i, b]
        {
            if (b->popup) { snapshotMenu (i); return; }
            proc.session.selectSnapshot (i);
            updateHeader();
        };
        setHelp (*b, "Four sounds to compare. Click to switch, right-click to copy one onto another.");
        content.addAndMakeVisible (b);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &prevPreset, &presetName, &nextPreset, &savePreset, &undoButton,
                                                             &redoButton, &diceButton, &modeBasic, &modeAdvanced, &autoGain,
                                                             &delta, &osBox, &basic, &advanced })
        content.addAndMakeVisible (c);
    content.addChildComponent (browser);

    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    presetName.onClick = [this] { showBrowser (! browser.isVisible()); };
    presetName.getProperties().set ("preset", true);
    setHelp (presetName, "Open the preset browser: search, categories, tags and favourites. "
                         "Your presets are stored in Documents/KLINGE/Presets.");
    savePreset.onClick = [this] { savePresetDialog(); };
    setHelp (savePreset, "Save the current sound as your own preset, with tags.");
    browser.onClose = [this] { showBrowser (false); };

    undoButton.onClick = [this] { proc.session.undo(); updateHeader(); };
    redoButton.onClick = [this] { proc.session.redo(); updateHeader(); };
    diceButton.onClick = [this] { proc.session.randomise(); updateHeader(); };

    modeBasic.getProperties().set ("segment", 0);
    modeAdvanced.getProperties().set ("segment", 2);
    modeBasic.onClick = [this] { setAdvanced (false); };
    modeAdvanced.onClick = [this] { setAdvanced (true); };
    setHelp (modeBasic, "The essentials on one page.");
    setHelp (modeAdvanced, "Every control: bands, dynamics, feedback, harmonics, modulation, capture and analysis.");

    collectKnobs (content, knobs);
    content.addMouseListener (this, true);
    setAdvanced (proc.ui.advanced.load());

    setWantsKeyboardFocus (true);
    const double scale = juce::jlimit (0.6, 1.6, (double) proc.ui.scale.load());
    setResizable (true, true);
    setResizeLimits ((int) (designW * 0.6), (int) (designH * 0.6), (int) (designW * 1.6), (int) (designH * 1.6));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) designW / designH);
    setSize ((int) (designW * scale), (int) (designH * scale));

    proc.session.setEditorOpen (true);
    updateHeader();
    startTimerHz (30);
}

KlingeEditor::~KlingeEditor()
{
    stopTimer();
    proc.session.setEditorOpen (false);
    content.removeMouseListener (this);
    saveWindow.reset();
    setLookAndFeel (nullptr);
}

void KlingeEditor::resized()
{
    const float scale = (float) getWidth() / (float) designW;
    content.setTransform (juce::AffineTransform::scale (scale));
    proc.ui.scale.store (scale);

    prevPreset.setBounds (182, 12, 28, 32);
    presetName.setBounds (214, 12, 210, 32);
    nextPreset.setBounds (428, 12, 28, 32);
    savePreset.setBounds (462, 12, 56, 32);
    for (int i = 0; i < snapButtons.size(); ++i) snapButtons[i]->setBounds (538 + i * 28, 12, 28, 32);
    undoButton.setBounds (668, 12, 30, 32);
    redoButton.setBounds (700, 12, 30, 32);
    diceButton.setBounds (736, 12, 30, 32);
    modeBasic.setBounds (784, 12, 82, 32);
    modeAdvanced.setBounds (866, 12, 96, 32);
    osBox.setBounds (designW - 18 - 62, 12, 62, 32);
    delta.setBounds (designW - 18 - 62 - 6 - 62, 12, 62, 32);
    autoGain.setBounds (designW - 18 - 62 - 6 - 62 - 6 - 92, 12, 92, 32);

    basic.setBounds (0, headerH, designW, designH - headerH - footerH);
    advanced.setBounds (0, headerH, designW, designH - headerH - footerH);
    browser.setBounds (0, headerH + 1, designW, designH - headerH - footerH - 1);
}

void KlingeEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void KlingeEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    // wordmark with a tempered blade underneath
    g.setColour (col::text);
    g.setFont (fonts::ui (27.0f, 2));
    g.drawText ("KLINGE", 22, 8, 150, 34, juce::Justification::centredLeft);
    juce::Path blade;
    blade.startNewSubPath (23.0f, 44.0f);
    blade.lineTo (140.0f, 41.5f);
    blade.lineTo (154.0f, 44.0f);
    blade.lineTo (140.0f, 46.5f);
    blade.closeSubPath();
    g.setGradientFill (temperGradient ({ 23.0f, 44.0f }, { 154.0f, 44.0f }));
    g.fillPath (blade);

    // filled snapshots get a dot
    for (int i = 0; i < owner.snapButtons.size(); ++i)
        if (owner.proc.session.snapshotUsed (i) && i != owner.proc.session.getActiveSnapshot())
        {
            const auto b = owner.snapButtons[i]->getBounds();
            g.setColour (col::gold.withAlpha (0.8f));
            g.fillEllipse ((float) b.getCentreX() - 2.0f, (float) b.getBottom() + 3.0f, 4.0f, 4.0f);
        }

    g.setColour (col::panelEdge);
    g.drawHorizontalLine (headerH, 0.0f, (float) designW);

    // footer
    g.drawHorizontalLine (designH - footerH, 0.0f, (float) designW);
    g.setFont (fonts::ui (13.0f, 0));
    g.setColour (owner.helpText.isEmpty() ? col::textFaint : col::textDim);
    g.drawFittedText (owner.helpText.isEmpty()
                          ? juce::String ("Point at any control to see what it does. Double-click a knob to reset it, "
                                          "right-click it to modulate or lock it.")
                          : owner.helpText,
                      { 20, designH - footerH, designW - 280, footerH }, juce::Justification::centredLeft, 1, 0.85f);
    g.setFont (fonts::mono (11.5f));
    g.setColour (col::textFaint);
    g.drawText ("Latency " + juce::String (owner.proc.getLatencySamples()) + " smp    v" JucePlugin_VersionString,
                designW - 260, designH - footerH, 240, footerH, juce::Justification::centredRight);
}

void KlingeEditor::setAdvanced (bool adv)
{
    proc.ui.advanced.store (adv);
    modeBasic.setToggleState (! adv, juce::dontSendNotification);
    modeAdvanced.setToggleState (adv, juce::dontSendNotification);
    basic.setVisible (! adv);
    advanced.setVisible (adv);
}

void KlingeEditor::showBrowser (bool show)
{
    if (show == browser.isVisible()) return;
    browser.setVisible (show);
    presetName.setToggleState (show, juce::dontSendNotification);
    if (show) { browser.toFront (true); browser.open(); }
    else if (isShowing()) grabKeyboardFocus();
}

void KlingeEditor::updateHeader()
{
    const auto name = proc.presets.getCurrentName();
    if (presetName.getButtonText() != name) presetName.setButtonText (name);
    undoButton.setEnabled (proc.session.canUndo());
    redoButton.setEnabled (proc.session.canRedo());

    const int active = proc.session.getActiveSnapshot();
    int state = active;
    for (int i = 0; i < snapButtons.size(); ++i)
    {
        snapButtons[i]->setToggleState (i == active, juce::dontSendNotification);
        if (proc.session.snapshotUsed (i)) state |= 1 << (i + 4);
    }
    if (state != snapState)
    {
        snapState = state;
        content.repaint (530, 44, 130, 12);
    }
}

void KlingeEditor::timerCallback()
{
    if (proc.ui.advanced.load() != advanced.isVisible()) setAdvanced (proc.ui.advanced.load());

    auto& m = proc.getMeters();
    const float in = m.inPeak.exchange (0.0f), out = m.outPeak.exchange (0.0f);
    if (! browser.isVisible())
    {
        if (basic.isVisible())
        {
            basic.inMeter.push (in); basic.outMeter.push (out);
            basic.refresh();
        }
        else
        {
            advanced.inMeter.push (in); advanced.outMeter.push (out);
            advanced.refresh();
        }
        for (auto* k : knobs)
            if (visibleInTree (*k)) k->refreshMod();
    }
    updateHeader();

    if (proc.getLatencySamples() != latencyShown)
    {
        latencyShown = proc.getLatencySamples();
        content.repaint (designW - 260, designH - footerH, 260, footerH);
    }
}

bool KlingeEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const auto c = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) k.getKeyCode());
    if (mods.isCommandDown() && c == 'z')
    {
        if (mods.isShiftDown()) proc.session.redo(); else proc.session.undo();
        updateHeader();
        return true;
    }
    if (mods.isCommandDown() && c == 'y') { proc.session.redo(); updateHeader(); return true; }
    if (k == juce::KeyPress::escapeKey && browser.isVisible()) { showBrowser (false); return true; }
    return false;
}

void KlingeEditor::mouseEnter (const juce::MouseEvent& e)
{
    for (auto* c = e.eventComponent; c != nullptr && c != &content; c = c->getParentComponent())
    {
        const auto h = c->getProperties()["help"].toString();
        if (h.isNotEmpty())
        {
            if (h != helpText) { helpText = h; content.repaint (0, designH - footerH, designW - 260, footerH); }
            return;
        }
    }
}

void KlingeEditor::mouseExit (const juce::MouseEvent& e)
{
    if (e.eventComponent == &content && helpText.isNotEmpty())
    {
        helpText.clear();
        content.repaint (0, designH - footerH, designW - 260, footerH);
    }
}

void KlingeEditor::stepPreset (int step)
{
    const auto list = proc.presets.list();
    const int n = (int) list.size();
    if (n == 0) return;
    const auto current = proc.presets.getCurrentName();
    int idx = -1;
    for (int i = 0; i < n; ++i)
        if (list[(size_t) i].name == current) { idx = i; break; }
    const int next = idx < 0 ? (step > 0 ? 0 : n - 1) : (idx + step + n) % n;
    proc.loadPreset (list[(size_t) next]);
    updateHeader();
}

void KlingeEditor::snapshotMenu (int index)
{
    juce::PopupMenu m;
    for (int j = 0; j < Session::numSnapshots; ++j)
        if (j != index)
            m.addItem (juce::String ("Copy ") + snapNames[index] + " to " + snapNames[j],
                       [this, index, j] { proc.session.copySnapshot (index, j); updateHeader(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (snapButtons[index]));
}

void KlingeEditor::savePresetDialog()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset",
                                                      "Name your preset and add tags if you like (comma separated). "
                                                      "It is stored in Documents/KLINGE/Presets.",
                                                      juce::MessageBoxIconType::NoIcon, this);
    saveWindow->setLookAndFeel (&lnf);
    saveWindow->addTextEditor ("name", proc.presets.getCurrentName(), "Name");
    saveWindow->addTextEditor ("tags", "", "Tags");
    saveWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<KlingeEditor> safe (this);
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
    {
        if (safe == nullptr || safe->saveWindow == nullptr) return;
        if (result == 1)
        {
            const auto name = safe->saveWindow->getTextEditorContents ("name");
            const auto tags = safe->saveWindow->getTextEditorContents ("tags");
            if (! safe->proc.presets.saveUser (name, tags, safe->proc.captureSound()))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not save the preset",
                                                        "Check that the name is not empty and that Documents/KLINGE/Presets "
                                                        "is writable.");
            safe->updateHeader();
        }
        safe->saveWindow.reset();
    }), false);
}
