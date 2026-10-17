#pragma once
#include "PluginProcessor.h"

class WaveEditor : public juce::AudioProcessorEditor
{
public:
    explicit WaveEditor (WaveProcessor&);
    ~WaveEditor() override;
    void resized() override;
private:
    class Interface;
    std::unique_ptr<Interface> ui;
};
