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
            const float t = (float) (x - n), y0 = at (n - 1), y1 = at (n), y2 = at (n + 1), y3 = at (n + 2);
            cycles[(size_t) f][(size_t) i] = y1 + 0.5f * t * (y2 - y0 + t * (2 * y0 - 5 * y1 + 4 * y2 - y3 + t * (3 * (y1 - y2) + y3 - y0)));
        }
    }
    return cycles;
}

// Decodes any registered audio format to a mono mixdown, then slices it into cycles.
inline std::vector<std::vector<float>> loadCycles (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) { error = "Unsupported or unreadable audio file"; return {}; }
    int frameSize = clmFrameSize (file);
    if (frameSize > 65536) frameSize = 0;
    const auto limit = (juce::int64) kMaxUserFrames * (frameSize > 0 ? frameSize : Wavetable::kSize);
    const int length = (int) juce::jmin (reader->lengthInSamples, limit);
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, juce::jmax (1, length));
    buffer.clear();
    reader->read (&buffer, 0, length, 0, true, true);
    std::vector<float> mono ((size_t) length);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < length; ++i) mono[(size_t) i] += buffer.getSample (ch, i) / (float) buffer.getNumChannels();
    auto cycles = cyclesFromAudio (mono, frameSize);
    if (cycles.empty()) error = "Audio is too short for a wavetable frame";
    return cycles;
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
