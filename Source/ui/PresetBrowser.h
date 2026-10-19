#pragma once

#include "Widgets.h"

namespace klinge::ui
{
// Full-panel preset browser: search, categories, tags, favourites
class PresetBrowser : public juce::Component,
                      private juce::ListBoxModel
{
public:
    explicit PresetBrowser (KlingeProcessor& p);
    void open();
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;

    void rebuild();
    void filter();
    void load (int row);

    KlingeProcessor& proc;
    std::vector<PresetEntry> all, shown;
    juce::TextEditor search;
    juce::OwnedArray<juce::TextButton> categories, tagButtons;
    juce::String category { "All" };
    juce::StringArray activeTags;
    juce::ListBox list { "presets", this };
    IconButton close { IconButton::Close, "Close the browser." };
    juce::TextButton loadButton { "Load" };
};

} // namespace klinge::ui
