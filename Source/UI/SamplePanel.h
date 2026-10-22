#pragma once
#include "Controls.h"
#include "../PluginProcessor.h"

namespace bw::ui
{
// The sample source: file, start/end/loop, level, pitch, root, pan and route, with an overview of the region.
class SamplePanel : public juce::Component, private juce::Timer
{
public:
    explicit SamplePanel (WaveProcessor&);
    void paint (juce::Graphics&) override;
    void resized () override;
private:
    void timerCallback () override;
    void choose (bool isRelink = false);
    WaveProcessor& processor;
    Action load, relink, cancel, clear;
    ParameterToggle enabled, loop;
    ParameterChoice route;
    Dial level, pitch, root, pan, start, end;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;
    std::vector<float> overview, overviewRight; // peak per column of the loaded sample (L/mono and R)
    unsigned revision = ~0u;
    bool wasLoading = false, wasMissing = false;
    float lastProgress = -1.0f;
    std::array<float, 4> shown {};
};
}
