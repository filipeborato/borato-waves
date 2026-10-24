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
    // Slice markers on the waveform: double-click adds, drag moves, right-click removes.
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    void timerCallback () override;
    void choose (bool isRelink = false);
    void refreshZones ();
    void editZone ();
    void addZone ();
    void addLayer ();
    void toggleSlices ();
    void commitSlices (std::vector<int> markers);
    std::vector<int> sliceMarkers () const;
    int markerAt (int x) const;
    int frameAt (int x) const;
    std::shared_ptr<const bw::Sample> displayedSample () const;
    juce::ComboBox zoneChoice, velocityLaw, variantChoice;
    juce::Slider lowKey, highKey, zoneRoot, lowVelocity, highVelocity, velocityFade, variantCount;
    Action addZoneButton, addLayerButton, sliceButton;
    juce::Slider sliceRoot;
    juce::ComboBox sliceMode;
    std::vector<int> draggedMarkers;
    int draggedIndex = -1;
    int selectedZone = 0, displayedZone = -1, selectedVariant = 0;
    bool updatingZones = false;
    WaveProcessor& processor;
    Action load, relink, cancel, clear;
    ParameterToggle enabled, loop, freeze, loopLink, jumpStart, grainLoop, oscA, oscB;
    ParameterChoice route, mode, loopMode, direction, densityMode, densityDivision, lengthMode, lengthDivision, distribution;
    Dial level, pitch, root, pan, start, end, loopStart, loopEnd, loopFade, fadeIn, fadeOut;
    Dial density, size, position, scan, spread, grainPitch, jitter, grainPan, seed;
    Dial spectralPosition, spectralSpeed, spectralPitch;
    Dial overlap, lengthPercent, batch, keyTrack;
    Action grainsPage, timingPage;
    bool showTiming = false, draggingPosition = false;
    int shownMode = -1;
    void updatePages ();
    void updateEnables ();
    void setGrainPosition (int x);
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;
    std::vector<float> overview, overviewRight; // peak per column of the loaded sample (L/mono and R)
    unsigned revision = ~0u;
    bool wasLoading = false, wasMissing = false;
    float lastProgress = -1.0f;
    std::array<float, 4> shown {};
};
}
