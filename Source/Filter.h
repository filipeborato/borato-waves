#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace bw
{
// Stereo zero-delay-feedback (TPT) state-variable filter. Its digital response is the analogue
// prototype evaluated at the bilinear-prewarped frequency, so magnitude() is exact for the DSP.
class VoiceFilter
{
public:
    enum Mode { low12, low24, high12, high24, band12, notch12 };
    static juce::StringArray modeNames() { return { "Low 12", "Low 24", "High 12", "High 24", "Band 12", "Notch 12" }; }
    // Second 24 dB section; with Q = 1.3066 on the first, the cascade is a 4th-order Butterworth.
    static constexpr double kSecondQ = 0.5412;

    void setMode (int m) noexcept { mode = juce::jlimit (0, 5, m); }
    int getMode() const noexcept { return mode; }

    void set (double hz, double q, double sampleRate) noexcept
    {
        first.set (hz, q, sampleRate);
        second.set (hz, kSecondQ, sampleRate);
    }

    void reset() noexcept { first.reset(); second.reset(); }

    float process (int channel, float x) noexcept
    {
        const auto a = first.tick (channel, x);
        switch (mode)
        {
            case low12:   return a.lp;
            case low24:   return second.tick (channel, a.lp).lp;
            case high12:  return a.hp;
            case high24:  return second.tick (channel, a.hp).hp;
            case band12:  return first.k * a.bp;
            default:      return x - first.k * a.bp;
        }
    }

    static double magnitude (int mode, double hz, double cutoff, double q, double sampleRate)
    {
        const auto warp = [sampleRate] (double f) { return std::tan (juce::MathConstants<double>::pi * juce::jmin (f, sampleRate * 0.4999) / sampleRate); };
        const double w = warp (hz) / warp (cutoff);
        const auto section = [w] (double k, double numerator)
        { return numerator / std::sqrt ((1 - w * w) * (1 - w * w) + k * k * w * w); };
        const double k = 1 / q;
        switch (mode)
        {
            case low12:  return section (k, 1);
            case low24:  return section (k, 1) * section (1 / kSecondQ, 1);
            case high12: return section (k, w * w);
            case high24: return section (k, w * w) * section (1 / kSecondQ, w * w);
            case band12: return section (k, k * w);
            default:     return section (k, std::abs (1 - w * w));
        }
    }

private:
    struct Section
    {
        struct Out { float lp, bp, hp; };
        float k = 1.4142f, a1 = 1, a2 = 0, a3 = 0;
        std::array<float, 2> s1 {}, s2 {};

        void set (double hz, double q, double sampleRate) noexcept
        {
            const double g = std::tan (juce::MathConstants<double>::pi * hz / sampleRate);
            k = (float) (1 / q);
            a1 = (float) (1 / (1 + g * (g + k)));
            a2 = (float) g * a1;
            a3 = (float) g * a2;
        }
        void reset() noexcept { s1 = {}; s2 = {}; }
        Out tick (int channel, float x) noexcept
        {
            auto& ic1 = s1[(size_t) channel];
            auto& ic2 = s2[(size_t) channel];
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1;
            ic2 = 2 * v2 - ic2;
            return { v2, v1, x - k * v1 - v2 };
        }
    };
    Section first, second;
    int mode = low12;
};
} // namespace bw
