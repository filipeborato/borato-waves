#pragma once
#include "Wavetable.h"
#include "UserWavetable.h"
#include <array>
#include <memory>

namespace bw
{
// An editable copy of a wavetable's full-band cycles, owned by the message thread. build() makes a
// new immutable table to publish; only frames edited since the last build are band-limited again.
// Built frames are normalised (peak 1, no DC), and the edit adopts that result, so what the editor
// shows is what plays.
class TableEdit
{
public:
    static constexpr int kHarmonics = 32;

    TableEdit() : cycles (1, std::vector<float> (Wavetable::kSize)), dirty (1, true) // one sine frame
    {
        for (int i = 0; i < Wavetable::kSize; ++i) cycles[0][(size_t) i] = std::sin (juce::MathConstants<float>::twoPi * (float) i / Wavetable::kSize);
    }
    explicit TableEdit (const Wavetable& source)
    {
        for (int f = 0; f < source.numFrames; ++f)
            cycles.emplace_back (source.ptr (f, 0), source.ptr (f, 0) + Wavetable::kSize);
        dirty.assign (cycles.size(), true);
    }

    int frames() const noexcept { return (int) cycles.size(); }
    const std::vector<float>& frame (int f) const { return cycles[(size_t) f]; }

    // Magnitudes of harmonics 1..kHarmonics relative to the strongest of them.
    std::array<float, kHarmonics> harmonics (int f) const
    {
        const auto spectrum = transform (cycles[(size_t) f]);
        std::array<float, kHarmonics> result {};
        float strongest = 0;
        for (int k = 1; k <= kHarmonics; ++k) strongest = std::max (strongest, result[(size_t) k - 1] = magnitude (spectrum, k));
        if (strongest > 0) for (auto& m : result) m /= strongest;
        return result;
    }

    // Sets harmonic k (1-based) to `level` times the strongest of the first kHarmonics, keeping its
    // phase; a silent harmonic starts as a sine.
    void setHarmonic (int f, int k, float level)
    {
        auto spectrum = transform (cycles[(size_t) f]);
        float strongest = 0;
        for (int h = 1; h <= kHarmonics; ++h) strongest = std::max (strongest, magnitude (spectrum, h));
        if (strongest <= 0) strongest = Wavetable::kSize / 2.0f; // an empty cycle: unit sines
        const float target = juce::jlimit (0.0f, 1.0f, level) * strongest, current = magnitude (spectrum, k);
        float re = 0, im = -target;
        if (current > 1.0e-6f) { re = spectrum[(size_t) 2 * k] * target / current; im = spectrum[(size_t) 2 * k + 1] * target / current; }
        spectrum[(size_t) 2 * k] = re; spectrum[(size_t) 2 * k + 1] = im;
        spectrum[(size_t) 2 * (Wavetable::kSize - k)] = re; spectrum[(size_t) 2 * (Wavetable::kSize - k) + 1] = -im;
        juce::dsp::FFT (Wavetable::kOrder).performRealOnlyInverseTransform (spectrum.data());
        std::copy (spectrum.begin(), spectrum.begin() + Wavetable::kSize, cycles[(size_t) f].begin());
        dirty[(size_t) f] = true;
    }

    // A straight stroke from (x0, y0) to (x1, y1): x in [0, 1] across the cycle, y in [-1, 1].
    void draw (int f, float x0, float y0, float x1, float y1)
    {
        if (x0 > x1) { std::swap (x0, x1); std::swap (y0, y1); }
        auto& cycle = cycles[(size_t) f];
        const int first = juce::jlimit (0, Wavetable::kSize - 1, (int) (x0 * Wavetable::kSize));
        const int last = juce::jlimit (0, Wavetable::kSize - 1, (int) (x1 * Wavetable::kSize));
        for (int i = first; i <= last; ++i)
        {
            const float t = last == first ? 1.0f : (float) (i - first) / (float) (last - first);
            cycle[(size_t) i] = juce::jlimit (-1.0f, 1.0f, y0 + t * (y1 - y0));
        }
        dirty[(size_t) f] = true;
    }

    // Duplicates frame f after itself, up to kMaxUserFrames.
    bool insertFrame (int f)
    {
        if (frames() >= kMaxUserFrames) return false;
        cycles.insert (cycles.begin() + f + 1, cycles[(size_t) f]);
        dirty.assign (cycles.size(), true);
        return true;
    }
    bool removeFrame (int f)
    {
        if (frames() <= 1) return false;
        cycles.erase (cycles.begin() + f);
        dirty.assign (cycles.size(), true);
        return true;
    }

    bool changed() const { return std::find (dirty.begin(), dirty.end(), true) != dirty.end(); }

    std::shared_ptr<const Wavetable> build()
    {
        auto table = std::make_shared<Wavetable>();
        if (built != nullptr && built->numFrames == frames())
        {
            *table = *built; // ponytail: copies every frame; share unedited frames if large tables edit slowly
            for (int f = 0; f < frames(); ++f) if (dirty[(size_t) f]) table->setFrame (f, cycles[(size_t) f]);
        }
        else table->setFrames (cycles);
        for (int f = 0; f < frames(); ++f)
            if (dirty[(size_t) f]) std::copy (table->ptr (f, 0), table->ptr (f, 0) + Wavetable::kSize, cycles[(size_t) f].begin());
        dirty.assign (cycles.size(), false);
        built = table;
        return table;
    }

private:
    static std::vector<float> transform (const std::vector<float>& cycle)
    {
        std::vector<float> spectrum (2 * Wavetable::kSize);
        std::copy (cycle.begin(), cycle.end(), spectrum.begin());
        juce::dsp::FFT (Wavetable::kOrder).performRealOnlyForwardTransform (spectrum.data());
        return spectrum;
    }
    static float magnitude (const std::vector<float>& spectrum, int k)
    {
        return std::hypot (spectrum[(size_t) 2 * k], spectrum[(size_t) 2 * k + 1]);
    }

    std::vector<std::vector<float>> cycles;
    std::vector<bool> dirty;
    std::shared_ptr<const Wavetable> built;
};

// Frames back to back as a 32-bit float WAV of kSize-sample cycles, which importWavetable reads back.
inline bool exportTable (const Wavetable& table, const juce::File& file)
{
    juce::AudioBuffer<float> audio (1, table.numFrames * Wavetable::kSize);
    for (int f = 0; f < table.numFrames; ++f) audio.copyFrom (0, f * Wavetable::kSize, table.ptr (f, 0), Wavetable::kSize);
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr) return false;
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (44100).withNumChannels (1).withBitsPerSample (32)
                             .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    const auto writer = juce::WavAudioFormat().createWriterFor (stream, options);
    return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}
} // namespace bw
