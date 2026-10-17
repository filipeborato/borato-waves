#pragma once
#include "Controls.h"
#include "../WavetableBank.h"
#include "../Library/borato_preset_search.h"

namespace bw::ui
{
class SearchField : public juce::Component
{
public:
    SearchField();
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    void clear();
    const juce::String& query() const { return textValue; }
    std::function<void()> onChange, onReturn;
private:
    juce::String textValue;
    int caret = 0;
    bool allSelected = false;
};

class WavetableBrowser : public juce::Component
{
public:
    WavetableBrowser (const bw::WavetableBank&, std::function<void (int)>, std::function<void()>);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setSelection (int);
    void setOscillatorB (bool b) { oscillatorB = b; repaint(); }
    void focusSearch();
private:
    class Row : public Action
    {
    public:
        Row (juce::String s, std::function<void()> click, std::function<void()> hover)
            : Action (std::move (s), std::move (click)), onHover (std::move (hover)) {}
        void mouseEnter (const juce::MouseEvent& e) override { Action::mouseEnter (e); onHover(); }
        void focusGained (FocusChangeType reason) override { Action::focusGained (reason); onHover(); }
    private:
        std::function<void()> onHover;
    };
    void filterRows();
    const bw::WavetableBank& bank;
    std::function<void (int)> choose;
    SearchField search;
    Action close, clearSearch;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<std::unique_ptr<Action>> categories;
    std::array<borato::PresetInfo, bw::WavetableBank::kCount> metadata;
    borato::PresetQuery query;
    int selected = 0, preview = 0, resultCount = 0;
    bool oscillatorB = false;
};
}
