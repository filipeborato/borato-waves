#pragma once
#include "Wavetable.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

namespace bw
{
constexpr int kMaxUserFrames = 256;

// Frame size declared by a Serum-style "clm " chunk ("<!>2048 ..."); 0 when absent.
inline int clmFrameSize (const juce::File& file)
{
    juce::FileInputStream in (file);
    if (! in.openedOk() || in.readInt() != (int) juce::ByteOrder::littleEndianInt ("RIFF")) return 0;
    in.skipNextBytes (8); // RIFF size, "WAVE"
    while (in.getNumBytesRemaining() >= 8)
    {
        const int id = in.readInt();
        const auto size = (juce::int64) (juce::uint32) in.readInt();
        if (id == (int) juce::ByteOrder::littleEndianInt ("clm "))
        {
            juce::MemoryBlock text;
            in.readIntoMemoryBlock (text, (juce::pointer_sized_int) juce::jmin<juce::int64> (size, 64));
            const auto s = text.toString();
            return s.startsWith ("<!>") ? s.substring (3).getIntValue() : 0;
        }
        in.skipNextBytes (size + (size & 1));
    }
    return 0;
}

// Catmull-Rom between y1 (t = 0) and y2 (t = 1).
inline float catmullRom (float y0, float y1, float y2, float y3, float t)
{
    return y1 + 0.5f * t * (y2 - y0 + t * (2 * y0 - 5 * y1 + 4 * y2 - y3 + t * (3 * (y1 - y2) + y3 - y0)));
}

// Splits mono audio into frames of frameSize samples and resamples each cycle to kSize.
// frameSize <= 0 guesses: kSize-sample frames for long or kSize-multiple audio, else one cycle.
inline std::vector<std::vector<float>> cyclesFromAudio (const std::vector<float>& audio, int frameSize)
{
    const auto length = (int) audio.size();
    if (frameSize <= 0)
        frameSize = length % Wavetable::kSize == 0 || length > 4 * Wavetable::kSize ? Wavetable::kSize : length;
    const int frames = frameSize >= 16 ? juce::jmin (kMaxUserFrames, length / frameSize) : 0;
    std::vector<std::vector<float>> cycles ((size_t) frames, std::vector<float> (Wavetable::kSize));
    for (int f = 0; f < frames; ++f)
    {
        const float* src = audio.data() + (size_t) f * (size_t) frameSize;
        auto at = [&] (int i) { return src[(i % frameSize + frameSize) % frameSize]; };
        // ponytail: periodic Catmull-Rom resampling; FFT resynthesis if odd frame sizes alias audibly.
        for (int i = 0; i < Wavetable::kSize; ++i)
        {
            const double x = (double) i * frameSize / Wavetable::kSize;
            const int n = (int) x;
            cycles[(size_t) f][(size_t) i] = catmullRom (at (n - 1), at (n), at (n + 1), at (n + 2), (float) (x - n));
        }
    }
    return cycles;
}

inline std::unique_ptr<juce::AudioFormatReader> openAudio (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file));
}
// Mono mixdown of the first `limit` samples.
inline std::vector<float> readMono (juce::AudioFormatReader& reader, juce::int64 limit)
{
    const int length = (int) juce::jmax<juce::int64> (0, juce::jmin (reader.lengthInSamples, limit));
    juce::AudioBuffer<float> buffer ((int) reader.numChannels, juce::jmax (1, length));
    buffer.clear();
    reader.read (&buffer, 0, length, 0, true, true);
    std::vector<float> mono ((size_t) length);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < length; ++i) mono[(size_t) i] += buffer.getSample (ch, i) / (float) buffer.getNumChannels();
    return mono;
}

// Decodes any registered audio format to a mono mixdown, then slices it into cycles.
inline std::vector<std::vector<float>> loadCycles (const juce::File& file, juce::String& error)
{
    const auto reader = openAudio (file);
    if (reader == nullptr) { error = "Unsupported or unreadable audio file"; return {}; }
    int frameSize = clmFrameSize (file);
    if (frameSize > 65536) frameSize = 0;
    const auto limit = (juce::int64) kMaxUserFrames * (frameSize > 0 ? frameSize : Wavetable::kSize);
    auto cycles = cyclesFromAudio (readMono (*reader, limit), frameSize);
    if (cycles.empty()) error = "Audio is too short for a wavetable frame";
    return cycles;
}

// Tuned import: one table cycle per detected period of the sound, so the table plays at the pressed note.
constexpr double kMinPitchHz = 25, kMaxPitchHz = 2000;
constexpr int kMaxImportSeconds = 20;

struct PitchEstimate
{
    double period = 0;     // samples, fractional
    float confidence = 0;  // 1 - the normalised difference at the period
    bool ambiguous = true; // no clear period, or the octave below fits much better
};

inline int maxPitchLag (double rate) { return (int) std::ceil (rate / kMinPitchHz); }
// One analysis window (two of the longest periods) plus the longest lag.
inline size_t samplesForPitch (double rate) { return (size_t) (3 * maxPitchLag (rate) + 2); }

// YIN: the cumulative-mean-normalised difference over the loudest window, first dip under a threshold.
// The period is then refined at the largest multiple of it that fits the lag range.
inline PitchEstimate detectPeriod (const std::vector<float>& mono, double rate)
{
    const int maxLag = maxPitchLag (rate), minLag = juce::jmax (2, (int) (rate / kMaxPitchHz)), window = 2 * maxLag;
    if (mono.size() < samplesForPitch (rate)) return {};
    size_t start = 0;
    double loudest = -1;
    for (size_t s = 0; s + (size_t) (window + maxLag + 2) <= mono.size(); s += (size_t) window / 2)
    {
        double energy = 0;
        for (int j = 0; j < window; ++j) energy += (double) mono[s + (size_t) j] * mono[s + (size_t) j];
        if (energy > loudest) { loudest = energy; start = s; }
    }
    const float* x = mono.data() + start;
    std::vector<double> d ((size_t) maxLag + 2), cmnd ((size_t) maxLag + 2, 1.0);
    double running = 0;
    for (int lag = 1; lag <= maxLag + 1; ++lag)
    {
        double sum = 0;
        for (int j = 0; j < window; ++j) { const double e = (double) x[j] - x[j + lag]; sum += e * e; }
        d[(size_t) lag] = sum;
        running += sum;
        cmnd[(size_t) lag] = running > 0 ? sum * lag / running : 1.0;
    }
    PitchEstimate result;
    int best = -1;
    for (int lag = minLag; lag <= maxLag && best < 0; ++lag)
        if (cmnd[(size_t) lag] < 0.15)
        {
            while (lag < maxLag && cmnd[(size_t) lag + 1] < cmnd[(size_t) lag]) ++lag;
            best = lag;
        }
    result.ambiguous = best < 0;
    if (best < 0)
        best = (int) (std::min_element (cmnd.begin() + minLag, cmnd.begin() + maxLag + 1) - cmnd.begin());
    result.confidence = (float) juce::jlimit (0.0, 1.0, 1 - cmnd[(size_t) best]);
    // A weak fundamental: the dip at twice the period is much deeper than the one found.
    if (const int twice = 2 * best; twice + 2 <= maxLag && cmnd[(size_t) best] > 0.02)
        if (*std::min_element (cmnd.begin() + twice - 2, cmnd.begin() + twice + 3) < 0.5 * cmnd[(size_t) best]) result.ambiguous = true;
    // Parabolic minimum of d near a multiple of the period: the largest within the lag range whose
    // search span (+/- one sample per multiple) stays inside half a period, clear of its neighbours.
    const int multiple = juce::jmax (1, juce::jmin (maxLag / best, best / 2 - 1));
    int at = multiple * best;
    for (int lag = juce::jmax (2, at - multiple); lag <= juce::jmin (maxLag, at + multiple); ++lag)
        if (d[(size_t) lag] < d[(size_t) at]) at = lag;
    const double a = d[(size_t) at - 1], b = d[(size_t) at], c = d[(size_t) at + 1], curve = a - 2 * b + c;
    result.period = (at + (curve > 0 ? 0.5 * (a - c) / curve : 0.0)) / multiple;
    return result;
}

// One kSize cycle per whole period, at most kMaxUserFrames spread evenly over the sound.
inline std::vector<std::vector<float>> cyclesFromPeriod (const std::vector<float>& mono, double period)
{
    if (period < 2 || mono.size() < 4) return {};
    const int available = (int) juce::jmin (1.0e6, ((double) mono.size() - 3) / period);
    if (available < 1) return {};
    const int frames = juce::jmin (kMaxUserFrames, available);
    std::vector<std::vector<float>> cycles ((size_t) frames, std::vector<float> (Wavetable::kSize));
    const auto at = [&mono] (int i) { return mono[(size_t) juce::jmax (0, i)]; };
    for (int f = 0; f < frames; ++f)
    {
        const double start = period * (frames > 1 ? std::round ((double) f * (available - 1) / (frames - 1)) : 0.0);
        for (int i = 0; i < Wavetable::kSize; ++i)
        {
            const double x = start + period * i / Wavetable::kSize;
            const int n = (int) x;
            cycles[(size_t) f][(size_t) i] = catmullRom (at (n - 1), at (n), at (n + 1), at (n + 2), (float) (x - n));
        }
    }
    return cycles;
}

struct TunedSource
{
    std::vector<float> mono; // at most kMaxImportSeconds
    double sampleRate = 0;
    PitchEstimate pitch;
};

// Reads and analyses a file for a tuned import; an empty source and a reason when it cannot be used.
inline TunedSource loadTunedSource (const juce::File& file, juce::String& error)
{
    error.clear();
    const auto reader = openAudio (file);
    if (reader == nullptr || reader->sampleRate <= 0) { error = "Unsupported or unreadable audio file"; return {}; }
    TunedSource source { readMono (*reader, (juce::int64) (kMaxImportSeconds * reader->sampleRate)), reader->sampleRate, {} };
    if (source.mono.size() < samplesForPitch (source.sampleRate)) error = "Audio is too short to detect a pitch";
    else if (std::all_of (source.mono.begin(), source.mono.end(), [] (float s) { return std::abs (s) < 1.0e-4f; })) error = "Audio is silent";
    if (error.isNotEmpty()) return {};
    source.pitch = detectPeriod (source.mono, source.sampleRate);
    return source;
}

// Non-audio threads publish immutable data (callers serialise publish/get). The audio thread
// acquires once per block. A replaced item stays alive until the audio thread reports that every
// voice has settled on its successor, or the successor has been rendered long enough for every
// voice's crossfade away from it (two 15 ms fades at most) to finish.
template <typename T>
class PublishSlot
{
public:
    static constexpr juce::int64 kSafeSamples = 1 << 16;

    void publish (std::shared_ptr<const T> table)
    {
        collect();
        if (owned != nullptr) retired.push_back (std::move (owned));
        owned = std::move (table);
        latest.store (owned.get(), std::memory_order_release);
    }
    const std::shared_ptr<const T>& get() const noexcept { return owned; }
    bool idle() const noexcept { return retired.empty(); }
    void collect()
    {
        if (seen.load (std::memory_order_acquire) == owned.get()
            && (settled.load (std::memory_order_acquire) || rendered.load (std::memory_order_relaxed) >= kSafeSamples))
            retired.clear();
    }

    // Audio thread: returns the item for this block. Counts only samples already rendered.
    const T* acquire (int blockSamples) noexcept
    {
        const auto* table = latest.load (std::memory_order_acquire);
        if (table != inUse)
        {
            inUse = table;
            rendered.store (0, std::memory_order_relaxed);
            settled.store (false, std::memory_order_relaxed);
            seen.store (table, std::memory_order_release);
        }
        else rendered.store (rendered.load (std::memory_order_relaxed) + previousBlock, std::memory_order_relaxed);
        previousBlock = blockSamples;
        return table;
    }
    // Audio thread, after rendering: no voice reads anything older than the acquired item.
    void settle() noexcept { settled.store (true, std::memory_order_release); }

private:
    std::shared_ptr<const T> owned;
    std::vector<std::shared_ptr<const T>> retired;
    std::atomic<const T*> latest { nullptr }, seen { nullptr };
    std::atomic<juce::int64> rendered { 0 };
    std::atomic<bool> settled { false };
    const T* inUse = nullptr;
    juce::int64 previousBlock = 0;
};
using UserTableSlot = PublishSlot<Wavetable>;
} // namespace bw
