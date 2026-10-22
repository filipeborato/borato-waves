#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

namespace bw
{
// A wavetable is N frames of kSize samples. Each frame is pre-rendered at kLevels band limits
// (level l keeps harmonics 1..kSize/2 >> l); playback selects the appropriate bandwidth.
struct Wavetable
{
    static constexpr int kOrder = 11, kSize = 1 << kOrder, kLevels = kOrder; // last level = 1 harmonic
    static constexpr int kStride = kSize + 1;                                // +1 guard sample for interpolation

    int numFrames = 0;
    std::vector<float> data;
    size_t bytes() const noexcept { return data.size() * sizeof (float); }

    static int maxHarmonic (int level) { return (kSize / 2) >> level; }

    // Richest level whose top harmonic stays at or below Nyquist.
    static int levelFor (double freq, double sampleRate)
    {
        const double allowed = sampleRate * 0.5 / freq;
        int l = 0;
        while (l < kLevels - 1 && maxHarmonic (l) > allowed)
            ++l;
        return l;
    }

    const float* ptr (int frame, int level) const { return data.data() + ((size_t) frame * kLevels + (size_t) level) * kStride; }
    float* ptr (int frame, int level) { return data.data() + ((size_t) frame * kLevels + (size_t) level) * kStride; }

    // Each input frame must hold kSize samples (one cycle). Frames are normalised to peak 1 at level 0.
    void setFrames (const std::vector<std::vector<float>>& cycles)
    {
        numFrames = (int) cycles.size();
        data.assign ((size_t) numFrames * kLevels * kStride, 0.0f);
        const juce::dsp::FFT fft (kOrder);
        for (int f = 0; f < numFrames; ++f)
            setFrame (f, cycles[(size_t) f], fft);
    }

    // Rebuilds every band level of one existing frame from a kSize-sample cycle.
    void setFrame (int f, const std::vector<float>& cycle) { setFrame (f, cycle, juce::dsp::FFT (kOrder)); }
    void setFrame (int f, const std::vector<float>& cycle, const juce::dsp::FFT& fft)
    {
        std::vector<float> spec (2 * kSize, 0.0f), buf (2 * kSize);
        std::copy (cycle.begin(), cycle.begin() + kSize, spec.begin());
        fft.performRealOnlyForwardTransform (spec.data());
        spec[0] = spec[1] = 0.0f; // drop DC

        float peak = 0.0f;
        for (int l = 0; l < kLevels; ++l)
        {
            buf = spec;
            for (int k = maxHarmonic (l) + 1; k <= kSize / 2; ++k)
            {
                buf[(size_t) 2 * k] = buf[(size_t) 2 * k + 1] = 0.0f;
                if (k < kSize / 2)
                    buf[(size_t) 2 * (kSize - k)] = buf[(size_t) 2 * (kSize - k) + 1] = 0.0f;
            }
            fft.performRealOnlyInverseTransform (buf.data());

            auto* dst = ptr (f, l);
            std::copy (buf.begin(), buf.begin() + kSize, dst);
            dst[kSize] = dst[0];
            if (l == 0)
                for (int i = 0; i < kSize; ++i)
                    peak = std::max (peak, std::abs (dst[i]));
        }

        if (peak > 0.0f)
            for (int l = 0; l < kLevels; ++l)
                juce::FloatVectorOperations::multiply (ptr (f, l), 1.0f / peak, kStride);
    }

    // phase in [0, 1), pos in [0, 1] across frames; linear interpolation in time and between frames.
    float read (double phase, float pos, int level) const
    {
        const float fp = pos * (float) (numFrames - 1);
        const int f0 = (int) fp, f1 = std::min (f0 + 1, numFrames - 1);
        const float ff = fp - (float) f0;
        const double x = phase * kSize;
        const int i = (int) x;
        const float xf = (float) (x - i);
        auto at = [&] (int f) { auto* d = ptr (f, level); return d[i] + xf * (d[i + 1] - d[i]); };
        const float a = at (f0);
        return a + ff * (at (f1) - a);
    }

    // Default table: sine -> saw -> square across 16 frames.
    static Wavetable makeBasic (int frames = 16)
    {
        std::vector<std::vector<float>> t ((size_t) frames, std::vector<float> (kSize));
        for (int f = 0; f < frames; ++f)
        {
            const float m = (float) f / (float) (frames - 1);
            for (int i = 0; i < kSize; ++i)
            {
                const float p = (float) i / kSize;
                const float sine = std::sin (juce::MathConstants<float>::twoPi * p);
                const float saw = p < 0.5f ? 2.0f * p : 2.0f * p - 2.0f; // starts at 0 like the sine
                const float square = p < 0.5f ? 1.0f : -1.0f;
                t[(size_t) f][(size_t) i] = m < 0.5f ? juce::jmap (m * 2.0f, sine, saw)
                                                     : juce::jmap (m * 2.0f - 1.0f, saw, square);
            }
        }
        Wavetable w;
        w.setFrames (t);
        return w;
    }
};
} // namespace bw
