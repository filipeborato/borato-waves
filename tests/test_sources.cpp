#include "PluginProcessor.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

static void set (WaveProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.apvts.getParameter (id);
    assert (parameter != nullptr);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

static std::array<std::vector<float>, 2> render (WaveProcessor& processor, int note = 69, double rate = 48000, int wheel = 8192)
{
    processor.prepareToPlay (rate, 128);
    std::array<std::vector<float>, 2> result;
    for (int block = 0; block < 160; ++block)
    {
        juce::AudioBuffer<float> audio (2, 128);
        juce::MidiBuffer midi;
        if (block == 0)
        {
            midi.addEvent (juce::MidiMessage::pitchWheel (1, wheel), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
        }
        processor.processBlock (audio, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                assert (std::isfinite (audio.getSample (ch, i)));
                if (block >= 32) result[(size_t) ch].push_back (audio.getSample (ch, i));
            }
    }
    return result;
}

static double energy (const std::vector<float>& audio)
{
    double result = 0;
    for (float value : audio) result += value * value;
    return result / (double) audio.size();
}

static double frequency (const std::vector<float>& audio, double rate = 48000)
{
    std::vector<size_t> crossings;
    for (size_t i = 1; i < audio.size(); ++i)
        if (audio[i - 1] <= 0 && audio[i] > 0) crossings.push_back (i);
    assert (crossings.size() > 2);
    return (double) (crossings.size() - 1) * rate / (double) (crossings.back() - crossings.front());
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    WaveProcessor p;
    set (p, "attack", 0.001f); set (p, "decay", 0.001f); set (p, "sustain", 1);
    set (p, "cutoff", 18000); set (p, "level", 0.5f); set (p, "gain", -12);
    const auto a = render (p);
    assert (std::abs (frequency (a[0]) - 440) < 1);
    assert (energy (a[0]) > 0.001);
    for (size_t i = 0; i < a[0].size(); ++i) assert (std::abs (a[0][i] - a[1][i]) < 1.0e-6f);
    set (p, "oscAOn", 0);
    assert (energy (render (p)[0]) == 0);
    set (p, "oscBOn", 1);
    const auto b = render (p);
    for (size_t i = 0; i < a[0].size(); ++i) assert (std::abs (a[0][i] - b[0][i]) < 1.0e-6f);
    set (p, "oscBPan", -1);
    auto hardLeft = render (p);
    assert (energy (hardLeft[0]) > 0.001 && energy (hardLeft[1]) < 1.0e-12);
    set (p, "oscBPan", 1);
    auto hardRight = render (p);
    assert (energy (hardRight[1]) > 0.001 && energy (hardRight[0]) < 1.0e-12);
    set (p, "oscBPan", 0); set (p, "oscBPitch", 12);
    assert (std::abs (frequency (render (p)[0]) - 880) < 1);
    set (p, "oscBPitch", 0); set (p, "oscBFine", 100);
    assert (std::abs (frequency (render (p)[0]) - 440 * std::exp2 (1.0 / 12.0)) < 1);
    set (p, "oscBFine", 0);
    assert (std::abs (frequency (render (p, 69, 48000, 16383)[0]) - 440 * std::exp2 (2.0 / 12.0)) < 1);
    set (p, "oscAOn", 1);
    auto mixed = render (p);
    for (size_t i = 0; i < a[0].size(); ++i) assert (std::abs (mixed[0][i] - 2 * a[0][i]) < 2.0e-6f);
    set (p, "oscAOn", 0); set (p, "oscBOn", 0); set (p, "subOn", 1);
    assert (std::abs (frequency (render (p)[0]) - 220) < 1);
    set (p, "subOctave", -2);
    assert (std::abs (frequency (render (p)[0]) - 110) < 1);
    // MIDI 127 is above Nyquist at 8 kHz, but its -2 octave sub is valid.
    assert (energy (render (p, 127, 8000)[0]) > 1.0e-6);
    set (p, "subOn", 0); set (p, "oscAOn", 1);
    assert (energy (render (p, 127, 8000)[0]) == 0);
    set (p, "oscAOn", 0); set (p, "noiseOn", 1);
    const auto noise = render (p);
    assert (energy (noise[0]) > 1.0e-5);
    double mean = 0; for (float v : noise[0]) mean += v;
    assert (std::abs (mean / (double) noise[0].size()) < 0.002);

    // Continuous held-note automation, independent tables and extremes.
    for (double rate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        p.prepareToPlay (rate, 64);
        for (int block = 0; block < 250; ++block)
        {
            set (p, "oscAOn", 1); set (p, "oscBOn", 1);
            set (p, "wtTable", (float) (block % 8)); set (p, "wtTableB", (float) ((block + 3) % 8));
            set (p, "wtPos", (float) (block % 2)); set (p, "wtPosB", (float) ((block + 1) % 2));
            set (p, "oscBPitch", block % 2 ? -24.0f : 24.0f);
            set (p, "oscBPan", block % 2 ? -1.0f : 1.0f);
            juce::AudioBuffer<float> audio (2, 64); juce::MidiBuffer midi;
            if (block == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 100, 1.0f), 0);
            p.processBlock (audio, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 64; ++i) assert (std::isfinite (audio.getSample (ch, i)) && std::abs (audio.getSample (ch, i)) < 4);
        }
    }
    // Absent new IDs must not inherit the current enabled sources.
    auto state = p.apvts.copyState();
    for (const auto* id : { "oscAOn", "oscBOn", "subOn", "noiseOn", "oscBPitch", "wtTableB" })
        state.removeChild (state.getChildWithProperty ("id", id), nullptr);
    auto xml = state.createXml(); juce::MemoryBlock memory;
    juce::AudioProcessor::copyXmlToBinary (*xml, memory);
    p.setStateInformation (memory.getData(), (int) memory.getSize());
    assert (p.apvts.getRawParameterValue ("oscAOn")->load() == 1);
    for (const auto* id : { "oscBOn", "subOn", "noiseOn", "oscBPitch", "wtTableB" })
        assert (p.apvts.getRawParameterValue (id)->load() == 0);
    // Mono centre must preserve the original level, with a fresh filter state.
    set (p, "wtTable", 0); set (p, "wtPos", 0);
    auto layout = p.getBusesLayout();
    layout.outputBuses.set (0, juce::AudioChannelSet::mono());
    assert (p.setBusesLayout (layout));
    p.prepareToPlay (48000, 128);
    size_t monoIndex = 0;
    for (int block = 0; block < 160; ++block)
    {
        juce::AudioBuffer<float> audio (1, 128); juce::MidiBuffer midi;
        if (block == 0)
        {
            midi.addEvent (juce::MidiMessage::pitchWheel (1, 8192), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
        }
        p.processBlock (audio, midi);
        if (block >= 32)
            for (int i = 0; i < 128; ++i)
                assert (std::abs (audio.getSample (0, i) - a[0][monoIndex++]) < 2.0e-6f);
    }
    std::cout << "ok: independent sources, stereo pan, tuning, pitch bend, mixing, Nyquist, automation, state migration\n";
}
