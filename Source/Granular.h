#pragma once
#include "Sample.h"

namespace bw
{
constexpr int kMaxGrainsPerVoice = 32;
constexpr int kMaxGrainBatch = 8;

// Normalised 16-tap Blackman sinc; interpolation between phases avoids coefficient steps.
struct GrainKernelTable
{
    static constexpr int radius = 8, taps = 2 * radius, phases = 1024;
    std::array<std::array<float, taps>, phases + 1> weights {};
    GrainKernelTable ()
    {
        for (int p = 0; p <= phases; ++p)
        {
            std::array<double, taps> values {};
            double sum = 0;
            for (int k = 0; k < taps; ++k)
            {
                const double x = k - radius + 1 - (double) p / phases;
                const double a = juce::MathConstants<double>::pi * x;
                values[(size_t) k] = (x == 0 ? 1 : std::sin (a) / a)
                    * (.42 + .5 * std::cos (a / radius) + .08 * std::cos (2 * a / radius));
                sum += values[(size_t) k];
            }
            for (int k = 0; k < taps; ++k) weights[(size_t) p][(size_t) k] = (float) (values[(size_t) k] / sum);
        }
    }
};
inline const GrainKernelTable grainKernel;

inline std::pair<float, float> readGrainStereo (const Sample& sample, double position, double increment,
                                               double first, double last) noexcept
{
    if (sample.data.empty () || ! std::isfinite (position) || ! std::isfinite (increment)
        || ! std::isfinite (first) || ! std::isfinite (last) || increment <= 0) return {};
    if (increment >= 1) return sample.readFilteredStereo (position, increment, first, last);
    const int lo = juce::jlimit (0, sample.numFrames () - 1, (int) std::ceil (first));
    const int hi = juce::jlimit (lo, sample.numFrames () - 1, (int) std::ceil (last) - 1);
    position = juce::jlimit ((double) lo, (double) hi, position);
    const int base = (int) std::floor (position);
    const bool stereo = sample.isStereo ();
    if (position == base) return { sample.data[(size_t) base], stereo ? sample.right[(size_t) base] : sample.data[(size_t) base] };
    const double phase = (position - base) * GrainKernelTable::phases;
    const auto p = (size_t) phase;
    const float fraction = (float) (phase - p);
    double left = 0, right = 0;
    for (int k = 0; k < GrainKernelTable::taps; ++k)
    {
        const auto index = (size_t) juce::jlimit (lo, hi, base + k - GrainKernelTable::radius + 1);
        const float weight = grainKernel.weights[p][(size_t) k]
            + fraction * (grainKernel.weights[p + 1][(size_t) k] - grainKernel.weights[p][(size_t) k]);
        left += weight * sample.data[index];
        if (stereo) right += weight * sample.right[index];
    }
    std::pair<float, float> out { (float) left, (float) (stereo ? right : left) };
    // Meet the existing ascending-pitch filter continuously during bend/glide across unity.
    if (increment > .98)
    {
        const auto full = sample.readFilteredStereo (position, increment, first, last);
        const float mix = (float) ((increment - .98) / .02);
        out.first += mix * (full.first - out.first); out.second += mix * (full.second - out.second);
    }
    return out;
}
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
enum class GrainWindowType { hann, welch, gaussian, blackmanHarris, sinc, tukey, triangle, trapezoid, expDec, expDecRev };
enum class GrainWarpType { identity, bend, asym };
inline juce::StringArray grainWindowNames () { return { "Hann", "Welch", "Gaussian", "Blackman-Harris", "Sinc", "Tukey", "Triangle", "Trapezoid", "ExpDec", "ExpDecRev" }; }
inline juce::StringArray grainWarpNames () { return { "Identity", "Bend", "Asym" }; }
inline double grainEdgeFade (double t) noexcept
{
    const double edge = std::min (t, 1 - t);
    return edge >= .02 ? 1 : .5 * (1 - std::cos (juce::MathConstants<double>::pi * std::max (0.0, edge) / .02));
}
inline double grainWindowFormula (GrainWindowType type, double t) noexcept
{
    if (! std::isfinite (t) || t <= 0 || t >= 1) return 0;
    const double pi = juce::MathConstants<double>::pi, x = 2 * t - 1;
    switch (type)
    {
        case GrainWindowType::welch: return 1 - x * x;
        case GrainWindowType::gaussian: return std::exp (-.5 * std::pow ((t - .5) / .18, 2)) * grainEdgeFade (t);
        case GrainWindowType::blackmanHarris: return (.35875 - .48829 * std::cos (2*pi*t) + .14128 * std::cos (4*pi*t) - .01168 * std::cos (6*pi*t)) * grainEdgeFade (t);
        case GrainWindowType::sinc: return x == 0 ? 1 : std::pow (std::sin (4*pi*x) / (4*pi*x), 2);
        case GrainWindowType::tukey: return t < .25 ? .5 * (1 - std::cos (4*pi*t)) : t > .75 ? .5 * (1 - std::cos (4*pi*(1-t))) : 1;
        case GrainWindowType::triangle: return 1 - std::abs (x);
        case GrainWindowType::trapezoid: return std::min ({ 1.0, 4*t, 4*(1-t) });
        case GrainWindowType::expDec: return std::exp (-6*t) * grainEdgeFade (t);
        case GrainWindowType::expDecRev: return std::exp (-6*(1-t)) * grainEdgeFade (t);
        default: return .5 * (1 - std::cos (2*pi*t));
    }
}
struct GrainWindowTables
{
    std::array<std::array<float, 4097>, 10> values {};
    std::array<double, 10> energy {}, cross {};
    std::array<double, 10> leftEnergy {}, leftCross {};
    GrainWindowTables ()
    {
        for (size_t w = 0; w < values.size (); ++w)
            for (size_t i = 0; i < values[w].size (); ++i)
            {
                const double t = (double) i / 4096, v = grainWindowFormula ((GrainWindowType) w, t);
                values[w][i] = (float) v;
                energy[w] += v * v / 4096;
                cross[w] += v * grainWindowFormula (GrainWindowType::hann, t) / 4096;
                if (i <= 2048)
                {
                    const double weight = i == 2048 ? .5 : 1;
                    leftEnergy[w] += weight * v * v / 4096;
                    leftCross[w] += weight * v * grainWindowFormula (GrainWindowType::hann, t) / 4096;
                }
            }
    }
};
inline const GrainWindowTables grainWindowTables;
inline float evaluateGrainWindow (GrainWindowType type, double t) noexcept
{
    if (! std::isfinite (t) || t <= 0 || t >= 1) return 0;
    const auto& table = grainWindowTables.values[(size_t) juce::jlimit (0, 9, (int) type)];
    const double x = t * 4096; const auto i = (size_t) x;
    return table[i] + (float) (x - i) * (table[i+1] - table[i]);
}
inline double applyGrainSkew (double t, double skew) noexcept
{
    const double b = std::isfinite (skew) ? juce::jlimit (.01, .99, skew) : .5;
    t = std::isfinite (t) ? juce::jlimit (0.0, 1.0, t) : 0;
    return t <= b ? .5 * t / b : .5 + .5 * (t - b) / (1 - b);
}
inline double applyGrainWarp (double t, GrainWarpType type, double amount) noexcept
{
    t = std::isfinite (t) ? juce::jlimit (0.0, 1.0, t) : 0;
    const double a = std::isfinite (amount) ? juce::jlimit (-1.0, 1.0, amount) : 0;
    if (a == 0 || type == GrainWarpType::identity) return t;
    if (type == GrainWarpType::bend) return t + a * t * (1-t);
    return applyGrainSkew (t, .5 + .49*a);
}
inline float grainWindowGain (GrainWindowType type, float amount, float skew) noexcept
{
    const auto w = (size_t) juce::jlimit (0, 9, (int) type);
    const double a = std::isfinite (amount) ? juce::jlimit (0.0f, 1.0f, amount) : 0;
    const double b = std::isfinite (skew) ? juce::jlimit (.01f, .99f, skew) : .5;
    const double energy = 2*b*grainWindowTables.leftEnergy[w] + 2*(1-b)*(grainWindowTables.energy[w]-grainWindowTables.leftEnergy[w]);
    const double cross = 2*b*grainWindowTables.leftCross[w] + 2*(1-b)*(grainWindowTables.cross[w]-grainWindowTables.leftCross[w]);
    return (float) std::sqrt (.375 / std::max (1e-4, (1-a)*(1-a)*.375 + a*a*energy + 2*a*(1-a)*cross));
}
struct GrainWarp { GrainWarpType type = GrainWarpType::identity; float amount = 0; };
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
    bool loop = false;                            // grains wrap inside the region instead of fitting before its end
    float jitter = 0;                             // 0..1: each grain waits a random [0, jitter x interval) after its event
    float panSpread = 0;                          // 0..1: random equal-power pan per grain
    GrainWindowType window = GrainWindowType::hann;
    float windowAmount = 0, windowSkew = .5f; // amount 0 = legacy Hann, 1 = selected window; skew .5 = symmetric time mapping
    float directionSpread = 0, pitchSpread = 0, lengthSpread = 0, gainSpread = 0;
    float amountSpread = 0, skewSpread = 0, warpSpread = 0;
    std::array<GrainWarp, 2> warp {};
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
    float gainL = 1, gainR = 1;
    bool active = false, warped = false, neutralWindow = true;
    GrainWindowType window = GrainWindowType::hann;
    float windowAmount = 0, windowSkew = .5f, windowGain = 1;
    double pitchRatio = 1;
    bool reverse = false;
    std::array<GrainWarp, 2> warp {};
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
inline float grainWindow (const Grain& grain, double phase) noexcept
{
    // Keep the original interpolation and gain at neutral settings for existing presets.
    if (grain.windowSkew == .5f && (grain.windowAmount == 0 || grain.window == GrainWindowType::hann)) return grainWindow (phase);
    const double t = applyGrainSkew (phase, grain.windowSkew);
    const float hann = evaluateGrainWindow (GrainWindowType::hann, t);
    return (hann + grain.windowAmount * (evaluateGrainWindow (grain.window, t) - hann)) * grain.windowGain;
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
                double delay = grainBatchOffset (controls.distribution, k, timing.batch, rng) * interval;
                if (const float jitter = std::isfinite (controls.jitter) ? juce::jlimit (0.0f, 1.0f, controls.jitter) : 0.0f; jitter > 0)
                    delay += rng.next () * jitter * interval;
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
            const double phase = (double) grain.age / grain.duration;
            const float window = grain.neutralWindow ? grainWindow (phase) : grainWindow (grain, phase);
            windowSum += window;
            double u = phase, nextU = (double) (grain.age + 1) / grain.duration;
            if (grain.warped) for (const auto& warp : grain.warp)
            {
                u = applyGrainWarp (u, warp.type, warp.amount);
                nextU = applyGrainWarp (nextU, warp.type, warp.amount);
            }
            for (size_t l = 0; l < 2; ++l)
            {
                const auto& source = grain.sources[l];
                if (! source.sample || source.weight <= 0) continue;
                const double increment = frequency * source.ratio * grain.pitchRatio * (grain.reverse ? -1 : 1)
                                       * (grain.warped ? (nextU - u) * grain.duration : 1);
                const auto value = readGrainStereo (*source.sample, grain.positions[l], std::abs (increment), source.from, source.to);
                const double weight = window * source.weight;
                left += value.first * weight * grain.gainL; right += value.second * weight * grain.gainR;
                grain.positions[l] = advance (grain.positions[l], increment, source, controls.loop);
            }
            if (++grain.age >= grain.duration) grain.active = false;
        }
        // Power normalisation: overlapped grains read decorrelated material, so their level holds steady as the
        // overlap grows; perfectly correlated input (DC) rises by sqrt (overlap). Sparse clouds keep their Hann envelope.
        const double normalise = 1.0 / std::sqrt (std::max (1.0, windowSum));
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
            visual.amp.store (g.active ? grainWindow (g, phase) : 0, std::memory_order_relaxed);
        }
        display.count.store (activeCount (), std::memory_order_relaxed);
    }
    bool saturated = false;
    uint64_t spawned = 0;
private:
    static double advance (double position, double increment, const GrainSource& source, bool loop) noexcept
    {
        const double next = position + increment, last = source.to - 1;
        if (last <= source.from) return source.from;
        if (loop && (next >= last || next < source.from))
        {
            double offset = std::fmod (next - source.from, last - source.from);
            if (offset < 0) offset += last - source.from;
            return source.from + offset;
        }
        return juce::jlimit (source.from, last, next);
    }
    void spawn (const GrainSources& sources, const GrainControls& controls, const GrainTiming& timing, double frequency, int age = 0) noexcept
    {
        auto free = std::find_if (grains.begin (), grains.end (), [] (const Grain& g) { return ! g.active; });
        const double randomOffset = (rng.next () * 2 - 1) * (std::isfinite (controls.spread) ? juce::jlimit (0.0f, 1.0f, controls.spread) : 0.0f);
        if (free == grains.end ()) { saturated = true; return; } // Refuse, never steal an audible grain.
        if (! sources[0].sample && ! sources[1].sample) return;
        *free = {}; free->sources = sources; free->active = true;
        auto bounded = [] (float v, float lo, float hi, float fallback = 0) { return std::isfinite (v) ? juce::jlimit (lo, hi, v) : fallback; };
        auto spread = [&] (float width) { return width > 0 ? (rng.next () * 2 - 1) * width : 0; };
        const float reverseChance = bounded (controls.directionSpread, 0, 1);
        free->reverse = reverseChance > 0 && rng.next () < reverseChance;
        free->pitchRatio = std::exp2 (spread (bounded (controls.pitchSpread, 0, 24)) / 12);
        free->duration = std::max (2, (int) std::round (rate * juce::jlimit (5.0, 2000.0, timing.lengthMs * (1 + spread (bounded (controls.lengthSpread, 0, 1)))) * .001));
        free->window = (GrainWindowType) juce::jlimit (0, 9, (int) controls.window);
        free->windowAmount = (float) juce::jlimit (0.0, 1.0, bounded (controls.windowAmount, 0, 1) + spread (bounded (controls.amountSpread, 0, 1)));
        free->windowSkew = (float) juce::jlimit (.01, .99, bounded (controls.windowSkew, .01f, .99f, .5f) + spread (bounded (controls.skewSpread, 0, 1)));
        free->warp = controls.warp;
        for (auto& warp : free->warp) warp.amount = (float) juce::jlimit (-1.0, 1.0, bounded (warp.amount, -1, 1) + spread (bounded (controls.warpSpread, 0, 1)));
        free->windowGain = grainWindowGain (free->window, free->windowAmount, free->windowSkew);
        free->neutralWindow = free->windowSkew == .5f && (free->windowAmount == 0 || free->window == GrainWindowType::hann);
        for (const auto& warp : free->warp) free->warped |= warp.type != GrainWarpType::identity && warp.amount != 0;
        // Without grain loop a grain must end inside the region: shorter regions shorten it, later starts move back.
        const size_t lead = sources[0].sample ? 0u : 1u;
        const double increment = frequency * sources[lead].ratio * free->pitchRatio, span = sources[lead].to - sources[lead].from - 1;
        if (! controls.loop && std::isfinite (increment) && increment > 0 && span > 0)
            free->duration = std::max (2, (int) std::min ((double) free->duration, span / increment));
        if (const float pan = std::isfinite (controls.panSpread) ? juce::jlimit (0.0f, 1.0f, controls.panSpread) : 0.0f; pan > 0)
        {
            const double angle = ((rng.next () * 2 - 1) * pan + 1) * juce::MathConstants<double>::pi * .25;
            free->gainL = (float) (std::cos (angle) * juce::MathConstants<double>::sqrt2);
            free->gainR = (float) (std::sin (angle) * juce::MathConstants<double>::sqrt2);
        }
        const float gain = (float) (1 + spread (bounded (controls.gainSpread, 0, 1)));
        free->gainL *= gain; free->gainR *= gain;
        double position = (std::isfinite (controls.position) ? juce::jlimit (0.0f, 1.0f, controls.position) : .5f) + scanOffset;
        if (position < 0 || position > 1) position -= std::floor (position);
        position = juce::jlimit (0.0, 1.0, position + randomOffset);
        for (size_t l = 0; l < 2; ++l)
        {
            const double length = std::max (0.0, sources[l].to - sources[l].from - 1);
            const double travel = controls.loop ? 0.0 : std::min (length, free->duration * frequency * sources[l].ratio * free->pitchRatio);
            free->positions[l] = sources[l].from + (free->reverse ? std::max (position * length, travel) : std::min (position * length, std::isfinite (travel) ? length - travel : length));
            // A jump-started grain is placed where it would be after `age` frames.
            if (age > 0)
            {
                double u = (double) std::min (age, free->duration - 1) / free->duration;
                for (const auto& warp : free->warp) u = applyGrainWarp (u, warp.type, warp.amount);
                free->positions[l] = advance (free->positions[l], u * free->duration * frequency * sources[l].ratio * free->pitchRatio * (free->reverse ? -1 : 1), sources[l], controls.loop);
            }
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
