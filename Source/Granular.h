#pragma once
#include "Sample.h"

namespace bw
{
constexpr int kMaxGrainsPerVoice = 32;
constexpr int kMaxGrainBatch = 8;
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
        return tickHz (rate, std::isfinite (density) ? juce::jlimit (1.0, 100.0, density) : 1.0);
    }
    // Events at `hz`, sample-accurate across blocks: the fractional remainder carries to the next interval.
    bool tickHz (double rate, double hz) noexcept
    {
        const bool due = samplesUntilNextGrain <= 0;
        if (due) samplesUntilNextGrain += rate / hz;
        samplesUntilNextGrain -= 1;
        return due;
    }
};

// Density: Free (grains/s), BPM Sync (one event per division) or Target Overlap N (event rate N / (length x batch),
// so about N grains overlap). Length: Free (ms), BPM Sync (the division's duration) or Percent of the interval
// between events. Divisions use divisionBeats (dotted x1.5 length / x2/3 rate, triplet x2/3 / x1.5). Each event
// emits `batch` grains spread over its interval: Together (all at the event), Even (k/M), Exponential
// ((2^k - 1)/(2^M - 1)) or Random (uniform, seeded). With Target Overlap, Percent measures the Free density's
// interval, since the overlap rate itself depends on the length.
enum class GrainDensityMode { free = 0, sync, overlap };
enum class GrainLengthMode { free = 0, sync, percent };
enum class GrainDistribution { together = 0, even, exponential, random };
inline juce::StringArray grainDensityModeNames () { return { "Free Hz", "BPM Sync", "Target Overlap" }; }
inline juce::StringArray grainLengthModeNames () { return { "Free ms", "BPM Sync", "Percent" }; }
inline juce::StringArray grainDistributionNames () { return { "Together", "Even", "Exponential", "Random" }; }

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
    GrainDensityMode densityMode = GrainDensityMode::free;
    GrainLengthMode lengthMode = GrainLengthMode::free;
    GrainDistribution distribution = GrainDistribution::together;
    double densityBeats = .25, lengthBeats = .25; // sync divisions, in beats
    float overlap = 4, lengthPercent = 100;       // grains; percent of the event interval
    int batch = 1;
    float keyTrack = 0;                           // 0..1: length x 2^(-(note - root) / 12 x keyTrack)
    int note = 60, root = 60;
    bool loop = false;                            // grains wrap inside the region instead of holding its last frame
};
// Host or internal tempo; with a playing transport, synced events follow the ppq grid, otherwise the note.
struct GrainClock { double bpm = 120, ppq = 0; bool transport = false; };

struct GrainTiming { double eventHz = 40, lengthMs = 100; int batch = 1; };
inline GrainTiming resolveGrainTiming (const GrainControls& c, double bpm) noexcept
{
    auto finite = [] (double value, double fallback) { return std::isfinite (value) ? value : fallback; };
    bpm = juce::jlimit (20.0, 999.0, finite (bpm, 120));
    GrainTiming t;
    t.batch = juce::jlimit (1, kMaxGrainBatch, c.batch);
    const double freeHz = juce::jlimit (1.0, 100.0, finite (c.density, 1));
    const double beats = juce::jlimit (1.0 / 64, 64.0, finite (c.densityBeats, .25));
    const double baseHz = c.densityMode == GrainDensityMode::sync ? bpm / 60 / beats : freeHz;
    double lengthMs = juce::jlimit (5.0, 500.0, finite (c.sizeMs, 100));
    if (c.lengthMode == GrainLengthMode::sync) lengthMs = juce::jlimit (1.0 / 64, 64.0, finite (c.lengthBeats, .25)) * 60000 / bpm;
    else if (c.lengthMode == GrainLengthMode::percent) lengthMs = juce::jlimit (1.0, 400.0, finite (c.lengthPercent, 100)) * 10 / baseHz;
    if (const double track = juce::jlimit (0.0, 1.0, finite (c.keyTrack, 0)); track > 0)
        lengthMs *= std::exp2 (-(c.note - c.root) / 12.0 * track);
    t.lengthMs = juce::jlimit (5.0, 2000.0, lengthMs);
    t.eventHz = c.densityMode == GrainDensityMode::overlap ? juce::jlimit (1.0, (double) kMaxGrainsPerVoice, finite (c.overlap, 4)) / (t.lengthMs * .001 * t.batch) : baseHz;
    t.eventHz = juce::jlimit (1.0 / 64, 1000.0, t.eventHz);
    return t;
}
// Offset of grain k of an M-grain batch, as a fraction of the event interval.
inline double grainBatchOffset (GrainDistribution d, int k, int count, DeterministicRng& rng) noexcept
{
    if (count <= 1 || d == GrainDistribution::together) return 0;
    if (d == GrainDistribution::even) return (double) k / count;
    if (d == GrainDistribution::exponential) return (std::exp2 ((double) k) - 1) / (std::exp2 ((double) count) - 1);
    return rng.next ();
}

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
        scanOffset = 0; saturated = false; spawned = 0; pending = 0; syncPrimed = false; jumpPending = false;
    }
    // The next process call pre-populates the overlap it expects (at most the pool), with ages spread across
    // one grain length, so a sustained cloud sounds from the first sample.
    void requestJumpStart () noexcept { jumpPending = true; }

    std::pair<float, float> process (const GrainSources& sources, const GrainControls& controls,
                                    double frequency, bool emit = true, const GrainClock& clock = {}) noexcept
    {
        const auto timing = resolveGrainTiming (controls, clock.bpm);
        if (jumpPending && emit)
        {
            jumpPending = false;
            const int count = juce::jlimit (0, kMaxGrainsPerVoice, (int) std::lround (timing.eventHz * timing.batch * timing.lengthMs * .001));
            const int duration = std::max (2, (int) std::round (rate * timing.lengthMs * .001));
            for (int k = count - 1; k >= 0; --k) spawn (sources, controls, timing, frequency, (int) ((double) k / count * duration));
        }
        bool event = false;
        double interval = rate / timing.eventHz;
        if (emit)
        {
            if (controls.densityMode == GrainDensityMode::sync && clock.transport && std::isfinite (clock.ppq))
            {
                const double beats = juce::jlimit (1.0 / 64, 64.0, std::isfinite (controls.densityBeats) ? controls.densityBeats : .25);
                const double index = std::floor (clock.ppq / beats + 1.0e-9);
                event = ! syncPrimed || index != syncIndex;
                syncIndex = index; syncPrimed = true;
                interval = beats * 60 / juce::jlimit (20.0, 999.0, clock.bpm) * rate;
            }
            else event = scheduler.tickHz (rate, timing.eventHz);
        }
        // Batch grains scheduled by earlier events, before this sample's event adds new ones.
        for (int p = 0; p < pending;)
            if ((delays[(size_t) p] -= 1) < 1) { spawn (sources, controls, timing, frequency); delays[(size_t) p] = delays[(size_t) --pending]; }
            else ++p;
        if (event)
            for (int k = 0; k < timing.batch; ++k)
            {
                const double delay = grainBatchOffset (controls.distribution, k, timing.batch, rng) * interval;
                if (delay < 1) spawn (sources, controls, timing, frequency);
                else if (pending < (int) delays.size ()) delays[(size_t) pending++] = delay;
                else saturated = true;
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
                grain.positions[l] = advance (grain.positions[l], increment, source, controls.loop);
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
    static double advance (double position, double increment, const GrainSource& source, bool loop) noexcept
    {
        const double next = position + increment, last = source.to - 1;
        if (loop && next >= last && last > source.from) return source.from + std::fmod (next - source.from, last - source.from);
        return std::min (last, next);
    }
    void spawn (const GrainSources& sources, const GrainControls& controls, const GrainTiming& timing, double frequency, int age = 0) noexcept
    {
        auto free = std::find_if (grains.begin (), grains.end (), [] (const Grain& g) { return ! g.active; });
        const double randomOffset = (rng.next () * 2 - 1) * (std::isfinite (controls.spread) ? juce::jlimit (0.0f, 1.0f, controls.spread) : 0.0f);
        if (free == grains.end ()) { saturated = true; return; } // Refuse, never steal an audible grain.
        if (! sources[0].sample && ! sources[1].sample) return;
        *free = {}; free->sources = sources; free->active = true;
        free->duration = std::max (2, (int) std::round (rate * timing.lengthMs * .001));
        double position = (std::isfinite (controls.position) ? juce::jlimit (0.0f, 1.0f, controls.position) : .5f) + scanOffset;
        if (position < 0 || position > 1) position -= std::floor (position);
        position = juce::jlimit (0.0, 1.0, position + randomOffset);
        for (size_t l = 0; l < 2; ++l)
        {
            free->positions[l] = sources[l].from + position * std::max (0.0, sources[l].to - sources[l].from - 1);
            // A jump-started grain is placed where it would be after `age` frames.
            if (age > 0) free->positions[l] = advance (free->positions[l], age * frequency * sources[l].ratio, sources[l], controls.loop);
        }
        free->age = std::min (age, free->duration - 1);
        ++spawned;
    }

    std::array<Grain, kMaxGrainsPerVoice> grains {};
    std::array<double, kMaxGrainBatch * 4> delays {};
    int pending = 0;
    GranularScheduler scheduler;
    DeterministicRng rng;
    double rate = 48000, scanOffset = 0, syncIndex = 0;
    bool syncPrimed = false, jumpPending = false;
};
} // namespace bw
