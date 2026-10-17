#include "Controls.h"

namespace bw::ui
{
using namespace juce;
Action::Action (String s, std::function<void()> f) : caption (std::move (s)), onClick (std::move (f))
{
    setTitle (caption);
    setWantsKeyboardFocus (true);
    setMouseCursor (MouseCursor::PointingHandCursor);
}
void Action::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    if (! quiet || selected || isMouseOver() || hasKeyboardFocus (false))
    {
        g.setColour (selected ? mint.withAlpha (0.12f) : (isMouseButtonDown() ? border : inset));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (hasKeyboardFocus (false) ? mint : (selected ? mint.withAlpha (0.4f) : border));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }
    label (g, caption, getLocalBounds().reduced (8, 0), 12.0f,
           selected || isMouseOver() ? mint : text, Justification::centred, selected);
}
void Action::mouseUp (const MouseEvent& e)
{
    repaint();
    if (e.mods.isLeftButtonDown() && getLocalBounds().contains (e.getPosition()) && onClick) onClick();
}
bool Action::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::returnKey || k == KeyPress::spaceKey) { if (onClick) onClick(); return true; }
    return false;
}
Dial::Dial (AudioProcessorValueTreeState& s, const String& parameterId, const String& name, Colour c)
    : parameter (*s.getParameter (parameterId)), id (parameterId), caption (name), accent (c),
      attachment (parameter, [this] (float v) { plainValue = v; repaint(); })
{
    setTitle (caption);
    setDescription ("Drag vertically. Shift for fine control. Double click to reset. Arrow keys adjust.");
    setWantsKeyboardFocus (true);
    setMouseCursor (MouseCursor::UpDownResizeCursor);
    attachment.sendInitialUpdate();
}
Dial::~Dial() { endDrag(); }
String Dial::valueText() const
{
    if (id == "cutoff" || id == "cutoff2") return plainValue >= 1000 ? String (plainValue / 1000.0f, 2) + " kHz" : String (plainValue, 0) + " Hz";
    if (id == "gain" || id == "distDrive") return String (plainValue, 1) + " dB";
    if (id == "delayTime") return String (plainValue, 0) + " ms";
    if (id == "attack" || id == "decay" || id == "release")
        return plainValue < 1 ? String (plainValue * 1000, 0) + " ms" : String (plainValue, 2) + " s";
    if (id == "reso" || id == "reso2") return String (plainValue, 2) + " Q";
    if (id.endsWith ("Unison")) return String (plainValue, 0) + " voices";
    if (id.endsWith ("Detune")) return String (plainValue, 1) + " ct";
    if (id.endsWith ("Phase")) return String (plainValue, 0) + " deg";
    if (id.startsWith ("lfo") && id.endsWith ("Rate")) return String (plainValue, 2) + " Hz";
    if (id == "internalBpm") return String (plainValue, 1) + " BPM";
    if (id.endsWith ("Pitch")) return String (plainValue, 1) + " st";
    if (id.endsWith ("Fine")) return String (plainValue, 1) + " ct";
    if (id == "subOctave") return String (plainValue, 0) + " oct";
    if (id.endsWith ("Pan")) return std::abs (plainValue) < .005f ? "Centre"
        : String (std::abs (plainValue) * 100, 0) + (plainValue < 0 ? " L" : " R");
    return String (plainValue * 100, 1) + " %";
}
void Dial::paint (Graphics& g)
{
    auto r = getLocalBounds();
    label (g, caption, r.removeFromTop (18), 10.0f, muted, Justification::centred, true);
    label (g, valueText(), r.removeFromBottom (19), 11.0f, isMouseOver() ? accent : text, Justification::centred);
    const float radius = jmin (r.getWidth() * 0.5f - 9.0f, r.getHeight() * 0.5f - 4.0f);
    const auto centre = r.toFloat().getCentre();
    const float start = -MathConstants<float>::pi * 0.77f, end = -start;
    const float angle = start + (end - start) * normalised();
    Path track, active;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0, start, end, true);
    active.addCentredArc (centre.x, centre.y, radius, radius, 0, start, angle, true);
    g.setColour (border);
    g.strokePath (track, PathStrokeType (2.5f));
    curve (g, active, accent, 2.5f);
    if (modHigh > modLow)
    {
        Path modulation;
        modulation.addCentredArc (centre.x, centre.y, radius + 4, radius + 4, 0,
            start + (end - start) * jlimit (0.0f, 1.0f, normalised() + modLow),
            start + (end - start) * jlimit (0.0f, 1.0f, normalised() + modHigh), true);
        g.setColour (violet); g.strokePath (modulation, PathStrokeType (2));
    }
    if (dragOver) { g.setColour (violet); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1), 4, 2); }
    auto body = Rectangle<float> (radius * 1.55f, radius * 1.55f).withCentre (centre);
    g.setGradientFill (ColourGradient (Colour (0xff354145), body.getTopLeft(), inset, body.getBottomRight(), false));
    g.fillEllipse (body);
    g.setColour (hasKeyboardFocus (false) ? accent : Colour (0xff4a595d));
    g.drawEllipse (body, 1.0f);
    g.setColour (text);
    g.drawLine ({ centre.getPointOnCircumference (radius * 0.43f, angle), centre.getPointOnCircumference (radius * 0.67f, angle) }, 2.0f);
}
void Dial::mouseDown (const MouseEvent& e)
{
    if (! e.mods.isLeftButtonDown()) return;
    endDrag(); dragValue = normalised(); lastDragY = e.y; dragging = true; attachment.beginGesture();
}
bool Dial::isInterestedInDragSource (const SourceDetails& details)
{
    const auto description = details.description.toString();
    const int source = description.fromFirstOccurrenceOf (":", false, false).getIntValue();
    return onModulationDrop && description.startsWith ("mod-source:") && source >= 1 && source <= 9 && source != 7 && source != 8;
}
void Dial::itemDropped (const SourceDetails& details)
{
    dragOver = false;
    if (isInterestedInDragSource (details))
        onModulationDrop (details.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue(), id);
    repaint();
}
void Dial::mouseDrag (const MouseEvent& e)
{
    if (! dragging) return;
    dragValue = jlimit (0.0f, 1.0f, dragValue + (lastDragY - e.y) * (e.mods.isShiftDown() ? 0.0005f : 0.005f));
    lastDragY = e.y;
    attachment.setValueAsPartOfGesture (parameter.convertFrom0to1 (dragValue));
}
void Dial::endDrag() { if (dragging) { attachment.endGesture(); dragging = false; } }
void Dial::mouseUp (const MouseEvent&) { endDrag(); }
void Dial::focusLost (FocusChangeType) { endDrag(); repaint(); }
void Dial::complete (float v)
{
    endDrag(); attachment.setValueAsCompleteGesture (parameter.convertFrom0to1 (jlimit (0.0f, 1.0f, v)));
}
void Dial::mouseDoubleClick (const MouseEvent&) { complete (parameter.getDefaultValue()); }
void Dial::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const auto delta = w.deltaY != 0.0f ? w.deltaY : w.deltaX;
    if (delta != 0.0f) complete (normalised() + (delta > 0 ? 1.0f : -1.0f) * (e.mods.isShiftDown() ? 0.001f : 0.01f));
}
bool Dial::keyPressed (const KeyPress& k)
{
    const auto code = k.getKeyCode();
    const float step = k.getModifiers().isShiftDown() ? 0.001f : 0.01f;
    if (code == KeyPress::upKey || code == KeyPress::rightKey) complete (normalised() + step);
    else if (code == KeyPress::downKey || code == KeyPress::leftKey) complete (normalised() - step);
    else if (code == KeyPress::homeKey) complete (0);
    else if (code == KeyPress::endKey) complete (1);
    else if (code == KeyPress::returnKey) complete (parameter.getDefaultValue());
    else return false;
    return true;
}
Piano::Piano (MidiKeyboardState& s) : state (s)
{
    typingNotes.fill (-1);
    setTitle ("Performance keyboard");
    setDescription ("Click or drag to play. A W S E D F T G Y H U J K play one octave.");
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}
Piano::~Piano() { releaseNotes(); }
std::vector<Piano::Key> Piano::keys() const
{
    std::vector<Key> result;
    const float w = getWidth() / 28.0f, h = (float) getHeight();
    int white = 0;
    for (int n = firstNote; n < firstNote + 48; ++n)
    {
        const int pc = n % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        if (black) result.push_back ({ n, { white * w - w * 0.31f, 0, w * 0.62f, h * 0.62f }, true });
        else { result.push_back ({ n, { white * w + 1, 0, w - 2, h }, false }); ++white; }
    }
    return result;
}
void Piano::paint (Graphics& g)
{
    const auto all = keys();
    for (bool black : { false, true })
        for (const auto& key : all)
            if (key.black == black)
            {
                const bool down = state.isNoteOnForChannels (0xffff, key.note);
                auto r = key.bounds;
                g.setColour (Colours::black.withAlpha (0.3f));
                g.fillRoundedRectangle (r.translated (0, 2), 3);
                g.setGradientFill (ColourGradient (down ? mint : (black ? Colour (0xff293337) : Colour (0xffd1dad4)), r.getTopLeft(),
                                                  down ? mint.darker (0.2f) : (black ? inset : Colour (0xffa8b8b3)), r.getBottomLeft(), false));
                g.fillRoundedRectangle (r.withTrimmedBottom (3), 3);
                if (key.note % 12 == 0)
                    label (g, "C" + String (key.note / 12 - 1), r.toNearestInt().removeFromBottom (20), 9, faint, Justification::centred);
                if (down) { g.setColour (text); g.fillEllipse (r.getCentreX() - 2, r.getBottom() - 14, 4, 4); }
            }
    if (hasKeyboardFocus (false)) { g.setColour (mint.withAlpha (0.6f)); g.drawRect (getLocalBounds(), 1); }
}
int Piano::noteAt (Point<float> p) const
{
    if (! getLocalBounds().toFloat().contains (p)) return -1;
    const auto all = keys();
    for (bool black : { true, false })
        for (const auto& key : all)
            if (key.black == black && key.bounds.contains (p)) return key.note;
    return -1;
}
void Piano::playMouse (const MouseEvent& e)
{
    const int next = noteAt (e.position);
    if (next == mouseNote) return;
    if (mouseNote >= 0) state.noteOff (1, mouseNote, 0.0f);
    mouseNote = next;
    if (mouseNote >= 0) state.noteOn (1, mouseNote, jlimit (0.15f, 0.85f, e.position.y / getHeight()));
    repaint();
}
void Piano::mouseDown (const MouseEvent& e) { if (e.mods.isLeftButtonDown()) playMouse (e); }
void Piano::mouseDrag (const MouseEvent& e) { if (e.mods.isLeftButtonDown()) playMouse (e); }
void Piano::mouseUp (const MouseEvent&)
{
    if (mouseNote >= 0) state.noteOff (1, mouseNote, 0);
    mouseNote = -1; repaint();
}
void Piano::releaseNotes()
{
    if (mouseNote >= 0) state.noteOff (1, mouseNote, 0);
    mouseNote = -1;
    for (auto& n : typingNotes) { if (n >= 0) state.noteOff (1, n, 0); n = -1; }
    repaint();
}
void Piano::shiftOctave (int delta) { releaseNotes(); firstNote = jlimit (12, 72, firstNote + 12 * delta); repaint(); }
void Piano::focusLost (FocusChangeType) { releaseNotes(); }
bool Piano::keyPressed (const KeyPress& k) { return String ("AWSEDFTGYHUJK").containsChar ((juce_wchar) k.getKeyCode()); }
bool Piano::keyStateChanged (bool)
{
    if (! hasKeyboardFocus (false)) return false;
    bool used = false;
    const String mapping = "AWSEDFTGYHUJK";
    for (int i = 0; i < mapping.length(); ++i)
    {
        auto& n = typingNotes[(size_t) i];
        const bool down = KeyPress::isKeyCurrentlyDown (mapping[i]);
        if (down && n < 0) { n = firstNote + i; state.noteOn (1, n, 0.65f); used = true; }
        if (! down && n >= 0) { state.noteOff (1, n, 0); n = -1; used = true; }
    }
    return used;
}
void Piano::timerCallback()
{
    bool changed = false;
    for (int i = 0; i < 128; ++i)
    {
        const bool on = state.isNoteOnForChannels (0xffff, i);
        if (lastState[(size_t) i] != on) { lastState[(size_t) i] = on; changed = true; }
    }
    if (changed) repaint();
}
}
