#pragma once
#include "Wavetable.h"
#include <array>

namespace bw
{
// Construct once on the control thread; every voice shares these immutable samples.
class WavetableBank
{
public:
    struct Descriptor
    {
        const char* name;
        const char* category;
        const char* description;
    };

    static constexpr int kCount = 8;
    inline static constexpr std::array<Descriptor, kCount> descriptions {{
        { "Basic Shapes", "Classic", "Original sine, saw and square morph" },
        { "Sine Triangle", "Classic", "Sine to triangle with alternating odd harmonics" },
        { "Saw Harmonics", "Analog", "Progressively brighter harmonic saw spectrum" },
        { "Odd Harmonics", "Analog", "Sine to square through odd harmonic growth" },
        { "Pulse Sweep", "Analog", "Pulse width sweeps from 50% to 8%" },
        { "Formant Sweep", "Digital", "Harmonic resonance sweeps above a sine fundamental" },
        { "Spectral Bells", "Digital", "Integer harmonic clusters with bell-like colour" },
        { "Phase Bend", "Digital", "Sine with increasing periodic phase distortion" }
    }};

    // The factory bank is immutable: build it once per process.
    static const WavetableBank& shared()
    {
        static const WavetableBank bank;
        return bank;
    }

    WavetableBank()
    {
        tables[0] = Wavetable::makeBasic();
        constexpr int harmonics = 64;
        std::vector<std::vector<float>> basis (harmonics + 1,
                                               std::vector<float> (Wavetable::kSize));
        for (int h = 1; h <= harmonics; ++h)
            for (int i = 0; i < Wavetable::kSize; ++i)
                basis[(size_t) h][(size_t) i] = (float) std::sin (
                    juce::MathConstants<double>::twoPi * h * i / Wavetable::kSize);

        for (int bank = 1; bank < kCount; ++bank)
        {
            constexpr int frames = 16;
            std::vector<std::vector<float>> cycles (frames, std::vector<float> (Wavetable::kSize));
            for (int f = 0; f < frames; ++f)
            {
                const double morph = (double) f / (frames - 1);
                auto& cycle = cycles[(size_t) f];
                if (bank == 4 || bank == 7)
                {
                    for (int i = 0; i < Wavetable::kSize; ++i)
                    {
                        const double phase = (double) i / Wavetable::kSize;
                        cycle[(size_t) i] = bank == 4
                            ? (phase < 0.5 - 0.42 * morph ? 1.0f : -1.0f)
                            : (float) std::sin (juce::MathConstants<double>::twoPi * phase
                                               + 3.0 * morph * std::sin (juce::MathConstants<double>::twoPi * phase));
                    }
                }
                else
                {
                    for (int h = 1; h <= harmonics; ++h)
                    {
                        double amplitude = 0.0;
                        const double growth = juce::jlimit (0.0, 1.0, 1.0 + morph * (harmonics - 1) - h);
                        if (bank == 1 && (h & 1) != 0)
                            amplitude = h == 1 ? 1.0 : morph * ((h % 4) == 1 ? 1.0 : -1.0) / (h * h);
                        else if (bank == 2)
                            amplitude = h == 1 ? 1.0 : growth / h;
                        else if (bank == 3 && (h & 1) != 0)
                            amplitude = h == 1 ? 1.0 : growth / h;
                        else if (bank == 5)
                        {
                            const double distance = (h - (3.0 + 25.0 * morph)) / 2.2;
                            amplitude = h == 1 ? 0.7 : std::exp (-0.5 * distance * distance) / std::sqrt ((double) h);
                        }
                        else if (bank == 6)
                        {
                            const double distance = (h - (5.0 + 18.0 * morph)) / 1.3;
                            amplitude = h == 1 ? 0.6 : ((h % 3) == 1 ? 1.0 : 0.15)
                                * std::exp (-0.5 * distance * distance);
                        }
                        if (amplitude != 0.0)
                            for (int i = 0; i < Wavetable::kSize; ++i)
                                cycle[(size_t) i] += (float) amplitude * basis[(size_t) h][(size_t) i];
                    }
                }
                // setFrames removes DC, normalises and builds all spectral mip levels.
            }
            tables[(size_t) bank].setFrames (cycles);
        }
    }

    const Wavetable& operator[] (int index) const noexcept
    {
        return tables[(size_t) juce::jlimit (0, kCount - 1, index)];
    }

    static juce::StringArray names()
    {
        juce::StringArray result;
        for (const auto& descriptor : descriptions)
            result.add (descriptor.name);
        return result;
    }

private:
    std::array<Wavetable, kCount> tables;
};
} // namespace bw
