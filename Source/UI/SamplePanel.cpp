#include "SamplePanel.h"

namespace bw::ui
{
using namespace juce;
namespace
{
const Rectangle<int> waveform { 20, 96, 1160, 300 };
}

SamplePanel::SamplePanel (WaveProcessor& p)
    : processor (p),
      load ("LOAD SAMPLE", [this] { choose(); }), clear ("CLEAR", [this] { processor.clearSample(); status.clear(); }),
      enabled (p.apvts, "sampleOn", "SAMPLE"), loop (p.apvts, "sampleLoop", "LOOP"),
      level (p.apvts, "sampleLevel", "SAMPLE LEVEL"), pitch (p.apvts, "samplePitch", "SAMPLE PITCH"),
      start (p.apvts, "sampleStart", "SAMPLE START", coral), end (p.apvts, "sampleEnd", "SAMPLE END", coral)
{
    setTitle ("Sample source");
    for (Component* c : std::initializer_list<Component*> { &load, &clear, &enabled, &loop, &level, &pitch, &start, &end }) addAndMakeVisible (c);
    startTimerHz (10);
}

void SamplePanel::resized()
{
    load.setBounds (20, 52, 130, 28); clear.setBounds (156, 52, 80, 28);
    enabled.setBounds (940, 52, 116, 28); loop.setBounds (1064, 52, 116, 28);
    int x = 20;
    for (auto* dial : { &level, &pitch, &start, &end }) { dial->setBounds (x, 430, 100, 88); x += 120; }
}

void SamplePanel::paint (Graphics& g)
{
    surface (g, getLocalBounds());
    g.setColour (mint); g.fillRoundedRectangle (14, 16, 4, 13, 2);
    label (g, "SAMPLE", { 27, 10, 400, 24 }, 13, text, Justification::centredLeft, true);
    badge (g, "> FILTER 1", { getWidth() - 86, 13, 72, 20 }, mint);
    const auto sample = processor.sample();
    const String heading = processor.sampleMissing() ? "MISSING / " + processor.samplePath().getFullPathName()
                         : sample != nullptr ? processor.sampleName() + "  /  " + String ((double) sample->data.size() / sample->rate, 2) + " s  /  "
                                                   + String (sample->rate / 1000.0, 1) + " kHz"
                                             : String ("NO SAMPLE / LOAD A WAV, AIFF OR FLAC FILE");
    label (g, heading, { 250, 52, 680, 28 }, 11, processor.sampleMissing() ? coral : muted);

    surface (g, waveform, inset);
    const auto r = waveform.toFloat().reduced (10);
    if (! overview.empty())
    {
        const float a = std::min (shown[0], shown[1]), b = std::max (shown[0], shown[1]);
        g.setColour (coral.withAlpha (.08f));
        g.fillRect (r.withX (r.getX() + a * r.getWidth()).withWidth ((b - a) * r.getWidth()));
        g.setColour (mint.withAlpha (.8f));
        for (size_t x = 0; x < overview.size(); ++x)
        {
            const float h = overview[x] * r.getHeight() * .5f;
            g.drawVerticalLine ((int) (r.getX() + (float) x), r.getCentreY() - h, r.getCentreY() + h);
        }
        g.setColour (coral);
        for (float edge : { a, b }) g.fillRect (r.getX() + edge * r.getWidth() - 1, r.getY(), 2.0f, r.getHeight());
        label (g, shown[2] >= .5f ? "LOOP: START > END, CROSSFADED" : "ONE-SHOT: PLAYS START > END", { 30, 104, 400, 16 }, 9, coral);
    }
    label (g, status.isNotEmpty() ? status : "Note C4 plays the file at its own pitch. Start and end move with the next loop pass or note.",
           { 520, 440, 660, 20 }, 11, status.isNotEmpty() ? coral : muted);
    label (g, "The patch keeps a reference to the file; move the file and the patch reports it missing.", { 520, 466, 660, 20 }, 11, muted);
}

void SamplePanel::timerCallback()
{
    const auto now = processor.sampleRevision.load (std::memory_order_relaxed);
    const std::array<float, 3> region { processor.apvts.getRawParameterValue ("sampleStart")->load(),
                                        processor.apvts.getRawParameterValue ("sampleEnd")->load(),
                                        processor.apvts.getRawParameterValue ("sampleLoop")->load() };
    if (now == revision && region == shown) return;
    if (now != revision)
    {
        revision = now;
        overview.clear();
        if (const auto sample = processor.sample())
        {
            const int columns = waveform.getWidth() - 20;
            overview.assign ((size_t) columns, 0.0f);
            for (int x = 0; x < columns; ++x)
            {
                const auto from = sample->data.size() * (size_t) x / (size_t) columns, to = sample->data.size() * (size_t) (x + 1) / (size_t) columns;
                for (auto i = from; i < to; ++i) overview[(size_t) x] = std::max (overview[(size_t) x], std::abs (sample->data[i]));
            }
        }
    }
    shown = region;
    repaint();
}

void SamplePanel::choose()
{
    chooser = std::make_unique<FileChooser> ("Load sample", File::getSpecialLocation (File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == File()) return;
        const WaveProcessor::ScopedEdit edit (processor, "SAMPLE");
        status = processor.loadSample (file).toUpperCase();
        if (status.isEmpty())
            if (auto* on = processor.apvts.getParameter ("sampleOn"); on->getValue() < .5f)
            {
                on->beginChangeGesture(); on->setValueNotifyingHost (1); on->endChangeGesture();
            }
        repaint();
    });
}
}
