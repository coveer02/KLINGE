#include "PresetBrowser.h"

namespace klinge::ui
{
PresetBrowser::PresetBrowser (KlingeProcessor& p) : proc (p)
{
    search.setTextToShowWhenEmpty ("Search names and tags", col::textFaint);
    search.setFont (fonts::ui (15.0f, 0));
    search.setIndents (10, 7);
    search.onTextChange = [this] { filter(); };
    search.onReturnKey = [this] { if (! shown.empty()) load (std::max (0, list.getSelectedRow())); };
    search.onEscapeKey = [this] { if (onClose) onClose(); };
    addAndMakeVisible (search);

    for (auto c : { "All", "Favourites", "Basic", "Techno", "Mixing", "Harmonics", "Design", "User" })
    {
        auto* b = categories.add (new juce::TextButton (c));
        b->getProperties().set ("chip", true);
        b->setClickingTogglesState (false);
        b->onClick = [this, b] { category = b->getButtonText(); filter(); };
        addAndMakeVisible (b);
    }

    list.setRowHeight (34);
    list.setColour (juce::ListBox::backgroundColourId, col::well);
    list.setColour (juce::ListBox::outlineColourId, col::well);
    addAndMakeVisible (list);

    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (close);
    loadButton.onClick = [this] { load (list.getSelectedRow()); };
    setHelp (loadButton, "Load the selected preset. Locked parameters keep their values.");
    addAndMakeVisible (loadButton);
    setWantsKeyboardFocus (true);
    setOpaque (true);
}

void PresetBrowser::open()
{
    rebuild();
    if (search.isShowing()) search.grabKeyboardFocus();
    const auto current = proc.presets.getCurrentName();
    for (int i = 0; i < (int) shown.size(); ++i)
        if (shown[(size_t) i].name == current) { list.selectRow (i); list.scrollToEnsureRowIsOnscreen (i); }
}

void PresetBrowser::rebuild()
{
    all = proc.presets.list();
    juce::StringArray tags;
    for (auto& e : all) for (auto& t : e.tags) tags.addIfNotAlreadyThere (t);
    tags.sort (true);
    tagButtons.clear();
    for (auto& t : tags)
    {
        auto* b = tagButtons.add (new juce::TextButton (t));
        b->getProperties().set ("chip", true);
        b->setClickingTogglesState (true);
        b->setToggleState (activeTags.contains (t), juce::dontSendNotification);
        b->onClick = [this, b]
        {
            if (b->getToggleState()) activeTags.addIfNotAlreadyThere (b->getButtonText());
            else activeTags.removeString (b->getButtonText());
            filter();
        };
        addAndMakeVisible (b);
    }
    resized();
    filter();
}

void PresetBrowser::filter()
{
    shown.clear();
    const auto q = search.getText().trim().toLowerCase();
    for (auto& e : all)
    {
        if (category == "Favourites" && ! proc.presets.isFavourite (e.name)) continue;
        if (category != "All" && category != "Favourites" && e.category != category) continue;
        bool tagsOk = true;
        for (auto& t : activeTags) tagsOk = tagsOk && e.tags.contains (t);
        if (! tagsOk) continue;
        if (q.isNotEmpty())
        {
            bool hit = e.name.toLowerCase().contains (q) || e.category.toLowerCase().contains (q);
            for (auto& t : e.tags) hit = hit || t.toLowerCase().contains (q);
            if (! hit) continue;
        }
        shown.push_back (e);
    }
    for (auto* b : categories) b->setToggleState (b->getButtonText() == category, juce::dontSendNotification);
    list.updateContent();
    list.repaint();
    if (! shown.empty() && list.getSelectedRow() < 0) list.selectRow (0);
}

void PresetBrowser::load (int row)
{
    if (row < 0 || row >= (int) shown.size()) return;
    proc.loadPreset (shown[(size_t) row]);
    list.repaint();
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& e = shown[(size_t) row];
    const bool current = e.name == proc.presets.getCurrentName();
    if (selected)
    {
        g.setColour (col::panelEdge);
        g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, 2.0f, (float) w - 8.0f, (float) h - 4.0f), 4.0f);
    }
    auto r = juce::Rectangle<int> (0, 0, w, h).reduced (14, 0);
    // favourite star
    {
        juce::Path star;
        star.addStar ({ (float) r.getX() + 8.0f, (float) h * 0.5f }, 5, 3.0f, 7.0f, juce::MathConstants<float>::pi * 0.2f);
        const bool fav = proc.presets.isFavourite (e.name);
        g.setColour (fav ? col::gold : col::textFaint);
        if (fav) g.fillPath (star); else g.strokePath (star, juce::PathStrokeType (1.2f));
        r.removeFromLeft (28);
    }
    g.setFont (fonts::ui (15.0f, current ? 2 : 1));
    g.setColour (current ? col::straw : col::text);
    g.drawText (e.name, r.removeFromLeft (260), juce::Justification::centredLeft);
    g.setFont (fonts::ui (13.0f, 0));
    g.setColour (col::textDim);
    g.drawText (e.category, r.removeFromLeft (110), juce::Justification::centredLeft);
    g.setColour (col::textFaint);
    g.drawText (e.tags.joinIntoString (", "), r, juce::Justification::centredLeft);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) shown.size()) return;
    if (e.x < 14 + 24)
    {
        const auto& name = shown[(size_t) row].name;
        proc.presets.setFavourite (name, ! proc.presets.isFavourite (name));
        if (category == "Favourites") filter();
        list.repaint();
    }
}

void PresetBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    load (row);
    if (onClose) onClose();
}

void PresetBrowser::returnKeyPressed (int row)
{
    load (row);
    if (onClose) onClose();
}

bool PresetBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    return false;
}

void PresetBrowser::resized()
{
    auto r = getLocalBounds().reduced (24, 0).withTrimmedTop (18);
    auto top = r.removeFromTop (36);
    close.setBounds (top.removeFromRight (36));
    top.removeFromRight (12);
    loadButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (12);
    search.setBounds (top.removeFromLeft (360));
    r.removeFromTop (12);
    auto cats = r.removeFromTop (28);
    for (auto* b : categories)
    {
        b->setBounds (cats.removeFromLeft (juce::jmax (64, (int) juce::GlyphArrangement::getStringWidth (fonts::ui (14.0f, 1), b->getButtonText()) + 28)));
        cats.removeFromLeft (6);
    }
    r.removeFromTop (8);
    // tags flow over as many lines as needed
    int x = r.getX(), y = r.getY();
    for (auto* b : tagButtons)
    {
        const int w = (int) juce::GlyphArrangement::getStringWidth (fonts::ui (13.0f, 1), b->getButtonText()) + 22;
        if (x + w > r.getRight()) { x = r.getX(); y += 28; }
        b->setBounds (x, y, w, 24);
        x += w + 6;
    }
    r.setTop (y + 36);
    r.removeFromBottom (26);     // status line
    list.setBounds (r);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::textFaint);
    g.setFont (fonts::ui (12.5f, 0));
    g.drawText (juce::String ((int) shown.size()) + " presets. Click the star to mark a favourite. Double-click or Enter loads.",
                getLocalBounds().reduced (24, 0).removeFromBottom (26), juce::Justification::centredLeft);
}

} // namespace klinge::ui
