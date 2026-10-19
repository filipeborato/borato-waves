#include "ImportDialog.h"

namespace bw::ui
{
using namespace juce;

ImportDialog::ImportDialog (std::function<void (bool, double)> accept, std::function<void()> dismiss)
    : onAccept (std::move (accept)),
      octaveDown ("OCTAVE -", [this] { shift (2); }), octaveUp ("OCTAVE +", [this] { shift (.5); }),
      tuned ("IMPORT TUNED", [this] { onAccept (true, period); }), frames ("IMPORT FRAMES", [this] { onAccept (false, 0); }),
      cancel ("CANCEL", std::move (dismiss))
{
    setTitle ("Import audio");
    for (auto* c : { &octaveDown, &octaveUp, &tuned, &frames, &cancel }) addAndMakeVisible (c);
    tuned.setSelected (true);
}

void ImportDialog::show (const String& name, TunedSource source, const String& error)
{
    fileName = name;
    analysed = std::move (source);
    problem = error;
    period = analysed.pitch.period;
    for (auto* c : { &octaveDown, &octaveUp, &tuned }) c->setVisible (problem.isEmpty());
    repaint();
}

// An octave correction stays within the detectable range and keeps at least one whole period.
void ImportDialog::shift (double factor)
{
    const double next = period * factor, rate = analysed.sampleRate;
    if (next >= rate / kMaxPitchHz && next <= rate / kMinPitchHz && next <= (double) analysed.mono.size() - 3) period = next;
    repaint();
}

int ImportDialog::cycles() const
{
    return period > 0 ? jmin (kMaxUserFrames, (int) (((double) analysed.mono.size() - 3) / period)) : 0;
}

String ImportDialog::pitchText() const
{
    if (period <= 0) return {};
    const double hz = analysed.sampleRate / period, note = 69 + 12 * std::log2 (hz / 440);
    const int nearest = roundToInt (note), cents = roundToInt ((note - nearest) * 100);
    return MidiMessage::getMidiNoteName (nearest, true, true, 4) + "  " + (cents >= 0 ? "+" : "") + String (cents) + " ct  /  " + String (hz, 1) + " Hz";
}

void ImportDialog::resized()
{
    octaveDown.setBounds (370, 450, 120, 32); octaveUp.setBounds (500, 450, 120, 32);
    tuned.setBounds (370, 560, 170, 34); frames.setBounds (550, 560, 170, 34); cancel.setBounds (760, 560, 110, 34);
}

void ImportDialog::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.72f));
    surface (g, { 340, 250, 560, 380 });
    label (g, "IMPORT AUDIO", { 370, 270, 400, 28 }, 12, mint, Justification::centredLeft, true);
    label (g, fileName, { 370, 300, 500, 36 }, 24, text);
    if (problem.isNotEmpty())
    {
        label (g, problem.toUpperCase(), { 370, 360, 500, 30 }, 16, coral);
        label (g, "The frame conversion can still slice the file into 2048-sample frames.", { 370, 395, 500, 22 }, 12, muted);
        return;
    }
    label (g, "DETECTED PITCH", { 370, 350, 300, 18 }, 10, muted);
    label (g, pitchText(), { 370, 370, 500, 40 }, 28, text);
    label (g, "CONFIDENCE " + String (roundToInt (analysed.pitch.confidence * 100)) + "%  /  " + String (cycles()) + " CYCLES", { 370, 414, 500, 20 }, 10, muted);
    if (analysed.pitch.ambiguous)
        badge (g, "AMBIGUOUS  /  CHECK THE OCTAVE BEFORE IMPORTING", { 640, 452, 240, 28 }, coral);
    label (g, "TUNED: one cycle per period, so the table plays at the note you press.", { 370, 500, 510, 20 }, 12, muted);
    label (g, "FRAMES: the existing conversion, 2048-sample slices of the file.", { 370, 522, 510, 20 }, 12, muted);
}
}
