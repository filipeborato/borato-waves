#pragma once
#include "Controls.h"
#include "../PluginProcessor.h"
#include "../TableEdit.h"

namespace bw::ui
{
// Draws cycles and sets harmonics of OSC A or B's table. Edits publish as the oscillator's user table
// at most 15 times a second (and on mouse up), so held notes crossfade into each edit.
class TableEditor : public juce::Component, private juce::Timer
{
public:
    explicit TableEditor (WaveProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override { if (isVisible()) reloadIfReplaced(); }
    juce::Rectangle<int> waveArea() const { return { 20, 96, 720, 450 }; }
    juce::Rectangle<int> harmonicArea() const { return { 760, 96, 420, 450 }; }
    int frames() const { return edit.frames(); }
    int selectedFrame() const { return frame; }
private:
    void timerCallback() override;
    void stroke (const juce::MouseEvent&, bool start);
    void select (int oscillator);
    // The edited frame is the one the oscillator plays: choosing a frame moves its position, and moving
    // the position elsewhere (dial, drag, automation) chooses the frame.
    void selectFrame (int frame);
    void followPosition();
    juce::RangedAudioParameter& position() const;
    const bw::Wavetable& sourceTable() const;
    void reloadIfReplaced();
    void publish();
    void exportWav();
    WaveProcessor& processor;
    bw::TableEdit edit;
    int oscillator = 0, frame = 0;
    const bw::Wavetable* source = nullptr; // what the edit started from, or last published
    juce::String name;
    juce::Point<float> last;
    enum class Drag { none, wave, bars } drag = Drag::none;
    Action editA, editB, previous, next, add, remove, save;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;
};
}
