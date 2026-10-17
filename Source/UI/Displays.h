#pragma once
#include "Theme.h"
#include "../PluginProcessor.h"

namespace bw::ui
{
class Graph : public juce::Component, private juce::Timer
{
public:
    enum class Kind { wavetable, envelope, filter };
    Graph (WaveProcessor&, Kind, bool oscillatorB = false);
    ~Graph() override;
    void paint (juce::Graphics&) override;
    void setFlat (bool f) { flat = f; spectrum = false; repaint(); }
    void setSpectrum() { spectrum = true; repaint(); }
    void setFilterIndex (int index) { filterIndex = index; repaint(); }
    void setEnvelopeIndex (int index) { filterIndex = index; repaint(); } // shares the page index
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    void timerCallback() override;
    juce::Rectangle<float> plot() const;
    float value (const char*) const;
    WaveProcessor& processor;
    Kind kind;
    bool oscillatorB = false;
    bool flat = false, spectrum = false, scanning = false;
    int filterIndex = 0;
    std::unique_ptr<juce::ParameterAttachment> positionAttachment;
    std::array<float, 18> last {};
};
class Meter : public juce::Component, private juce::Timer
{
public:
    explicit Meter (WaveProcessor& p) : processor (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    WaveProcessor& processor;
    std::array<float, 2> levels {};
};
}
