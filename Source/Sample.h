#pragma once
#include "UserWavetable.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>

namespace bw
{
constexpr int kMaxSampleFrames = 1 << 22;                 // 16 MiB per channel of floats, about 95 s at 44.1 kHz
constexpr int kMinSampleRegion = 64;                      // frames between start and end
constexpr int kSampleRootNote = 60;                       // default MIDI root note (C4)
constexpr int kMaxSampleChannels = 2;                     // mono or stereo source files
constexpr int kSampleContainerVersion = 1;                // portable preset sample container format version
constexpr size_t kMaxAssetMemoryBytes = (size_t) 256 << 20; // 256 MiB aggregate budget (active + retired + pending + undo)

inline uint64_t fnv1a64 (const void* bytes, size_t count, uint64_t hash = 14695981039346656037ull) noexcept
{
    const auto* ptr = static_cast<const uint8_t*> (bytes);
    for (size_t i = 0; i < count; ++i)
        hash = (hash ^ ptr[i]) * 1099511628211ull;
    return hash;
}

inline uint64_t computeSampleHash (int channels, int frames, double rate, const float* left, const float* right) noexcept
{
    uint64_t h = fnv1a64 (&channels, sizeof (channels));
    h = fnv1a64 (&frames, sizeof (frames), h);
    h = fnv1a64 (&rate, sizeof (rate), h);
    if (left != nullptr && frames > 0)
        h = fnv1a64 (left, (size_t) frames * sizeof (float), h);
    if (channels == 2 && right != nullptr && frames > 0)
        h = fnv1a64 (right, (size_t) frames * sizeof (float), h);
    return h;
}

// 64-tap Blackman sinc, 1024 fractional phases. Constructed before audio starts.
struct SincKernelTable
{
    static constexpr int radius = 32, phases = 1024;
    std::array<float, radius * phases + 1> values {};
    SincKernelTable ()
    {
        for (size_t i = 0; i < values.size (); ++i)
        {
            const double x = (double) i / phases, a = juce::MathConstants<double>::pi * x;
            values[i] = (float) ((i == 0 ? 1.0 : std::sin (a) / a)
                * (0.42 + 0.5 * std::cos (a / radius) + 0.08 * std::cos (2 * a / radius)));
        }
        values.back () = 0;
    }
    float at (double x) const noexcept
    {
        const double phase = std::abs (x) * phases;
        if (phase >= radius * phases) return 0;
        const auto i = (size_t) phase;
        return values[i] + (float) (phase - (double) i) * (values[i + 1] - values[i]);
    }
};
inline const SincKernelTable sampleSincKernel;

// Imported audio for the sample source: mono (`right` empty) or stereo (`right.size() == data.size()`)
// at the file's own rate, immutable once published.
struct Sample
{
    std::vector<float> data;  // Left channel (or mono signal)
    std::vector<float> right; // Right channel when stereo; empty when mono
    double rate = 44100;
    uint64_t contentHash = 0;

    int numFrames() const noexcept { return (int) data.size(); }
    int numChannels() const noexcept { return ! right.empty() && right.size() == data.size() ? 2 : 1; }
    bool isStereo() const noexcept { return numChannels() == 2; }
    size_t bytes() const noexcept { return (data.size() + right.size()) * sizeof (float); }

    void updateHash() noexcept
    {
        contentHash = computeSampleHash (numChannels(), numFrames(), rate, data.data(), numChannels() == 2 ? right.data() : nullptr);
    }

    // Cubic Hermite read; frames outside the data read as the nearest end.
    float read (double position, int channel = 0) const noexcept
    {
        const auto& ch = (channel == 1 && ! right.empty()) ? right : data;
        const int last = (int) ch.size() - 1;
        const int n = (int) std::floor (position);
        const float t = (float) (position - n);
        auto at = [&] (int i) { return ch[(size_t) juce::jlimit (0, last, i)]; };
        return catmullRom (at (n - 1), at (n), at (n + 1), at (n + 2), t);
    }

    std::pair<float, float> readStereo (double position) const noexcept
    {
        const float l = read (position, 0);
        return { l, ! right.empty() ? read (position, 1) : l };
    }

    std::pair<float, float> readFilteredStereo (double position, double increment,
                                                  double first = 0, double last = -1) const noexcept
    {
        if (data.empty () || ! std::isfinite (position) || ! std::isfinite (increment) || ! std::isfinite (first) || ! std::isfinite (last) || increment <= 0)
            return {};
        // Bound worst-case callback work to 4096 taps; extreme ratios fade to silence.
        const double guard = juce::jlimit (0.0, 1.0, (64.0 - increment) / 16.0);
        if (guard <= 0) return {};
        const double step = std::max (1.0, increment), support = SincKernelTable::radius * step;
        const int lo = juce::jlimit (0, numFrames () - 1, (int) std::ceil (first));
        const int hi = last < 0 ? numFrames () - 1 : juce::jlimit (lo, numFrames () - 1, (int) std::ceil (last) - 1);
        position = juce::jlimit ((double) lo, (double) hi, position);
        const bool stereo = isStereo ();
        // At unity cutoff and integer phase, sinc is exactly the identity; this is common for dense grain clouds.
        if (increment <= 1.0 && position == std::floor (position))
        {
            const auto index = (size_t) position;
            return { data[index], stereo ? right[index] : data[index] };
        }
        double left = 0, r = 0, sum = 0;
        const int begin = (int) std::ceil (position - support), finish = (int) std::floor (position + support);
        for (int n = begin; n <= finish; ++n)
        {
            const double weight = sampleSincKernel.at ((position - n) / step);
            const auto index = (size_t) juce::jlimit (lo, hi, n);
            left += weight * data[index];
            if (stereo) r += weight * right[index];
            sum += weight;
        }
        const double gain = guard / sum;
        return { (float) (left * gain), (float) ((stereo ? r : left) * gain) };
    }

    // Normalised start/end (either order) to frames, at least kMinSampleRegion apart.
    std::pair<double, double> region (float start, float end) const noexcept;
};

inline std::pair<double, double> Sample::region (float start, float end) const noexcept
{
    const double length = (double) data.size();
    const double lo = juce::jlimit (0.0, length - kMinSampleRegion, std::min (start, end) * length);
    return { lo, juce::jlimit (lo + kMinSampleRegion, length, std::max (start, end) * length) };
}

// Validates and decodes an AudioFormatReader on a non-audio thread in cancellable chunks.
// Refuses incomplete reads, NaN/Inf, invalid sample rates, channel counts above kMaxSampleChannels,
// lengths outside [kMinSampleRegion, kMaxSampleFrames], or payloads exceeding maxAllowedBytes.
inline std::shared_ptr<const Sample> decodeSample (
    juce::AudioFormatReader& reader,
    juce::String& error,
    juce::int64 rawFileBytes = -1,
    const std::function<bool()>& shouldCancel = {},
    const std::function<void (float)>& onProgress = {},
    size_t maxAllowedBytes = kMaxAssetMemoryBytes)
{
    if (! std::isfinite (reader.sampleRate) || reader.sampleRate <= 0)
    {
        error = "Invalid sample rate";
        return {};
    }
    if (reader.numChannels == 0 || (int) reader.numChannels > kMaxSampleChannels)
    {
        error = "Unsupported channel count (max " + juce::String (kMaxSampleChannels) + " channels)";
        return {};
    }
    if (reader.lengthInSamples > kMaxSampleFrames)
    {
        error = "Sample too long (max " + juce::String (kMaxSampleFrames) + " frames)";
        return {};
    }
    if (reader.lengthInSamples < kMinSampleRegion)
    {
        error = "Sample too short";
        return {};
    }
    const int length = (int) reader.lengthInSamples;
    const int channels = (int) reader.numChannels;
    const size_t decodedBytes = (size_t) length * (size_t) channels * sizeof (float);
    if (decodedBytes > maxAllowedBytes)
    {
        error = "Sample exceeds 256 MB memory budget";
        return {};
    }
    if (rawFileBytes >= 0)
    {
        const auto format = reader.getFormatName().toUpperCase();
        if (format.contains ("WAV") || format.contains ("AIFF") || format.contains ("AIF"))
        {
            const juce::int64 bytesPerSample = std::max<juce::int64> (1, reader.bitsPerSample / 8);
            const juce::int64 expectedPcmBytes = reader.lengthInSamples * (juce::int64) reader.numChannels * bytesPerSample;
            if (rawFileBytes < expectedPcmBytes)
            {
                error = "Incomplete or truncated audio file";
                return {};
            }
        }
    }
    juce::AudioBuffer<float> buffer (channels, length);
    buffer.clear();
    const int chunkSize = juce::jmax (64, juce::jmin (32768, length / 4));
    for (int start = 0; start < length; start += chunkSize)
    {
        if (shouldCancel && shouldCancel())
        {
            error = "Sample load cancelled";
            return {};
        }
        const int count = juce::jmin (chunkSize, length - start);
        if (! reader.read (&buffer, start, count, start, true, true))
        {
            error = "Incomplete or corrupted audio read";
            return {};
        }
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* src = buffer.getReadPointer (ch) + start;
            for (int i = 0; i < count; ++i)
                if (! std::isfinite (src[i]))
                {
                    error = "Sample contains non-finite (NaN/Inf) audio data";
                    return {};
                }
        }
        if (onProgress) onProgress ((float) (start + count) / (float) length);
    }
    if (shouldCancel && shouldCancel())
    {
        error = "Sample load cancelled";
        return {};
    }
    auto sample = std::make_shared<Sample>();
    sample->rate = reader.sampleRate;
    sample->data.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + length);
    if (channels == 2)
        sample->right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + length);
    sample->updateHash();
    return sample;
}

// Decodes on the calling (non-audio) thread. Longer or invalid files are refused rather than truncated.
inline std::shared_ptr<const Sample> loadSample (
    const juce::File& file,
    juce::String& error,
    const std::function<bool()>& shouldCancel = {},
    const std::function<void (float)>& onProgress = {},
    size_t maxAllowedBytes = kMaxAssetMemoryBytes)
{
    if (! file.existsAsFile() || file.getSize() <= 0)
    {
        error = "Empty or unreadable audio file";
        return {};
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) { error = "Unsupported or unreadable audio file"; return {}; }
    return decodeSample (*reader, error, file.getSize(), shouldCancel, onProgress, maxAllowedBytes);
}

// Serialises the sample source into a versioned ValueTree container with embedded PCM and content hash.
inline juce::ValueTree serializeSampleContainer (const Sample* sample, const juce::File& path)
{
    juce::ValueTree node ("SAMPLE");
    if (path != juce::File())
    {
        node.setProperty ("path", path.getFullPathName(), nullptr);
        node.setProperty ("name", path.getFileNameWithoutExtension(), nullptr);
    }
    if (sample != nullptr && sample->numFrames() >= kMinSampleRegion)
    {
        const int channels = sample->numChannels();
        const int frames = sample->numFrames();
        const size_t channelBytes = (size_t) frames * sizeof (float);
        const size_t totalBytes = (size_t) channels * channelBytes;
        juce::MemoryBlock raw (totalBytes);
        raw.copyFrom (sample->data.data(), 0, channelBytes);
        if (channels == 2)
            raw.copyFrom (sample->right.data(), (int) channelBytes, channelBytes);
        const uint64_t hash = sample->contentHash != 0
            ? sample->contentHash
            : computeSampleHash (channels, frames, sample->rate, sample->data.data(), channels == 2 ? sample->right.data() : nullptr);
        node.setProperty ("containerVersion", kSampleContainerVersion, nullptr);
        node.setProperty ("channels", channels, nullptr);
        node.setProperty ("frames", frames, nullptr);
        node.setProperty ("rate", sample->rate, nullptr);
        node.setProperty ("pcmBytes", (juce::int64) totalBytes, nullptr);
        node.setProperty ("hash", juce::String::toHexString ((juce::int64) hash), nullptr);
        node.setProperty ("data", raw.toBase64Encoding(), nullptr);
    }
    return node;
}

// Validates and unpacks a SAMPLE ValueTree node. Bounds and byte budgets are checked before allocating buffers.
// Returns false on malformed/corrupted container payloads so the caller can preserve the active state.
inline bool deserializeSampleContainer (
    const juce::ValueTree& node,
    std::shared_ptr<const Sample>& outSample,
    juce::File& outPath,
    bool& hasEmbeddedPayload,
    juce::String& error,
    size_t maxAllowedBytes = kMaxAssetMemoryBytes)
{
    outSample.reset();
    outPath = juce::File();
    hasEmbeddedPayload = false;
    if (! node.isValid()) return true;

    const auto pathStr = node.getProperty ("path").toString();
    if (juce::File::isAbsolutePath (pathStr))
        outPath = juce::File (pathStr);

    hasEmbeddedPayload = node.hasProperty ("containerVersion") || node.hasProperty ("data")
                      || node.hasProperty ("frames") || node.hasProperty ("pcmBytes") || node.hasProperty ("hash");
    if (! hasEmbeddedPayload)
        return true;

    const int version = (int) node.getProperty ("containerVersion", kSampleContainerVersion);
    if (version != kSampleContainerVersion)
    {
        error = "Unsupported sample container version";
        return false;
    }
    const int channels = (int) node.getProperty ("channels", 0);
    const int frames = (int) node.getProperty ("frames", 0);
    const double rate = (double) node.getProperty ("rate", 0.0);
    const juce::int64 declaredBytes = (juce::int64) node.getProperty ("pcmBytes", -1);
    const auto hashStr = node.getProperty ("hash").toString().trim();
    const auto dataB64 = node.getProperty ("data").toString();

    if (channels < 1 || channels > kMaxSampleChannels || frames < kMinSampleRegion || frames > kMaxSampleFrames
        || ! std::isfinite (rate) || rate <= 0.0 || hashStr.isEmpty() || dataB64.isEmpty())
    {
        error = "Invalid sample container header";
        return false;
    }
    const size_t expectedBytes = (size_t) channels * (size_t) frames * sizeof (float);
    if (declaredBytes != (juce::int64) expectedBytes)
    {
        error = "Mismatched declared sample size";
        return false;
    }
    if (expectedBytes > maxAllowedBytes)
    {
        error = "Embedded sample exceeds 256 MB memory budget";
        return false;
    }
    // Check encoded length before decoding to avoid unbounded allocation on malicious Base64 strings.
    if ((size_t) dataB64.length() > expectedBytes * 2 + 1024)
    {
        error = "Embedded sample payload too large";
        return false;
    }
    juce::MemoryBlock raw;
    if (! raw.fromBase64Encoding (dataB64) || raw.getSize() != expectedBytes)
    {
        error = "Truncated or corrupted embedded sample data";
        return false;
    }
    const auto* floats = static_cast<const float*> (raw.getData());
    const size_t totalFloats = (size_t) channels * (size_t) frames;
    for (size_t i = 0; i < totalFloats; ++i)
        if (! std::isfinite (floats[i]))
        {
            error = "Embedded sample contains non-finite (NaN/Inf) data";
            return false;
        }
    const float* leftPtr = floats;
    const float* rightPtr = channels == 2 ? floats + frames : nullptr;
    const uint64_t actualHash = computeSampleHash (channels, frames, rate, leftPtr, rightPtr);
    const uint64_t expectedHash = (uint64_t) hashStr.getHexValue64();
    if (actualHash != expectedHash || hashStr != juce::String::toHexString ((juce::int64) expectedHash))
    {
        error = "Invalid embedded sample content hash";
        return false;
    }

    auto sample = std::make_shared<Sample>();
    sample->rate = rate;
    sample->data.assign (leftPtr, leftPtr + frames);
    if (channels == 2)
        sample->right.assign (rightPtr, rightPtr + frames);
    sample->contentHash = actualHash;
    outSample = std::move (sample);
    return true;
}

constexpr int kMaxSampleZones = 128;
enum class VelocityCrossfadeLaw { linear, equalPower };
struct SampleVariant
{
    std::shared_ptr<const Sample> sample;
    juce::File path;
    bool missing = false;
};
struct VelocityLayers
{
    std::array<int, 2> zones { -1, -1 };
    std::array<float, 2> weights {};
};
struct SampleZone
{
    std::shared_ptr<const Sample> sample;
    juce::File path;
    int lowKey = 0, highKey = 127, rootKey = 60;
    float start = 0, end = 1;
    bool loop = false;
    int lowVel = 1, highVel = 127, crossfadeVel = 0;
    VelocityCrossfadeLaw crossfadeLaw = VelocityCrossfadeLaw::linear;
    // Slot 0 falls back to the original sample/path for compatibility with single-sample callers.
    std::array<SampleVariant, 4> variants {};
    int numVariants = 1;
    // Loop window (fractions of the region when linked, else of the file), loop mode and direction.
    int loopMode = 0; // LoopMode
    float loopStart = 0, loopEnd = 1;
    bool loopLinked = true, reverse = false;
    // Per-zone fine tune (cents), equal-power pan (-1..1) and gain (dB), as imported from SFZ tune/pan/volume.
    float tuneCents = 0, pan = 0, gainDb = 0;
    const Sample* sampleForVariant (int index) const noexcept
    {
        if (index < 0 || index >= numVariants || variants[(size_t) index].missing) return nullptr;
        const auto* asset = variants[(size_t) index].sample.get ();
        return asset ? asset : index == 0 ? sample.get () : nullptr;
    }
    juce::File pathForVariant (int index) const
    { return index == 0 && variants[0].path == juce::File () ? path : variants[(size_t) index].path; }
};
// Manual slices of one sample: marker frames start each slice (the first is always 0), slice i plays on note
// root + i, from its marker to the next marker (single) or to the end of the file (play to end).
constexpr int kMaxSlices = kMaxSampleZones;
struct SliceMap
{
    std::vector<int> markers;
    int root = 60;
    bool playToEnd = false;
};
// Sorted, unique slice starts: 0 first, each at least kMinSampleRegion frames after the previous one and
// before the file end; markers too close to a kept one, or past the cap, are dropped.
inline std::vector<int> sanitiseSliceMarkers (std::vector<int> frames, int numFrames)
{
    frames.push_back (0);
    std::sort (frames.begin (), frames.end ());
    std::vector<int> kept;
    for (int f : frames)
        if (f >= 0 && f <= numFrames - kMinSampleRegion && (kept.empty () || f >= kept.back () + kMinSampleRegion) && (int) kept.size () < kMaxSlices)
            kept.push_back (f);
    return kept;
}

struct SampleBank
{
    std::vector<SampleZone> zones;
    // Set when the zones were generated from slices of zones[0].sample; kept for editing the markers again.
    bool sliced = false;
    SliceMap slices;
    int findZone (int note) const noexcept
    {
        for (size_t i = 0; i < zones.size (); ++i)
            if (note >= zones[i].lowKey && note <= zones[i].highKey) return (int) i;
        return -1;
    }
    bool hasKeyOverlap () const noexcept
    {
        for (size_t i = 0; i < zones.size (); ++i)
            for (size_t j = i + 1; j < zones.size (); ++j)
                if (zones[i].lowKey <= zones[j].highKey && zones[j].lowKey <= zones[i].highKey)
                {
                    const auto& a = zones[i]; const auto& b = zones[j];
                    if (a.lowKey != b.lowKey || a.highKey != b.highKey || (a.lowVel <= b.highVel && b.lowVel <= a.highVel)) return true;
                }
        return false;
    }
    bool valid () const noexcept
    {
        if (zones.empty () || zones.size () > kMaxSampleZones || hasKeyOverlap ()) return false;
        for (const auto& z : zones)
        {
            if (z.lowKey < 0 || z.highKey > 127 || z.lowKey > z.highKey || z.rootKey < 0 || z.rootKey > 127
                || ! std::isfinite (z.start) || ! std::isfinite (z.end) || z.start < 0 || z.end > 1 || z.start >= z.end
                || z.lowVel < 1 || z.highVel > 127 || z.lowVel > z.highVel || z.crossfadeVel < 0 || z.crossfadeVel > 63
                || (z.crossfadeLaw != VelocityCrossfadeLaw::linear && z.crossfadeLaw != VelocityCrossfadeLaw::equalPower)
                || z.numVariants < 1 || z.numVariants > 4 || z.loopMode < 0 || z.loopMode > 3
                || ! std::isfinite (z.loopStart) || ! std::isfinite (z.loopEnd) || z.loopStart < 0 || z.loopEnd > 1 || z.loopStart > z.loopEnd
                || ! std::isfinite (z.tuneCents) || std::abs (z.tuneCents) > 2400 || ! std::isfinite (z.pan) || std::abs (z.pan) > 1
                || ! std::isfinite (z.gainDb) || z.gainDb < -144 || z.gainDb > 24) return false;
            for (int v = 0; v < z.numVariants; ++v)
                if (const auto* asset = z.sampleForVariant (v))
                    if (asset->numFrames () < kMinSampleRegion || asset->numFrames () > kMaxSampleFrames
                        || ! std::isfinite (asset->rate) || asset->rate <= 0
                        || (! asset->right.empty () && asset->right.size () != asset->data.size ())) return false;
            for (int velocity = 1; velocity <= 127; ++velocity)
            {
                int count = 0;
                for (const auto& other : zones)
                    if (other.lowKey == z.lowKey && other.highKey == z.highKey
                        && velocity >= other.lowVel - other.crossfadeVel && velocity <= other.highVel + other.crossfadeVel)
                    {
                        if (++count > 2) return false;
                        if (velocity >= z.lowVel - z.crossfadeVel && velocity <= z.highVel + z.crossfadeVel
                            && other.crossfadeLaw != z.crossfadeLaw) return false;
                    }
            }
        }
        return true;
    }
    VelocityLayers selectVelocityLayers (int note, int velocity) const noexcept
    {
        VelocityLayers selected;
        if (velocity < 1 || velocity > 127) return selected;
        int count = 0;
        for (size_t i = 0; i < zones.size () && count < 2; ++i)
        {
            const auto& z = zones[i];
            if (note >= z.lowKey && note <= z.highKey && velocity >= z.lowVel - z.crossfadeVel && velocity <= z.highVel + z.crossfadeVel)
                selected.zones[(size_t) count++] = (int) i;
        }
        if (count == 1) selected.weights[0] = 1;
        if (count == 2)
        {
            if (zones[(size_t) selected.zones[0]].lowVel > zones[(size_t) selected.zones[1]].lowVel) std::swap (selected.zones[0], selected.zones[1]);
            const auto& a = zones[(size_t) selected.zones[0]]; const auto& b = zones[(size_t) selected.zones[1]];
            const int low = std::max (a.lowVel - a.crossfadeVel, b.lowVel - b.crossfadeVel);
            const int high = std::min (a.highVel + a.crossfadeVel, b.highVel + b.crossfadeVel);
            const float t = high > low ? juce::jlimit (0.0f, 1.0f, (float) (velocity - low) / (float) (high - low)) : .5f;
            selected.weights = a.crossfadeLaw == VelocityCrossfadeLaw::linear
                ? std::array<float, 2> { 1 - t, t }
                : std::array<float, 2> { std::cos (t * juce::MathConstants<float>::halfPi), std::sin (t * juce::MathConstants<float>::halfPi) };
        }
        return selected;
    }
    template <typename Fn> void forEachAsset (Fn&& fn) const
    {
        for (const auto& z : zones) { fn (z.sample.get ()); for (const auto& v : z.variants) fn (v.sample.get ()); }
    }
    size_t bytes () const noexcept
    {
        std::array<const Sample*, kMaxSampleZones * 5> seen {};
        size_t used = 0, count = 0;
        forEachAsset ([&] (const Sample* asset)
        {
            if (asset && std::find (seen.begin (), seen.begin () + (ptrdiff_t) used, asset) == seen.begin () + (ptrdiff_t) used)
            { if (used < seen.size ()) seen[used++] = asset; count += asset->bytes (); }
        });
        return count;
    }
};
inline juce::ValueTree serializeSampleBank (const SampleBank& bank)
{
    juce::ValueTree tree ("SAMPLE_BANK"); tree.setProperty ("version", 2, nullptr);
    std::array<const Sample*, kMaxSampleZones * 4> assets {};
    for (size_t i = 0; i < bank.zones.size (); ++i)
    {
        const auto& z = bank.zones[i];
        juce::ValueTree zone ("ZONE");
        zone.setProperty ("low", z.lowKey, nullptr); zone.setProperty ("high", z.highKey, nullptr);
        zone.setProperty ("root", z.rootKey, nullptr); zone.setProperty ("start", z.start, nullptr);
        zone.setProperty ("end", z.end, nullptr); zone.setProperty ("loop", z.loop, nullptr);
        zone.setProperty ("lowVel", z.lowVel, nullptr); zone.setProperty ("highVel", z.highVel, nullptr);
        zone.setProperty ("crossfadeVel", z.crossfadeVel, nullptr); zone.setProperty ("law", (int) z.crossfadeLaw, nullptr);
        zone.setProperty ("numVariants", z.numVariants, nullptr);
        zone.setProperty ("loopMode", z.loopMode, nullptr); zone.setProperty ("loopStart", z.loopStart, nullptr);
        zone.setProperty ("loopEnd", z.loopEnd, nullptr); zone.setProperty ("loopLink", z.loopLinked, nullptr);
        zone.setProperty ("reverse", z.reverse, nullptr);
        zone.setProperty ("tune", z.tuneCents, nullptr); zone.setProperty ("pan", z.pan, nullptr); zone.setProperty ("gain", z.gainDb, nullptr);
        for (int v = 0; v < z.numVariants; ++v)
        {
            juce::ValueTree node = v == 0 ? zone : juce::ValueTree ("VARIANT");
            if (v > 0) node.setProperty ("slot", v, nullptr);
            const auto* asset = z.sampleForVariant (v); const auto path = z.pathForVariant (v);
            const size_t index = i * 4 + (size_t) v;
            node.setProperty ("path", path.getFullPathName (), nullptr);
            if (! asset) node.setProperty ("missing", true, nullptr);
            else
            {
                const auto found = std::find (assets.begin (), assets.begin () + (ptrdiff_t) index, asset);
                if (found != assets.begin () + (ptrdiff_t) index)
                {
                    const int reference = (int) std::distance (assets.begin (), found);
                    if (v == 0 && reference % 4 == 0) node.setProperty ("sampleZone", reference / 4, nullptr);
                    else node.setProperty ("assetRef", reference, nullptr);
                }
                else node.addChild (serializeSampleContainer (asset, path), -1, nullptr);
            }
            assets[index] = asset;
            if (v > 0) zone.addChild (node, -1, nullptr);
        }
        tree.addChild (zone, -1, nullptr);
    }
    if (bank.sliced)
    {
        juce::ValueTree slices ("SLICES");
        juce::StringArray markers;
        for (int f : bank.slices.markers) markers.add (juce::String (f));
        slices.setProperty ("markers", markers.joinIntoString (","), nullptr);
        slices.setProperty ("root", bank.slices.root, nullptr); slices.setProperty ("playToEnd", bank.slices.playToEnd, nullptr);
        tree.addChild (slices, -1, nullptr);
    }
    return tree;
}
inline std::shared_ptr<const SampleBank> deserializeSampleBank (const juce::ValueTree& tree, juce::String& error)
{
    const int version = tree.getProperty ("version", 0);
    const auto slicesNode = tree.getChildWithName ("SLICES");
    const int zoneCount = tree.getNumChildren () - (slicesNode.isValid () ? 1 : 0);
    if ((version != 1 && version != 2) || zoneCount < 1 || zoneCount > kMaxSampleZones)
    { error = "Invalid sample bank header"; return {}; }
    auto bank = std::make_shared<SampleBank> ();
    std::array<std::shared_ptr<const Sample>, kMaxSampleZones * 4> assets {};
    size_t remaining = kMaxAssetMemoryBytes;
    for (const auto& node : tree)
    {
        if (node == slicesNode) continue;
        if (! node.hasType ("ZONE")) { error = "Invalid sample zone"; return {}; }
        SampleZone z;
        auto integer = [&] (const char* key, int fallback, int lo, int hi, int& out)
        {
            const double value = (double) node.getProperty (key, fallback);
            if (! std::isfinite (value) || value < lo || value > hi || value != std::floor (value)) return false;
            out = (int) value; return true;
        };
        int law = 0;
        if (! integer ("low", -1, 0, 127, z.lowKey) || ! integer ("high", -1, 0, 127, z.highKey)
            || ! integer ("root", -1, 0, 127, z.rootKey) || ! integer ("lowVel", 1, 1, 127, z.lowVel)
            || ! integer ("highVel", 127, 1, 127, z.highVel) || ! integer ("crossfadeVel", 0, 0, 63, z.crossfadeVel)
            || ! integer ("law", 0, 0, 1, law) || ! integer ("numVariants", 1, 1, 4, z.numVariants)
            || ! integer ("loopMode", 0, 0, 3, z.loopMode))
        { error = "Invalid layer or variant header"; return {}; }
        z.crossfadeLaw = (VelocityCrossfadeLaw) law;
        z.start = node.getProperty ("start", -1.0f); z.end = node.getProperty ("end", -1.0f); z.loop = node.getProperty ("loop", false);
        z.loopStart = node.getProperty ("loopStart", 0.0f); z.loopEnd = node.getProperty ("loopEnd", 1.0f);
        z.loopLinked = node.getProperty ("loopLink", true); z.reverse = node.getProperty ("reverse", false);
        z.tuneCents = node.getProperty ("tune", 0.0f); z.pan = node.getProperty ("pan", 0.0f); z.gainDb = node.getProperty ("gain", 0.0f);
        int variantChildren = 0;
        for (const auto& child : node)
            if (child.hasType ("VARIANT")) ++variantChildren;
            else if (! child.hasType ("SAMPLE")) { error = "Invalid zone child"; return {}; }
        if (variantChildren != z.numVariants - 1 || (version == 1 && variantChildren != 0))
        { error = "Invalid variant count"; return {}; }
        for (int v = 0; v < z.numVariants; ++v)
        {
            const auto entry = v == 0 ? node : node.getChildWithProperty ("slot", v);
            if (! entry.isValid () || (v > 0 && ! entry.hasType ("VARIANT"))) { error = "Missing variant slot"; return {}; }
            auto& variant = z.variants[(size_t) v];
            const auto path = entry.getProperty ("path").toString ();
            if (juce::File::isAbsolutePath (path)) variant.path = juce::File (path);
            variant.missing = entry.getProperty ("missing", false);
            const size_t index = bank->zones.size () * 4 + (size_t) v;
            int payloads = 0;
            for (const auto& child : entry) if (child.hasType ("SAMPLE")) ++payloads;
            const bool hasReference = entry.hasProperty ("sampleZone") || entry.hasProperty ("assetRef");
            if ((entry.hasProperty ("sampleZone") && entry.hasProperty ("assetRef")) || payloads > 1
                || (variant.missing && (version == 1 || hasReference || payloads != 0)))
            { error = "Invalid variant payload"; return {}; }
            if (hasReference)
            {
                const double value = entry.hasProperty ("sampleZone") ? 4.0 * (double) entry.getProperty ("sampleZone") : (double) entry.getProperty ("assetRef");
                if (! std::isfinite (value) || value < 0 || value >= (double) index || value != std::floor (value) || payloads != 0 || ! assets[(size_t) value])
                { error = "Invalid zone sample reference"; return {}; }
                variant.sample = assets[(size_t) value];
            }
            else if (! variant.missing)
            {
                bool embedded = false;
                if (payloads != 1 || ! deserializeSampleContainer (entry.getChildWithName ("SAMPLE"), variant.sample, variant.path, embedded, error, remaining) || ! variant.sample)
                { if (error.isEmpty ()) error = "Variant requires embedded audio"; return {}; }
                remaining -= variant.sample->bytes ();
            }
            assets[index] = variant.sample;
            if (v == 0) { z.sample = variant.sample; z.path = variant.path; variant.sample.reset (); variant.path = {}; }
        }
        bank->zones.push_back (std::move (z));
    }
    if (! bank->valid ()) { error = "Invalid or overlapping key/velocity ranges"; return {}; }
    if (slicesNode.isValid ())
    {
        const auto* asset = bank->zones[0].sample.get ();
        std::vector<int> markers;
        for (const auto& token : juce::StringArray::fromTokens (slicesNode.getProperty ("markers").toString (), ",", ""))
            if (token.trim ().containsOnly ("0123456789") && token.trim ().isNotEmpty () && token.length () <= 9) markers.push_back (token.getIntValue ());
        const int root = slicesNode.getProperty ("root", -1);
        if (! asset || root < 0 || root > 127) { error = "Invalid slice map"; return {}; }
        bank->sliced = true;
        bank->slices = { sanitiseSliceMarkers (std::move (markers), asset->numFrames ()), root, (bool) slicesNode.getProperty ("playToEnd", false) };
    }
    return bank;
}

// Zones for slices of `sample`; refuses maps that would run past MIDI note 127.
inline std::shared_ptr<const SampleBank> buildSliceBank (std::shared_ptr<const Sample> sample, const juce::File& path, SliceMap map, juce::String& error)
{
    if (! sample || sample->numFrames () < kMinSampleRegion) { error = "Load a sample before slicing"; return {}; }
    map.markers = sanitiseSliceMarkers (std::move (map.markers), sample->numFrames ());
    const int count = (int) map.markers.size ();
    if (map.root < 0 || map.root + count - 1 > 127) { error = "Slices from the root note run past MIDI note 127"; return {}; }
    auto bank = std::make_shared<SampleBank> ();
    const double frames = sample->numFrames ();
    for (int i = 0; i < count; ++i)
    {
        SampleZone zone;
        zone.sample = sample; zone.path = path;
        zone.lowKey = zone.highKey = zone.rootKey = map.root + i;
        zone.start = (float) (map.markers[(size_t) i] / frames);
        zone.end = map.playToEnd || i + 1 == count ? 1.0f : (float) (map.markers[(size_t) i + 1] / frames);
        bank->zones.push_back (std::move (zone));
    }
    bank->sliced = true; bank->slices = std::move (map);
    if (! bank->valid ()) { error = "Invalid slice map"; return {}; }
    return bank;
}

struct SampleTrigger
{
    int note = -1, velocity = 0;
    VelocityLayers layers;
    std::array<int, 2> variants {};
    uint64_t serial = 0;
};
struct SamplePlaybackState
{
    std::array<uint32_t, kMaxSampleZones> counters {};
    std::array<std::atomic<int>, kMaxSampleZones> lastVariant {};
    SampleTrigger current;
    uint64_t serial = 0;
    void reset () noexcept
    {
        counters.fill (0); serial = 0; current = {};
        for (auto& v : lastVariant) v.store (-1, std::memory_order_relaxed);
    }
    SampleTrigger trigger (const SampleBank* bank, int note, int velocity) noexcept
    {
        SampleTrigger result; result.note = note; result.velocity = velocity;
        if (velocity < 1 || velocity > 127) return result;
        result.serial = ++serial;
        if (bank)
        {
            result.layers = bank->selectVelocityLayers (note, velocity);
            for (size_t l = 0; l < 2; ++l)
                if (const int index = result.layers.zones[l]; index >= 0 && result.layers.weights[l] > 0)
                {
                    const int count = bank->zones[(size_t) index].numVariants;
                    result.variants[l] = (int) (counters[(size_t) index] % (uint32_t) count);
                    counters[(size_t) index] = (counters[(size_t) index] + 1u) % (uint32_t) count;
                    lastVariant[(size_t) index].store (result.variants[l], std::memory_order_relaxed);
                }
        }
        return result;
    }
};

// How a voice plays its region: direction, an inner loop window and its mode, and the loop/edge fades.
// Linked loop points are fractions of the region (they move with start/end); unlinked ones are fractions of
// the file, clamped into the region. Defaults reproduce the original whole-region forward loop.
enum class LoopMode { forward = 0, reverse, pingPong, tailed };
inline juce::StringArray loopModeNames () { return { "Forward", "Reverse", "Ping-Pong", "Tailed" }; }
struct PlaybackSettings
{
    bool loop = false, reverse = false, linked = true;
    LoopMode mode = LoopMode::forward;
    float loopStart = 0, loopEnd = 1;
    float crossfadeSeconds = .01f, fadeInSeconds = 0, fadeOutSeconds = .01f;
};

// One voice's read head, in play-order coordinates v (0 at the start of the region in the play direction, so
// reverse mirrors everything). Geometry is taken at the first read and again at every wrap or bounce, so moving
// start/end/loop points never jumps the head: a loop pass ends no sooner than one crossfade after the wrap.
// Forward loop: the last `xf` frames before the loop end crossfade into the frames one period earlier, so the
// wrap never steps; Reverse loop: after reaching the loop end it bounces once, then loops backwards with the
// mirrored crossfade; Ping-Pong bounces at both loop ends; Tailed loops forwards until a release is seen on
// entering the crossfade (or at the loop end without one), then plays on to the region end.
// Without a loop (or in the tail) the last fade-out frames fade, then the head stops.
struct SamplePlayer
{
    double position = 0; // file frame of the head
    bool finished = false, tail = false;

    void reset (std::pair<double, double> region) noexcept
    {
        std::tie (from, to) = region; position = from; v = 0; travelled = 0; direction = 1;
        finished = tail = captured = inZone = false;
    }

    std::pair<float, float> nextStereo (const Sample& sample, std::pair<double, double> region, const PlaybackSettings& s,
                                        double increment, bool released = false) noexcept
    {
        if (finished) return { 0.0f, 0.0f };
        if (! captured) capture (sample, region, s);
        const bool looping = s.loop && ! tail;
        if (! looping) direction = 1; // a loop switched off mid-bounce plays on towards the end
        if (looping && s.mode == LoopMode::pingPong)
        {
            if (direction > 0 && v >= vb) { v = std::max (va, 2 * (vb - 1) - v); direction = -1; capture (sample, region, s); }
            else if (direction < 0 && v < va) { v = std::min (vb - 1, 2 * va - v); direction = 1; capture (sample, region, s); }
        }
        else if (looping && s.mode == LoopMode::reverse)
        {
            if (direction > 0 && v >= vb) { v = std::max (va, 2 * (vb - 1) - v); direction = -1; capture (sample, region, s); }
            else if (direction < 0 && v < va) { v += period * (std::floor ((va - v) / period) + 1); inZone = false; capture (sample, region, s); }
        }
        else if (looping)
        {
            if (! inZone && v >= vb - xf)
            {
                inZone = true;
                if (s.mode == LoopMode::tailed && released) tail = true;
            }
            if (! tail && v >= vb) { v -= period * (std::floor ((v - vb) / period) + 1); inZone = false; capture (sample, region, s); }
        }
        if (direction > 0 && v >= length) { finished = true; return { 0.0f, 0.0f }; }

        auto read = [&] (double at) { return sample.readFilteredStereo (fileAt (at), increment); };
        auto [outL, outR] = read (v);
        const bool wrapping = s.loop && ! tail && s.mode != LoopMode::pingPong && xf > 0;
        if (wrapping && direction > 0 && s.mode != LoopMode::reverse && v > vb - xf)
        {
            const float t = (float) std::min (1.0, (v - (vb - xf)) / xf);
            const auto [wrapL, wrapR] = read (v - period);
            outL += t * (wrapL - outL); outR += t * (wrapR - outR);
        }
        else if (wrapping && direction < 0 && v < va + xf)
        {
            const float t = (float) std::min (1.0, ((va + xf) - v) / xf);
            const auto [wrapL, wrapR] = read (v + period);
            outL += t * (wrapL - outL); outR += t * (wrapR - outR);
        }
        float gain = 1;
        if (fadeIn > 0 && travelled < fadeIn) gain *= (float) (travelled / fadeIn);
        if ((! s.loop || tail) && fadeOut > 0 && v > length - fadeOut) gain *= (float) std::max (0.0, (length - v) / fadeOut);
        v += direction * increment; travelled += increment;
        position = fileAt (v);
        return { outL * gain, outR * gain };
    }
    std::pair<float, float> nextStereo (const Sample& sample, std::pair<double, double> region, bool loop, double increment) noexcept
    {
        PlaybackSettings settings; settings.loop = loop;
        return nextStereo (sample, region, settings, increment);
    }
    float next (const Sample& sample, std::pair<double, double> region, bool loop, double increment) noexcept
    {
        return nextStereo (sample, region, loop, increment).first;
    }

private:
    double fileAt (double at) const noexcept { return reverse ? (to - 1) - at : from + at; }
    void capture (const Sample& sample, std::pair<double, double> region, const PlaybackSettings& s) noexcept
    {
        const double file = fileAt (v), frames = (double) sample.numFrames ();
        if (! captured) reverse = s.reverse;
        from = juce::jlimit (0.0, frames, std::min (region.first, region.second));
        to = juce::jlimit (from, frames, std::max (region.first, region.second));
        const double span = to - from;
        auto fraction = [] (float value, float fallback) { return std::isfinite (value) ? juce::jlimit (0.0f, 1.0f, value) : fallback; };
        double ls = s.linked ? from + fraction (s.loopStart, 0) * span : fraction (s.loopStart, 0) * frames;
        double le = s.linked ? from + fraction (s.loopEnd, 1) * span : fraction (s.loopEnd, 1) * frames;
        ls = juce::jlimit (from, to, ls); le = juce::jlimit (from, to, le);
        if (ls > le) std::swap (ls, le);
        if (le - ls < kMinSampleRegion) { le = std::min (to, ls + kMinSampleRegion); ls = std::max (from, le - kMinSampleRegion); }
        if (captured) v = reverse ? (to - 1) - file : file - from;
        captured = true;
        va = reverse ? to - le : ls - from; vb = reverse ? to - ls : le - from;
        auto seconds = [&] (float value, float fallback) { return (std::isfinite (value) ? juce::jlimit (0.0f, 2.0f, value) : fallback) * sample.rate; };
        xf = std::min (seconds (s.crossfadeSeconds, .01f), (vb - va) * .5);
        // After a forward wrap the pass ends no sooner than one crossfade later, even if the end moved before it.
        if (! reverse && direction > 0 && s.loop && s.mode != LoopMode::pingPong && vb < v + xf)
        {
            vb = std::min (v + xf, frames - from);
            to = std::max (to, from + vb);
        }
        period = std::max (1.0, vb - va - xf);
        length = to - from;
        fadeIn = std::min (seconds (s.fadeInSeconds, 0), length * .5);
        fadeOut = std::min (seconds (s.fadeOutSeconds, .01f), length * .5);
    }

    double from = 0, to = 0, v = 0, va = 0, vb = 0, xf = 0, period = 1, length = 0, fadeIn = 0, fadeOut = 0, travelled = 0;
    int direction = 1;
    bool reverse = false, captured = false, inZone = false;
};
} // namespace bw
