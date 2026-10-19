#pragma once

#include "PluginProcessor.h"
#include "ui/Panels.h"
#include "ui/PresetBrowser.h"

class KlingeEditor : public juce::AudioProcessorEditor,
                     public juce::DragAndDropContainer,
                     private juce::Timer
{
public:
    explicit KlingeEditor (KlingeProcessor&);
    ~KlingeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int designW = 1240, designH = 760;

    void setAdvanced (bool adv);
    void showTab (int t) { advanced.showTab (t); }
    void showBrowser (bool show);

private:
    void timerCallback() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void stepPreset (int step);
    void savePresetDialog();
    void snapshotMenu (int index);
    void updateHeader();

    KlingeProcessor& proc;
    klinge::ui::KlingeLookAndFeel lnf;

    struct Content : public juce::Component
    {
        explicit Content (KlingeEditor& e) : owner (e) {}
        void paint (juce::Graphics&) override;
        KlingeEditor& owner;
    } content { *this };

    klinge::ui::IconButton prevPreset { klinge::ui::IconButton::Prev, "Previous preset." },
                           nextPreset { klinge::ui::IconButton::Next, "Next preset." };
    juce::TextButton presetName, savePreset { "Save" };
    struct SnapButton : public juce::TextButton
    {
        using juce::TextButton::TextButton;
        using juce::TextButton::clicked;
        void clicked (const juce::ModifierKeys& m) override { popup = m.isPopupMenu(); }
        bool popup = false;
    };
    juce::OwnedArray<SnapButton> snapButtons;
    klinge::ui::IconButton undoButton { klinge::ui::IconButton::Undo, "Undo (Ctrl/Cmd+Z)." },
                           redoButton { klinge::ui::IconButton::Redo, "Redo (Ctrl/Cmd+Shift+Z)." },
                           diceButton { klinge::ui::IconButton::Dice,
                                        "Random sound within musical ranges. Locked parameters keep their values "
                                        "(right-click a knob to lock it)." };
    juce::TextButton modeBasic { "Basic" }, modeAdvanced { "Advanced" };
    klinge::ui::Chip autoGain, delta;
    klinge::ui::ChoiceBox osBox;

    klinge::ui::BasicView basic;
    klinge::ui::AdvancedView advanced;
    klinge::ui::PresetBrowser browser;

    std::vector<klinge::ui::Knob*> knobs;
    juce::String helpText;
    int latencyShown = -1, snapState = -1;
    std::unique_ptr<juce::AlertWindow> saveWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KlingeEditor)
};
