#pragma once
#include "Sample.h"

namespace bw
{
constexpr int kMaxGrainsPerVoice = 32;
struct DeterministicRng
{
    uint64_t state = 1;
    double next () noexcept
    {
        uint64_t z = (state += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return (double) ((z ^ (z >> 31)) >> 11) * 0x1.0p-53;
    }
};
struct GranularScheduler
{
    double samplesUntilNextGrain = 0;
    bool tick (double rate, double density) noexcept
    {
        const bool due = samplesUntilNextGrain <= 0;
        if (due) samplesUntilNextGrain += rate / (std::isfinite (density) ? juce::jlimit (1.0, 100.0, density) : 1.0);
        samplesUntilNextGrain -= 1;
        return due;
    }
};
struct GrainSource
{
    const Sample* sample = nullptr;
    double from = 0, to = 0, ratio = 1; // source frames / output frame / MIDI frequency
    float weight = 0;
};
using GrainSources = std::array<GrainSource, 2>;
struct GrainControls
{
    float density = 40, sizeMs = 100, position = .5f, scan = 0, spread = 0;
};
struct Grain
{
    GrainSources sources {};
    std::array<double, 2> positions {};
    int age = 0, duration = 1;
    bool active = false;
};
struct GrainDisplaySnapshot
{
    struct Visual { std::atomic<float> normPos { 0 }, normPhase { 0 }, amp { 0 }; };
    std::array<Visual, kMaxGrainsPerVoice> grains;
    std::atomic<int> count { 0 };
    static_assert (std::atomic<float>::is_always_lock_free && std::atomic<int>::is_always_lock_free);
};
inline const auto hannWindow = []
{
    std::array<float, 1025> table {};
    for (size_t i = 0; i < table.size (); ++i)
        table[i] = (float) (.5 * (1 - std::cos (juce::MathConstants<double>::twoPi * (double) i / 1024.0)));
    table.front () = table.back () = 0;
    return table;
} ();
inline float grainWindow (double phase) noexcept
{
    const double x = juce::jlimit (0.0, 1024.0, phase * 1024);
    const auto index = (size_t) x;
    if (index >= 1024) return 0;
    return hannWindow[index] + (float) (x - (double) index) * (hannWindow[index + 1] - hannWindow[index]);
}
class GranularVoiceEngine
{
public:
    void reset (double sampleRate, uint64_t seed) noexcept
    {
        rate = std::isfinite (sampleRate) ? juce::jlimit (1000.0, 768000.0, sampleRate) : 48000.0; rng.state = seed; scheduler = {}; grains = {};
        scanOffset = 0; saturated = false; spawned = 0;
    }
    std::pair<float, float> process (const GrainSources& sources, const GrainControls& controls,
                                    double frequency, bool emit = true) noexcept
    {
        if (emit && scheduler.tick (rate, controls.density))
        {
            auto free = std::find_if (grains.begin (), grains.end (), [] (const Grain& g) { return ! g.active; });
            const double randomOffset = (rng.next () * 2 - 1) * (std::isfinite (controls.spread) ? juce::jlimit (0.0f, 1.0f, controls.spread) : 0.0f);
            if (free == grains.end ()) saturated = true; // Refuse, never steal an audible grain.
            else if (sources[0].sample || sources[1].sample)
            {
                *free = {}; free->sources = sources; free->active = true;
                free->duration = std::max (2, (int) std::round (rate * (std::isfinite (controls.sizeMs) ? juce::jlimit (5.0f, 500.0f, controls.sizeMs) : 100.0f) * .001));
                double position = (std::isfinite (controls.position) ? juce::jlimit (0.0f, 1.0f, controls.position) : .5f) + scanOffset;
                if (position < 0 || position > 1) position -= std::floor (position);
                position = juce::jlimit (0.0, 1.0, position + randomOffset);
                for (size_t l = 0; l < 2; ++l)
                    free->positions[l] = sources[l].from + position * std::max (0.0, sources[l].to - sources[l].from - 1);
                ++spawned;
            }
        }
        if (const auto& source = sources[0]; source.sample && source.to > source.from)
        {
            scanOffset += (source.sample->rate / rate) / (source.to - source.from) * (std::isfinite (controls.scan) ? juce::jlimit (-2.0f, 2.0f, controls.scan) : 0.0f);
            // Keep negative scan continuous around zero instead of clamping it to the start.
            if (scanOffset > 1 || scanOffset < -1) scanOffset -= std::trunc (scanOffset);
        }
        double left = 0, right = 0, windowSum = 0;
        for (auto& grain : grains)
        {
            if (! grain.active) continue;
            const float window = grainWindow ((double) grain.age / grain.duration);
            windowSum += window;
            for (size_t l = 0; l < 2; ++l)
            {
                const auto& source = grain.sources[l];
                if (! source.sample || source.weight <= 0) continue;
                const double increment = frequency * source.ratio;
                const auto value = source.sample->readFilteredStereo (grain.positions[l], increment, source.from, source.to);
                const double weight = window * source.weight;
                left += value.first * weight; right += value.second * weight;
                grain.positions[l] = std::min (source.to - 1, grain.positions[l] + increment);
            }
            if (++grain.age >= grain.duration) grain.active = false;
        }
        // Unity for overlapped DC; sparse clouds retain their Hann envelope. Denominator never approaches zero.
        const double normalise = 1.0 / std::max (1.0, windowSum);
        return { (float) (left * normalise), (float) (right * normalise) };
    }
    int activeCount () const noexcept
    { int count = 0; for (const auto& g : grains) count += g.active ? 1 : 0; return count; }
    bool referencesOnly (const GrainSources& sources) const noexcept
    {
        for (const auto& g : grains) if (g.active)
            for (const auto& s : g.sources)
                if (s.sample && s.sample != sources[0].sample && s.sample != sources[1].sample) return false;
        return true;
    }
    void publish (GrainDisplaySnapshot& display) const noexcept
    {
        for (size_t i = 0; i < grains.size (); ++i)
        {
            const auto& g = grains[i]; auto& visual = display.grains[i];
            const double phase = (double) g.age / g.duration;
            const size_t sourceIndex = g.sources[1].weight > g.sources[0].weight ? 1u : 0u;
            const auto* sample = g.sources[sourceIndex].sample;
            visual.normPos.store (g.active && sample ? (float) (g.positions[sourceIndex] / sample->numFrames ()) : 0, std::memory_order_relaxed);
            visual.normPhase.store (g.active ? (float) phase : 0, std::memory_order_relaxed);
            visual.amp.store (g.active ? grainWindow (phase) : 0, std::memory_order_relaxed);
        }
        display.count.store (activeCount (), std::memory_order_relaxed);
    }
    bool saturated = false;
    uint64_t spawned = 0;
private:
    std::array<Grain, kMaxGrainsPerVoice> grains {};
    GranularScheduler scheduler;
    DeterministicRng rng;
    double rate = 48000, scanOffset = 0;
};
} // namespace bw
