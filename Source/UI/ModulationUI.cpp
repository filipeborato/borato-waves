#include "ModulationUI.h"
#include "../PluginProcessor.h"

namespace bw::ui
{
namespace
{
// Custom cycling selector: mouse/Space moves forward, arrows move either way.
class Choice : public juce::Component
{
public:
    Choice (juce::AudioProcessorValueTreeState& state, const juce::String& id, juce::String name)
        : parameter (*state.getParameter (id)), caption (std::move (name)), isBpm (id == "internalBpm"),
          attachment (parameter, [this] (float v) { value = v; repaint(); })
    {
        setTitle (caption); setWantsKeyboardFocus (true); attachment.sendInitialUpdate();
    }
    void paint (juce::Graphics& g) override
    {
        surface (g, getLocalBounds(), inset);
        label (g, caption + ": " + (isBpm ? juce::String (value, 1) : parameter.getText (parameter.convertTo0to1 (value), 40)),
               getLocalBounds().reduced (7, 0), 11.0f);
        if (hasKeyboardFocus (true)) { g.setColour (mint); g.drawRect (getLocalBounds()); }
    }
    void mouseDown (const juce::MouseEvent& e) override { step (e.mods.isRightButtonDown() ? -1 : 1); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::leftKey || k == juce::KeyPress::downKey) { step (-1); return true; }
        if (k == juce::KeyPress::rightKey || k == juce::KeyPress::upKey || k == juce::KeyPress::spaceKey || k == juce::KeyPress::returnKey) { step (1); return true; }
        return false;
    }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
private:
    void step (int delta)
    {
        const auto range = parameter.getNormalisableRange();
        int next = juce::roundToInt (value) + delta;
        if (next > range.end) next = juce::roundToInt (range.start);
        if (next < range.start) next = juce::roundToInt (range.end);
        attachment.setValueAsCompleteGesture ((float) next);
    }
    juce::RangedAudioParameter& parameter;
    juce::String caption;
    bool isBpm = false;
    float value = 0;
    juce::ParameterAttachment attachment;
};
class DepthControl : public Dial
{
public:
    using Dial::Dial;
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().reduced (3);
        label (g, "DEPTH", { r.getX(), r.getY() + 2, r.getWidth(), 13 }, 9, muted, juce::Justification::centred);
        label (g, valueText(), { r.getX(), r.getBottom() - 18, r.getWidth(), 16 }, 11, hasKeyboardFocus (false) ? mint : text, juce::Justification::centred);
        const float y = r.getCentreY() - 1.0f, left = (float) r.getX() + 3, right = (float) r.getRight() - 3;
        const float mid = (left + right) * 0.5f, x = left + (right - left) * normalised();
        g.setColour (border); g.drawLine (left, y, right, y, 3);
        g.setColour (violet); g.drawLine (mid, y, x, y, 3);
        g.setColour (muted); g.drawLine (mid, y - 4, mid, y + 4, 1);
        g.setColour (text); g.fillEllipse (x - 3, y - 3, 6, 6);
        if (hasKeyboardFocus (false)) { g.setColour (mint); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1), 4, 1); }
    }
};
class CurveEditor : public juce::Component, private juce::Timer
{
public:
    CurveEditor (WaveProcessor& p, int i) : processor (p), index (i)
    {
        setTitle ("LFO " + juce::String (index + 1) + " curve editor"); setWantsKeyboardFocus (true); startTimerHz (30);
    }
    void paint (juce::Graphics& g) override
    {
        surface (g, getLocalBounds(), inset);
        const auto r = area(); grid (g, r, 8, 4);
        const auto c = processor.getLfoCurve (index);
        juce::Path path;
        for (int x = 0; x <= 256; ++x)
        {
            auto pt = juce::Point<float> (r.getX() + r.getWidth() * x / 256.0f,
                                         r.getBottom() - r.getHeight() * c.evaluate (x / 256.0));
            if (x == 0) path.startNewSubPath (pt); else path.lineTo (pt);
        }
        bw::ui::curve (g, path, mint);
        for (int j = 0; j < c.count; ++j)
        {
            auto pt = point (c.points[(size_t) j]);
            g.setColour (j == selected ? text : mint); g.fillEllipse (pt.x - 3.5f, pt.y - 3.5f, 7, 7);
        }
        const float x = r.getX() + r.getWidth() * processor.lfoDisplayPhase[(size_t) index].load();
        g.setColour (coral.withAlpha (0.7f)); g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
        if (hasKeyboardFocus (true)) { g.setColour (mint); g.drawRect (getLocalBounds()); }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        // A drag, or a double click's added point, is one undo step.
        if (! editing) { processor.beginEdit ("LFO " + juce::String (index + 1) + " SHAPE"); editing = true; }
        grabKeyboardFocus(); auto c = processor.getLfoCurve (index); selected = -1; segment = -1;
        for (int j = 0; j < c.count; ++j)
            if (point (c.points[(size_t) j]).getDistanceFrom (e.position) < 10) { selected = j; break; }
        if (selected < 0)
        {
            const auto x = normal (e.position).x;
            for (int j = 0; j + 1 < c.count; ++j)
                if (x >= c.points[(size_t) j].x && x <= c.points[(size_t) j + 1].x) { segment = j; startCurve = c.points[(size_t) j].curve; break; }
        }
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto c = processor.getLfoCurve (index);
        if (selected >= 0 && selected < c.count)
        {
            auto xy = normal (e.position); auto& pt = c.points[(size_t) selected];
            pt.y = xy.y;
            if (selected == 0 || selected == c.count - 1) c.points[0].y = c.points[(size_t) c.count - 1].y = xy.y;
            else
            {
                const float left = c.points[(size_t) selected - 1].x, right = c.points[(size_t) selected + 1].x;
                const float gap = juce::jmin (0.001f, (right - left) * 0.2f);
                pt.x = juce::jlimit (left + gap, right - gap, xy.x);
            }
        }
        else if (segment >= 0 && segment + 1 < c.count)
            c.points[(size_t) segment].curve = juce::jlimit (-1.0f, 1.0f, startCurve - e.getDistanceFromDragStartY() / 80.0f);
        processor.setLfoCurve (index, c); repaint();
    }
    ~CurveEditor() override { finishEdit(); }
    void mouseUp (const juce::MouseEvent&) override { finishEdit(); }
    void finishEdit() { if (editing) { editing = false; processor.endEdit(); } }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto c = processor.getLfoCurve (index); if (c.count >= 32) return;
        auto xy = normal (e.position);
        for (int j = 1; j < c.count; ++j)
            if (xy.x > c.points[(size_t) j - 1].x + 0.001f && xy.x < c.points[(size_t) j].x - 0.001f)
            {
                for (int k = c.count; k > j; --k) c.points[(size_t) k] = c.points[(size_t) k - 1];
                c.points[(size_t) j] = { xy.x, xy.y, 0 }; ++c.count; selected = j;
                processor.setLfoCurve (index, c); repaint(); return;
            }
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        auto c = processor.getLfoCurve (index);
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
        {
            if (selected > 0 && selected + 1 < c.count)
            {
                for (int j = selected; j + 1 < c.count; ++j) c.points[(size_t) j] = c.points[(size_t) j + 1];
                --c.count; selected = juce::jmin (selected, c.count - 1); processor.setLfoCurve (index, c);
            }
            repaint(); return true;
        }
        if (k == juce::KeyPress::leftKey || k == juce::KeyPress::rightKey)
        { selected = juce::jlimit (0, c.count - 1, selected + (k == juce::KeyPress::leftKey ? -1 : 1)); repaint(); return true; }
        if ((k == juce::KeyPress::upKey || k == juce::KeyPress::downKey) && selected >= 0 && selected < c.count)
        {
            auto& pt = c.points[(size_t) selected]; pt.y = juce::jlimit (0.0f, 1.0f, pt.y + (k == juce::KeyPress::upKey ? 0.01f : -0.01f));
            if (selected == 0 || selected == c.count - 1) c.points[0].y = c.points[(size_t) c.count - 1].y = pt.y;
            processor.setLfoCurve (index, c); repaint(); return true;
        }
        return false;
    }
private:
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (9); }
    juce::Point<float> normal (juce::Point<float> p) const
    { auto r = area(); return { juce::jlimit (0.0f, 1.0f, (p.x - r.getX()) / r.getWidth()), juce::jlimit (0.0f, 1.0f, (r.getBottom() - p.y) / r.getHeight()) }; }
    juce::Point<float> point (const bw::CurvePoint& p) const
    { auto r = area(); return { r.getX() + r.getWidth() * p.x, r.getBottom() - r.getHeight() * p.y }; }
    void timerCallback() override { if (isShowing()) repaint(); }
    WaveProcessor& processor;
    int index, selected = -1, segment = -1;
    float startCurve = 0;
    bool editing = false;
};
class MacroName : public juce::Component, private juce::Timer
{
public:
    MacroName (WaveProcessor& p, int i) : processor (p), index (i)
    { setWantsKeyboardFocus (true); setTitle ("Macro " + juce::String (i + 1) + " name"); startTimerHz (5); }
    void paint (juce::Graphics& g) override
    {
        surface (g, getLocalBounds(), inset);
        label (g, editing ? draft + "|" : processor.getMacroName (index), getLocalBounds().reduced (6, 0), 12, editing ? mint : text);
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { begin(); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::returnKey) { if (editing) finish(); else begin(); return true; }
        if (k == juce::KeyPress::escapeKey) { editing = false; repaint(); return true; }
        if (! editing) begin();
        if (k.getModifiers().isCtrlDown() && k.getKeyCode() == 'A') { draft.clear(); repaint(); return true; }
        if (k == juce::KeyPress::backspaceKey) draft = draft.dropLastCharacters (1);
        else if (k.getTextCharacter() >= 32 && ! k.getModifiers().isCommandDown() && draft.length() < 24) draft += juce::String::charToString (k.getTextCharacter());
        else return false;
        repaint(); return true;
    }
    void focusLost (FocusChangeType) override { finish(); }
private:
    void begin() { draft = processor.getMacroName (index); editing = true; grabKeyboardFocus(); repaint(); }
    void finish() { if (editing) processor.setMacroName (index, draft.trim()); editing = false; repaint(); }
    void timerCallback() override { if (isShowing() && ! editing) repaint(); }
    WaveProcessor& processor;
    int index;
    bool editing = false;
    juce::String draft;
};
}

struct LfoPanel::Content : public juce::Component
{
    struct Page : public juce::Component
    {
        Page (WaveProcessor& p, int i) : editor (p, i), rate (p.apvts, prefix (i) + "Rate", "RATE"),
            sync (p.apvts, prefix (i) + "Sync", "SYNC"), division (p.apvts, prefix (i) + "Division", "Beat"),
            mode (p.apvts, prefix (i) + "Mode", "Mode"), drag ("DRAG LFO " + juce::String (i + 1), i + 1)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &editor, &rate, &sync, &division, &mode, &drag }) addAndMakeVisible (c);
            const juce::StringArray names { "SINE", "TRI", "UP", "DOWN", "SQUARE" };
            for (int s = 0; s < 5; ++s)
            {
                shapes[(size_t) s] = std::make_unique<Action> (names[s], [&p, i, s] { p.setLfoCurve (i, bw::LfoCurve::factory (s)); });
                shapes[(size_t) s]->setTitle ("LFO " + juce::String (i + 1) + " shape " + names[s]); addAndMakeVisible (*shapes[(size_t) s]);
            }
        }
        static juce::String prefix (int i) { return "lfo" + juce::String (i + 1); }
        void resized() override
        {
            int w = getWidth();
            mode.setBounds (0, 0, w / 3, 24); sync.setBounds (w / 3 + 4, 0, w / 4 - 4, 24); division.setBounds (w * 7 / 12 + 4, 0, w * 5 / 12 - 4, 24);
            editor.setBounds (0, 30, w - 78, juce::jmax (60, getHeight() - 86));
            int y = getHeight() - 50;
            for (int s = 0; s < 5; ++s) shapes[(size_t) s]->setBounds (s * w / 5, y, w / 5 - 3, 22);
            rate.setBounds (w - 74, 30, 74, 90); drag.setBounds (0, getHeight() - 25, 110, 25);
        }
        void paint (juce::Graphics& g) override { label (g, "Double-click: add | Delete: remove | Drag segment: bend", { 118, getHeight() - 26, getWidth() - 120, 25 }, 9, muted); }
        CurveEditor editor;
        Dial rate;
        ParameterToggle sync;
        Choice division, mode;
        SourceHandle drag;
        std::array<std::unique_ptr<Action>, 5> shapes;
    };
    Content (WaveProcessor& p) : tab1 ("LFO 1", [this] { select (0); }), tab2 ("LFO 2", [this] { select (1); }), bpm (p.apvts, "internalBpm", "BPM")
    {
        addAndMakeVisible (tab1); addAndMakeVisible (tab2); addAndMakeVisible (bpm);
        for (int i = 0; i < 2; ++i) { pages[(size_t) i] = std::make_unique<Page> (p, i); addChildComponent (*pages[(size_t) i]); }
        select (0);
    }
    void select (int i) { pages[0]->setVisible (i == 0); pages[1]->setVisible (i == 1); tab1.setSelected (i == 0); tab2.setSelected (i == 1); }
    void resized() override
    {
        tab1.setBounds (0, 0, 64, 24); tab2.setBounds (68, 0, 64, 24);
        bpm.setBounds (getWidth() - 122, 0, 122, 24);
        for (auto& p : pages) p->setBounds (0, 51, getWidth(), getHeight() - 51);
    }
    void paint (juce::Graphics& g) override { label (g, "LAST NOTE PHASE", { 140, 3, getWidth() - 270, 20 }, 9, muted); }
    Action tab1, tab2;
    Choice bpm;
    std::array<std::unique_ptr<Page>, 2> pages;
};
LfoPanel::LfoPanel (WaveProcessor& p) : content (std::make_unique<Content> (p)) { setTitle ("LFO editor"); addAndMakeVisible (*content); }
LfoPanel::~LfoPanel() = default;
void LfoPanel::resized() { content->setBounds (getLocalBounds().reduced (8)); }
void LfoPanel::paint (juce::Graphics& g) { surface (g, getLocalBounds()); }

struct MatrixPanel::Content : public juce::Component
{
    struct Row : public juce::Component
    {
        Row (WaveProcessor& p, int slot) : number (slot + 1), enable (p.apvts, prefix (slot) + "Enabled", "ROUTE " + juce::String (number)),
            source (p.apvts, prefix (slot) + "Source", "Source"), destination (p.apvts, prefix (slot) + "Destination", "Target"),
            amount (p.apvts, prefix (slot) + "Amount", "DEPTH"), bipolar (p.apvts, prefix (slot) + "Bipolar", "BIPOLAR")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &enable, &source, &destination, &amount, &bipolar }) addAndMakeVisible (c);
            source.setTitle ("Route " + juce::String (number) + " source"); destination.setTitle ("Route " + juce::String (number) + " destination");
        }
        static juce::String prefix (int slot) { return "mod" + juce::String (slot + 1); }
        void resized() override
        {
            const int w = getWidth(), h = getHeight();
            enable.setBounds (8, 10, 130, h - 20); source.setBounds (150, 10, (w - 420) / 2, h - 20);
            destination.setBounds (160 + (w - 420) / 2, 10, (w - 420) / 2, h - 20);
            amount.setBounds (w - 246, 0, 84, h); bipolar.setBounds (w - 146, 10, 138, h - 20);
        }
        void paint (juce::Graphics& g) override { surface (g, getLocalBounds(), number % 2 ? panel : inset); }
        int number;
        ParameterToggle enable;
        Choice source, destination;
        DepthControl amount;
        ParameterToggle bipolar;
    };
    Content (WaveProcessor& p) : first ("ROUTES 1 - 8", [this] { selectPage (0); }), second ("ROUTES 9 - 16", [this] { selectPage (1); })
    {
        addAndMakeVisible (first); addAndMakeVisible (second);
        for (int i = 0; i < 16; ++i) { rows[(size_t) i] = std::make_unique<Row> (p, i); addChildComponent (*rows[(size_t) i]); }
        for (int i = 0; i < 4; ++i)
        {
            macros[(size_t) i] = std::make_unique<Dial> (p.apvts, "macro" + juce::String (i + 1), "MACRO " + juce::String (i + 1), violet);
            names[(size_t) i] = std::make_unique<MacroName> (p, i);
            handles[(size_t) i] = std::make_unique<SourceHandle> ("DRAG MACRO " + juce::String (i + 1), i + 3);
            addAndMakeVisible (*macros[(size_t) i]); addAndMakeVisible (*names[(size_t) i]); addAndMakeVisible (*handles[(size_t) i]);
        }
        selectPage (0);
    }
    void selectPage (int p)
    {
        page = p; for (int i = 0; i < 16; ++i) rows[(size_t) i]->setVisible (i / 8 == p);
        first.setSelected (p == 0); second.setSelected (p == 1); resized();
    }
    void selectSlot (int slot)
    {
        slot = juce::jlimit (0, 15, slot); selectPage (slot / 8);
        if (isShowing()) rows[(size_t) slot]->amount.grabKeyboardFocus();
    }
    void resized() override
    {
        first.setBounds (0, 0, 142, 28); second.setBounds (150, 0, 142, 28);
        const int rowHeight = juce::jlimit (44, 60, (getHeight() - 170) / 8);
        for (int i = 0; i < 16; ++i) rows[(size_t) i]->setBounds (0, 38 + (i % 8) * rowHeight, getWidth(), rowHeight - 3);
        const int y = 42 + 8 * rowHeight, w = getWidth() / 4;
        for (int i = 0; i < 4; ++i)
        {
            macros[(size_t) i]->setBounds (i * w, y, 82, 94);
            names[(size_t) i]->setBounds (i * w + 86, y + 8, w - 98, 29);
            handles[(size_t) i]->setBounds (i * w + 86, y + 44, w - 98, 28);
        }
    }
    void paint (juce::Graphics& g) override
    {
        label (g, "Click choices to cycle; arrows change selection. Depth is a fraction of the target range.", { 308, 0, getWidth() - 308, 28 }, 11, muted);
        label (g, "Double-click a macro name to rename. Drag over SYNTH tab, then a compatible dial to create a route.", { 0, getHeight() - 20, getWidth(), 20 }, 11, muted);
    }
    int page = 0;
    Action first, second;
    std::array<std::unique_ptr<Row>, 16> rows;
    std::array<std::unique_ptr<Dial>, 4> macros;
    std::array<std::unique_ptr<MacroName>, 4> names;
    std::array<std::unique_ptr<SourceHandle>, 4> handles;
};
MatrixPanel::MatrixPanel (WaveProcessor& p) : content (std::make_unique<Content> (p)) { setTitle ("Modulation matrix"); addAndMakeVisible (*content); }
MatrixPanel::~MatrixPanel() = default;
void MatrixPanel::resized() { content->setBounds (getLocalBounds()); }
void MatrixPanel::selectSlot (int slot) { content->selectSlot (slot); }
}




