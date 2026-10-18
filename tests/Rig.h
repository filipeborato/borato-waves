#pragma once
// Shared by the processBlock tests: MIDI in, audio out.
#include "PluginProcessor.h"
#include <cassert>
#include <cmath>
#include <vector>

namespace rig
{
// Drives the processor through processBlock only: MIDI in, audio out.
struct Rig
{
    explicit Rig (double r = 48000) : rate (r)
    {
        set ("attack", .001f); set ("decay", .001f); set ("sustain", 1); set ("release", .05f);
        set ("cutoff", 18000); set ("level", .5f); set ("gain", -12);
        processor.prepareToPlay (rate, kBlock);
    }
    void set (const char* id, float value)
    {
        auto* parameter = processor.apvts.getParameter (id);
        assert (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }
    void send (const juce::MidiMessage& message) { pending.addEvent (message, 0); }
    // Renders whole blocks; pending events land on the first sample. Returns the left channel.
    std::vector<float> run (double seconds)
    {
        std::vector<float> left;
        for (int block = 0; block < (int) std::ceil (seconds * rate / kBlock); ++block)
        {
            juce::AudioBuffer<float> audio (2, kBlock);
            processor.processBlock (audio, pending);
            pending.clear();
            for (int i = 0; i < kBlock; ++i)
            {
                assert (std::isfinite (audio.getSample (0, i)) && std::isfinite (audio.getSample (1, i)));
                left.push_back (audio.getSample (0, i));
            }
        }
        return left;
    }
    static constexpr int kBlock = 64;
    double rate;
    WaveProcessor processor;
    juce::MidiBuffer pending;
};

inline juce::MidiMessage on (int note, float velocity = 1) { return juce::MidiMessage::noteOn (1, note, velocity); }
inline juce::MidiMessage off (int note) { return juce::MidiMessage::noteOff (1, note); }
inline juce::MidiMessage pedal (bool down) { return juce::MidiMessage::controllerEvent (1, 64, down ? 127 : 0); }

inline double rms (const std::vector<float>& audio, size_t from, size_t to)
{
    double sum = 0;
    for (size_t i = from; i < to; ++i) sum += audio[i] * audio[i];
    return std::sqrt (sum / (double) (to - from));
}
inline bool silent (const std::vector<float>& audio, size_t from = 0)
{
    for (size_t i = from; i < audio.size(); ++i) if (audio[i] != 0) return false;
    return true;
}
inline float maxStep (const std::vector<float>& audio, size_t from, size_t to, float previous)
{
    float result = 0;
    for (size_t i = from; i < to; ++i) { result = std::max (result, std::abs (audio[i] - previous)); previous = audio[i]; }
    return result;
}

// Mean frequency from rising zero crossings in [from, to).
inline double frequency (const std::vector<float>& audio, size_t from, size_t to, double rate = 48000)
{
    std::vector<size_t> crossings;
    for (size_t i = from + 1; i < to; ++i) if (audio[i - 1] <= 0 && audio[i] > 0) crossings.push_back (i);
    assert (crossings.size() > 2);
    return (double) (crossings.size() - 1) * rate / (double) (crossings.back() - crossings.front());
}
inline double frequency (const std::vector<float>& audio) { return frequency (audio, audio.size() / 2, audio.size()); }
inline bool near (double hz, int note) { return std::abs (hz / juce::MidiMessage::getMidiNoteInHertz (note) - 1) < .005; }

// Energy per FFT bin of the first kSpectrumSize samples (Hann window); bin k is k * rate / kSpectrumSize Hz.
constexpr int kSpectrumOrder = 14, kSpectrumSize = 1 << kSpectrumOrder;
inline std::vector<double> spectrum (const std::vector<float>& audio)
{
    assert (audio.size() >= (size_t) kSpectrumSize);
    std::vector<float> data (2 * kSpectrumSize);
    for (int i = 0; i < kSpectrumSize; ++i) data[(size_t) i] = audio[(size_t) i] * (.5f - .5f * std::cos (juce::MathConstants<float>::twoPi * i / kSpectrumSize));
    juce::dsp::FFT (kSpectrumOrder).performFrequencyOnlyForwardTransform (data.data());
    std::vector<double> energy (kSpectrumSize / 2);
    for (size_t k = 0; k < energy.size(); ++k) energy[k] = (double) data[k] * data[k];
    return energy;
}

// Fraction of spectral energy away from the note's harmonics.
inline double inharmonic (const std::vector<float>& audio, double rate, int note)
{
    const auto energy = spectrum (audio);
    const double f0 = juce::MidiMessage::getMidiNoteInHertz (note), bin = rate / kSpectrumSize;
    double harmonic = 0, other = 0;
    for (size_t k = 1; k < energy.size(); ++k)
    {
        const double hz = (double) k * bin, distance = std::abs (hz - f0 * std::round (hz / f0));
        (distance <= 4 * bin ? harmonic : other) += energy[k];
    }
    return other / (harmonic + other);
}

inline float peak (const std::vector<float>& audio, size_t from = 0)
{
    float result = 0;
    for (size_t i = from; i < audio.size(); ++i) result = std::max (result, std::abs (audio[i]));
    return result;
}
}
