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
    void visibilityChanged () override { if (isVisible ()) timerCallback (); }
private:
    void timerCallback () override;
    void choose (bool isRelink = false);
    void refreshZones ();
    void editZone ();
    void addZone ();
    void addLayer ();
    std::shared_ptr<const bw::Sample> displayedSample () const;
    juce::ComboBox zoneChoice, velocityLaw, variantChoice;
    juce::Slider lowKey, highKey, zoneRoot, lowVelocity, highVelocity, velocityFade, variantCount;
    Action addZoneButton, addLayerButton;
    int selectedZone = 0, displayedZone = -1, selectedVariant = 0;
    bool updatingZones = false;
    WaveProcessor& processor;
    Action load, relink, cancel, clear;
    ParameterToggle enabled, loop;
    ParameterChoice route, mode;
    Dial level, pitch, root, pan, start, end;
    Dial density, size, position, scan, spread, grainPitch, seed;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;
    std::vector<float> overview, overviewRight; // peak per column of the loaded sample (L/mono and R)
    unsigned revision = ~0u;
    bool wasLoading = false, wasMissing = false;
    float lastProgress = -1.0f;
    std::array<float, 4> shown {};
};
}
