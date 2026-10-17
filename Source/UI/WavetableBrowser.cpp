#include "WavetableBrowser.h"

namespace bw::ui
{
using namespace juce;
SearchField::SearchField()
{
    setTitle ("Search wavetables");
    setDescription ("Type a name, category or sound description. Escape closes the library.");
    setWantsKeyboardFocus (true);
    setMouseCursor (MouseCursor::IBeamCursor);
}
void SearchField::paint (Graphics& g)
{
    surface (g, getLocalBounds(), inset);
    if (hasKeyboardFocus (false))
    {
        g.setColour (mint.withAlpha (.6f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (.5f), 8, 1);
    }
    auto r = getLocalBounds().reduced (13, 0);
    if (allSelected && textValue.isNotEmpty())
    {
        g.setColour (mint.withAlpha (.2f));
        g.fillRect (r.reduced (0, 8));
    }
    const Font font (FontOptions (13.0f));
    const auto prefix = textValue.substring (0, caret);
    const int caretWidth = roundToInt (GlyphArrangement::getStringWidth (font, prefix));
    const int scroll = jmax (0, caretWidth - r.getWidth() + 4);
    Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (r);
    label (g, textValue.isEmpty() ? "Search name, category or character..." : textValue,
           r.translated (-scroll, 0).withWidth (r.getWidth() + scroll + 2000), 13, textValue.isEmpty() ? muted : text);
    if (hasKeyboardFocus (false) && ! allSelected)
    {
        g.setColour (mint);
        g.fillRect (r.getX() + caretWidth - scroll, 10, 1, getHeight() - 20);
    }
}
void SearchField::clear()
{
    textValue.clear(); caret = 0; allSelected = false;
    if (onChange) onChange();
    repaint();
}
void SearchField::mouseDown (const MouseEvent&) { caret = textValue.length(); allSelected = false; repaint(); }
bool SearchField::keyPressed (const KeyPress& key)
{
    const int code = key.getKeyCode();
    const bool command = key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown();
    if (command && (code == 'A' || code == 'a')) { allSelected = true; repaint(); return true; }
    if (code == KeyPress::returnKey) { if (onReturn) onReturn(); return true; }
    if (code == KeyPress::escapeKey || code == KeyPress::tabKey) return false;
    if (code == KeyPress::leftKey || code == KeyPress::rightKey || code == KeyPress::homeKey || code == KeyPress::endKey)
    {
        if (code == KeyPress::homeKey) caret = 0;
        else if (code == KeyPress::endKey) caret = textValue.length();
        else caret = jlimit (0, textValue.length(), caret + (code == KeyPress::leftKey ? -1 : 1));
        allSelected = false; repaint(); return true;
    }
    bool changed = false;
    if (code == KeyPress::backspaceKey || code == KeyPress::deleteKey)
    {
        if (allSelected) { textValue.clear(); caret = 0; }
        else if (code == KeyPress::backspaceKey && caret > 0)
        { textValue = textValue.replaceSection (caret - 1, 1, ""); --caret; }
        else if (code == KeyPress::deleteKey) textValue = textValue.replaceSection (caret, 1, "");
        changed = true;
    }
    else if (! command && key.getTextCharacter() >= 32 && key.getTextCharacter() != 127)
    {
        if (allSelected) { textValue.clear(); caret = 0; }
        if (textValue.length() < 96)
        {
            textValue = textValue.substring (0, caret) + String::charToString (key.getTextCharacter()) + textValue.substring (caret);
            ++caret; changed = true;
        }
    }
    if (! changed) return false;
    allSelected = false;
    if (onChange) onChange();
    repaint(); return true;
}
WavetableBrowser::WavetableBrowser (const bw::WavetableBank& b, std::function<void (int)> select,
                                  std::function<void()> dismiss)
    : bank (b), choose (std::move (select)), close ("Done  x", std::move (dismiss)),
      clearSearch ("Clear", [this] { search.clear(); search.grabKeyboardFocus(); })
{
    setTitle ("Wavetable library");
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    addAndMakeVisible (close); addAndMakeVisible (search); addAndMakeVisible (clearSearch);
    StringArray categoryNames { "All" };
    for (int i = 0; i < bw::WavetableBank::kCount; ++i)
    {
        const auto& d = bw::WavetableBank::descriptions[(size_t) i];
        metadata[(size_t) i].name = d.name;
        metadata[(size_t) i].category = d.category;
        metadata[(size_t) i].description = d.description;
        categoryNames.addIfNotAlreadyThere (d.category);
        rows.push_back (std::make_unique<Row> (d.name, [this, i] { choose (i); }, [this, i] { preview = i; repaint(); }));
        addAndMakeVisible (*rows.back());
    }
    for (const auto& category : categoryNames)
    {
        categories.push_back (std::make_unique<Action> (category, [this, category]
        {
            query.category = category == "All" ? String() : category;
            for (auto& button : categories) button->setSelected (button->getTitle() == category);
            filterRows();
        }));
        addAndMakeVisible (*categories.back());
    }
    categories.front()->setSelected (true);
    search.onChange = [this] { query.text = search.query(); filterRows(); };
    search.onReturn = [this]
    {
        for (int i = 0; i < bw::WavetableBank::kCount; ++i)
            if (rows[(size_t) i]->isVisible()) { choose (i); break; }
    };
    filterRows();
}
void WavetableBrowser::setSelection (int index)
{
    selected = jlimit (0, bw::WavetableBank::kCount - 1, index);
    preview = selected;
    for (int i = 0; i < bw::WavetableBank::kCount; ++i) rows[(size_t) i]->setSelected (i == selected);
    repaint();
}
void WavetableBrowser::focusSearch() { search.grabKeyboardFocus(); }
void WavetableBrowser::filterRows()
{
    resultCount = 0;
    int first = -1;
    for (int i = 0; i < bw::WavetableBank::kCount; ++i)
    {
        const bool match = borato::PresetIndex::matches (metadata[(size_t) i], query);
        rows[(size_t) i]->setVisible (match);
        if (match) { if (first < 0) first = i; ++resultCount; }
    }
    if (first >= 0 && ! rows[(size_t) preview]->isVisible()) preview = first;
    resized(); repaint();
}
void WavetableBrowser::resized()
{
    close.setBounds (914, 143, 86, 28);
    search.setBounds (240, 207, 680, 40); clearSearch.setBounds (930, 207, 70, 40);
    const int categoryWidth = 760 / jmax (1, (int) categories.size());
    for (int i = 0; i < (int) categories.size(); ++i)
        categories[(size_t) i]->setBounds (240 + i * categoryWidth, 258, categoryWidth - 8, 29);
    int row = 0;
    for (auto& button : rows)
        if (button->isVisible()) button->setBounds (240, 304 + row++ * 43, 310, 35);
}
void WavetableBrowser::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (.72f));
    surface (g, { 210, 120, 820, 590 });
    label (g, "WAVETABLE LIBRARY", { 240, 140, 640, 24 }, 12, mint, Justification::centredLeft, true);
    label (g, "Choose the source. Explore its motion.", { 240, 170, 760, 29 }, 22, text);
    if (resultCount == 0)
        label (g, "No matching tables. Try another search.", { 240, 320, 760, 50 }, 15, muted, Justification::centred);
    else
    {
        const auto& d = bw::WavetableBank::descriptions[(size_t) preview];
        const auto& wt = bank[preview];
        label (g, d.name, { 580, 304, 420, 32 }, 23, text);
        badge (g, String (d.category).toUpperCase(), { 580, 346, 118, 22 }, mint);
        const Rectangle<int> area (580, 385, 420, 170);
        surface (g, area, inset); grid (g, area.toFloat().reduced (16));
        for (int frame = 3; frame >= 0; --frame)
        {
            Path p;
            for (int i = 0; i <= 200; ++i)
            {
                const Point<float> point (598 + i * 1.62f + frame * 17, 504 - frame * 26 - 26 * wt.read (jmin (.999999, i / 200.0), frame / 3.0f, 0));
                if (i == 0) p.startNewSubPath (point); else p.lineTo (point);
            }
            g.setColour (mint.withAlpha (frame == 0 ? .95f : .22f)); g.strokePath (p, PathStrokeType (1.5f));
        }
        g.setColour (muted); g.setFont (FontOptions (13.0f));
        g.drawFittedText (d.description, { 580, 573, 420, 48 }, Justification::topLeft, 3);
        label (g, String (wt.numFrames) + " FRAMES  /  2048 SAMPLES  /  BAND-LIMITED", { 580, 631, 420, 19 }, 9, mint);
    }
    label (g, String (resultCount) + " TABLES  /  SELECT TO LOAD INTO OSC " + (oscillatorB ? "B" : "A"), { 240, 674, 760, 20 }, 10, muted);
}
}
