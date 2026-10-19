#pragma once
#include "Controls.h"
#include "../UserWavetable.h"

namespace bw::ui
{
// Shown after choosing a file to import: the detected pitch, octave corrections, and a choice between
// the tuned import (one cycle per period) and the frame conversion.
class ImportDialog : public juce::Component
{
public:
    // tuned is false for the frame conversion; period is in samples of the source.
    ImportDialog (std::function<void (bool tuned, double period)> accept, std::function<void()> dismiss);
    // error: why the tuned import cannot be used (the frame conversion still can).
    void show (const juce::String& name, TunedSource source, const juce::String& error);
    const TunedSource& source() const { return analysed; }
    // "A2  +3 ct  /  110.4 Hz" for the chosen period.
    juce::String pitchText() const;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void shift (double factor);
    int cycles() const;
    std::function<void (bool, double)> onAccept;
    TunedSource analysed;
    juce::String fileName, problem;
    double period = 0;
    Action octaveDown, octaveUp, tuned, frames, cancel;
};
}
