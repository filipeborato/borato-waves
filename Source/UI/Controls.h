#pragma once
#include "Theme.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace bw::ui
{
// Own drawing and input; JUCE supplies infrastructure, not stock widgets.
class Action : public juce::Component, public juce::DragAndDropTarget
{
public:
    Action (juce::String, std::function<void()>);
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    void setText (const juce::String& s) { caption = s; setTitle (s); repaint(); }
    const juce::String& getText() const { return caption; }
    void setSelected (bool s) { selected = s; repaint(); }
    bool selected = false, quiet = false;
    std::function<void()> onModulationHover;
    bool isInterestedInDragSource (const SourceDetails& d) override
    { return onModulationHover && d.description.toString().startsWith ("mod-source:"); }
    void itemDragEnter (const SourceDetails&) override { if (onModulationHover) onModulationHover(); }
    void itemDropped (const SourceDetails&) override {}
private:
    juce::String caption;
    std::function<void()> onClick;
};
class ParameterToggle : public Action
{
public:
    ParameterToggle (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& name)
        : Action (name, [this] { attachment.setValueAsCompleteGesture (selected ? 0.0f : 1.0f); }),
          attachment (*state.getParameter (id), [this, name] (float value)
          {
              setSelected (value >= 0.5f);
              setText (name + (selected ? " ON" : " OFF"));
              setTitle (name + " enable");
          })
    {
        attachment.sendInitialUpdate();
    }
private:
    juce::ParameterAttachment attachment;
};
// Click steps through a choice parameter; Shift+click steps backwards.
class ParameterChoice : public Action
{
public:
    ParameterChoice (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& caption, const juce::String& title)
        : Action (caption, [this] { step(); }),
          choices (dynamic_cast<juce::AudioParameterChoice&> (*state.getParameter (id)).choices),
          attachment (*state.getParameter (id), [this, caption, title] (float value)
          {
              index = juce::jlimit (0, choices.size() - 1, juce::roundToInt (value));
              setText ((caption.isEmpty() ? juce::String() : caption + "  ") + choices[index]);
              setTitle (title);
          })
    {
        attachment.sendInitialUpdate();
    }
private:
    void step()
    {
        const int n = choices.size();
        attachment.setValueAsCompleteGesture ((float) ((index + (juce::ModifierKeys::currentModifiers.isShiftDown() ? n - 1 : 1)) % n));
    }
    juce::StringArray choices;
    int index = 0;
    juce::ParameterAttachment attachment;
};
// Drag onto a modulatable dial to create a matrix route from this source.
class SourceHandle : public Action
{
public:
    SourceHandle (juce::String name, int s) : Action (std::move (name), [] {}), source (s) {}
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() > 5)
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
                if (! container->isDragAndDropActive()) container->startDragging ("mod-source:" + juce::String (source), this);
    }
private:
    int source;
};
class Dial: public juce::Component, public juce::DragAndDropTarget
{
public:
    Dial (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& caption, juce::Colour = mint);
    ~Dial() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override;
    float normalised() const { return parameter.convertTo0to1 (plainValue); }
    juce::String valueText() const;
    const juce::String& parameterId() const { return id; }
    void setModulationRange (float lo, float hi) { modLow = lo; modHigh = hi; repaint(); }
    std::function<void (int, const juce::String&)> onModulationDrop;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dragOver = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dragOver = false; repaint(); }
    void itemDropped (const SourceDetails&) override;
private:
    void complete (float);
    void endDrag();
    juce::RangedAudioParameter& parameter;
    juce::String id, caption;
    juce::Colour accent;
    float plainValue = 0, dragValue = 0;
    int lastDragY = 0;
    bool dragging = false;
    bool dragOver = false;
    float modLow = 0, modHigh = 0;
    juce::ParameterAttachment attachment;
};
class Piano : public juce::Component, private juce::Timer
{
public:
    explicit Piano (juce::MidiKeyboardState&);
    ~Piano() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyStateChanged (bool) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusLost (FocusChangeType) override;
    void shiftOctave (int);
    int octave() const { return firstNote / 12 - 1; }
    void releaseNotes();
private:
    struct Key { int note; juce::Rectangle<float> bounds; bool black; };
    std::vector<Key> keys() const;
    int noteAt (juce::Point<float>) const;
    void playMouse (const juce::MouseEvent&);
    void timerCallback() override;
    juce::MidiKeyboardState& state;
    int firstNote = 48, mouseNote = -1;
    std::array<int, 13> typingNotes;
    std::array<bool, 128> lastState {};
};
}
