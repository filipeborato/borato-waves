#include "TableOscillator.h"
#include <cassert>
#include <cstdio>

static void testBank (const bw::WavetableBank& bank)
{
    using W = bw::Wavetable;
    const auto legacy = W::makeBasic();
    assert (legacy.data == bank[0].data);
    assert (&bank[-1] == &bank[0]);
    assert (&bank[99] == &bank[bw::WavetableBank::kCount - 1]);
    assert (bw::WavetableBank::names().size() == bw::WavetableBank::kCount);

    juce::dsp::FFT fft (W::kOrder);
    std::vector<float> spectrum (2 * W::kSize);
    for (int b = 0; b < bw::WavetableBank::kCount; ++b)
    {
        const auto& table = bank[b];
        assert (table.numFrames == 16);
        for (int f = 0; f < table.numFrames; ++f)
            for (int level = 0; level < W::kLevels; ++level)
            {
                const auto* samples = table.ptr (f, level);
                double sum = 0.0;
                float peak = 0.0f;
                for (int i = 0; i < W::kSize; ++i)
                {
                    assert (std::isfinite (samples[i]));
                    sum += samples[i];
                    peak = std::max (peak, std::abs (samples[i]));
                }
                assert (std::abs (sum / W::kSize) < 1.0e-6);
                assert (samples[0] == samples[W::kSize]);
                assert (peak < 2.0f); // spectral truncation may overshoot a normalised frame
                if (level == 0)
                    assert (std::abs (peak - 1.0f) < 1.0e-5f);

                std::fill (spectrum.begin(), spectrum.end(), 0.0f);
                std::copy (samples, samples + W::kSize, spectrum.begin());
                fft.performRealOnlyForwardTransform (spectrum.data());
                for (int h = W::maxHarmonic (level) + 1; h <= W::kSize / 2; ++h)
                    assert (std::hypot (spectrum[(size_t) 2 * h], spectrum[(size_t) 2 * h + 1])
                            / W::kSize < 1.0e-6f);
            }

        // Halfway between adjacent frames and sample positions, including the wrap guard.
        for (int i : { 0, 117, W::kSize - 1 })
        {
            const float* a = table.ptr (4, 2);
            const float* c = table.ptr (5, 2);
            const float expected = (a[i] + a[i + 1] + c[i] + c[i + 1]) * 0.25f;
            assert (std::abs (table.read ((i + 0.5) / W::kSize, 4.5f / 15.0f, 2) - expected) < 1.0e-6f);
        }

        for (int other = 0; other < b; ++other)
        {
            double difference = 0.0;
            for (int f = 0; f < table.numFrames; ++f)
                for (int i = 0; i < W::kSize; i += 8)
                    difference += std::abs (table.ptr (f, 0)[i] - bank[other].ptr (f, 0)[i]);
            assert (difference > 1.0);
        }
    }
}

static void testTransitions (const bw::WavetableBank& bank)
{
    for (double rate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        bw::TableOscillator oscillator (bank);
        oscillator.prepare (rate);
        const int duration = (int) std::ceil (rate * 0.015);
        constexpr double phase = 0.231;
        constexpr float position = 0.61f;
        const float source = bank[0].read (phase, position, 2);
        const float destination = bank[4].read (phase, position, 2);
        oscillator.setTable (4);
        for (int i = 0; i < duration; ++i)
        {
            const float expected = source + (float) i / duration * (destination - source);
            assert (std::abs (oscillator.read (phase, position, 2) - expected) < 1.0e-6f);
        }
        assert (oscillator.read (phase, position, 2) == destination);

        // Frequent targets do not restart the fade or replace either audible endpoint.
        float previous = destination;
        for (int i = 0; i < duration * 4; ++i)
        {
            oscillator.setTable ((i / 3) % bw::WavetableBank::kCount);
            const float output = oscillator.read (phase, position, 2);
            assert (std::isfinite (output));
            assert (std::abs (output - previous) < 4.0f / duration + 1.0e-6f);
            previous = output;
        }
        oscillator.setTable (7);
        for (int i = 0; i < 2 * duration + 1; ++i)
            oscillator.read (phase, position, 2);
        assert (oscillator.read (phase, position, 2) == bank[7].read (phase, position, 2));
        oscillator.reset (-1);
        assert (oscillator.read (phase, position, 2) == source);

        // Oscillating phase remains finite with note frequencies approaching Nyquist.
        for (double frequency : { 20.0, 440.0, rate * 0.49 })
        {
            double runningPhase = 0.0;
            const int level = bw::Wavetable::levelFor (frequency, rate);
            for (int i = 0; i < 4096; ++i)
            {
                oscillator.setTable ((i / 17) % bw::WavetableBank::kCount);
                assert (std::isfinite (oscillator.read (runningPhase, position, level)));
                runningPhase += frequency / rate;
                runningPhase -= std::floor (runningPhase);
            }
        }
    }
}

int main()
{
    using W = bw::Wavetable;
    const auto wt = W::makeBasic();

    // Mip level choice: richest level that still keeps every harmonic at or below Nyquist.
    for (double sr : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double f = 20; f < sr / 2; f *= 1.05)
        {
            const int l = W::levelFor (f, sr);
            assert (l == W::kLevels - 1 || W::maxHarmonic (l) * f <= sr / 2);
            assert (l == 0 || W::maxHarmonic (l - 1) * f > sr / 2);
        }

    // Every frame is normalised to peak 1 at the full-band level.
    for (int f = 0; f < wt.numFrames; ++f)
    {
        float peak = 0;
        for (int i = 0; i < W::kSize; ++i)
            peak = std::max (peak, std::abs (wt.read ((double) i / W::kSize, (float) f / (float) (wt.numFrames - 1), 0)));
        assert (std::abs (peak - 1.0f) < 1e-3f);
    }

    // Frame 0 is a sine, so the 1-harmonic level reproduces it exactly.
    for (int i = 0; i < W::kSize; i += 64)
        assert (std::abs (wt.read ((double) i / W::kSize, 0, W::kLevels - 1)
                          - std::sin (juce::MathConstants<float>::twoPi * (float) i / W::kSize)) < 1e-3f);

    const bw::WavetableBank bank;
    testBank (bank);
    testTransitions (bank);
    std::puts ("ok: legacy table, eight banks, spectra, interpolation and smooth transitions");
}
