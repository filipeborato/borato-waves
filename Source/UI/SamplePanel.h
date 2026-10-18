#pragma once
#include "Controls.h"
#include "../PluginProcessor.h"

namespace bw::ui
{
// The sample source: file, start/end/loop, level and pitch, with an overview of the region.
class SamplePanel : public juce::Component, private juce::Timer
{
public:
    explicit SamplePanel (WaveProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void choose();
    WaveProcessor& processor;
    Action load, clear;
    ParameterToggle enabled, loop;
    Dial level, pitch, start, end;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;
    std::vector<float> overview; // peak per column of the loaded sample
    unsigned revision = ~0u;
    std::array<float, 3> shown {};
};
}
