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
        if (data.empty () || ! std::isfinite (position) || ! std::isfinite (increment) || increment <= 0)
            return {};
        // Bound worst-case callback work to 4096 taps; extreme ratios fade to silence.
        const double guard = juce::jlimit (0.0, 1.0, (64.0 - increment) / 16.0);
        if (guard <= 0) return {};
        const double step = std::max (1.0, increment), support = SincKernelTable::radius * step;
        const int lo = juce::jlimit (0, numFrames () - 1, (int) first);
        const int hi = last < 0 ? numFrames () - 1 : juce::jlimit (lo, numFrames () - 1, (int) std::ceil (last) - 1);
        position = juce::jlimit ((double) lo, (double) hi, position);
        const bool stereo = isStereo ();
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
struct SampleZone
{
    std::shared_ptr<const Sample> sample;
    juce::File path;
    int lowKey = 0, highKey = 127, rootKey = 60;
    float start = 0, end = 1;
    bool loop = false;
};
struct SampleBank
{
    std::vector<SampleZone> zones;
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
                if (zones[i].lowKey <= zones[j].highKey && zones[j].lowKey <= zones[i].highKey) return true;
        return false;
    }
    bool valid () const noexcept
    {
        if (zones.empty () || zones.size () > kMaxSampleZones || hasKeyOverlap ()) return false;
        for (const auto& z : zones)
            if (! z.sample || z.sample->numFrames () < kMinSampleRegion || z.sample->numFrames () > kMaxSampleFrames
                || ! std::isfinite (z.sample->rate) || z.sample->rate <= 0
                || (! z.sample->right.empty () && z.sample->right.size () != z.sample->data.size ()) || z.lowKey < 0 || z.highKey > 127
                || z.lowKey > z.highKey || z.rootKey < 0 || z.rootKey > 127 || ! std::isfinite (z.start)
                || ! std::isfinite (z.end) || z.start < 0 || z.end > 1 || z.start >= z.end) return false;
        return true;
    }
    size_t bytes () const noexcept
    {
        size_t count = 0;
        for (size_t i = 0; i < zones.size (); ++i)
        {
            bool seen = false;
            for (size_t j = 0; j < i; ++j) seen |= zones[j].sample == zones[i].sample;
            if (! seen && zones[i].sample) count += zones[i].sample->bytes ();
        }
        return count;
    }
};
inline juce::ValueTree serializeSampleBank (const SampleBank& bank)
{
    juce::ValueTree tree ("SAMPLE_BANK");
    tree.setProperty ("version", 1, nullptr);
    for (size_t i = 0; i < bank.zones.size (); ++i)
    {
        const auto& z = bank.zones[i];
        juce::ValueTree zone ("ZONE");
        zone.setProperty ("low", z.lowKey, nullptr); zone.setProperty ("high", z.highKey, nullptr);
        zone.setProperty ("root", z.rootKey, nullptr); zone.setProperty ("start", z.start, nullptr);
        zone.setProperty ("end", z.end, nullptr); zone.setProperty ("loop", z.loop, nullptr);
        int reference = -1;
        for (size_t j = 0; j < i; ++j) if (bank.zones[j].sample == z.sample) { reference = (int) j; break; }
        zone.setProperty ("path", z.path.getFullPathName (), nullptr);
        if (reference >= 0) zone.setProperty ("sampleZone", reference, nullptr);
        else zone.addChild (serializeSampleContainer (z.sample.get (), z.path), -1, nullptr);
        tree.addChild (zone, -1, nullptr);
    }
    return tree;
}
inline std::shared_ptr<const SampleBank> deserializeSampleBank (const juce::ValueTree& tree, juce::String& error)
{
    if ((int) tree.getProperty ("version", 0) != 1 || tree.getNumChildren () < 1 || tree.getNumChildren () > kMaxSampleZones)
    { error = "Invalid sample bank header"; return {}; }
    auto bank = std::make_shared<SampleBank> ();
    size_t remaining = kMaxAssetMemoryBytes;
    for (const auto& node : tree)
    {
        if (! node.hasType ("ZONE")) { error = "Invalid sample zone"; return {}; }
        SampleZone z;
        z.lowKey = node.getProperty ("low", -1); z.highKey = node.getProperty ("high", -1);
        z.rootKey = node.getProperty ("root", -1); z.start = node.getProperty ("start", -1.0f);
        z.end = node.getProperty ("end", -1.0f); z.loop = node.getProperty ("loop", false);
        for (const auto* key : { "low", "high", "root" })
        {
            const double value = (double) node.getProperty (key, -1);
            if (! std::isfinite (value) || value < 0 || value > 127 || value != std::floor (value))
            { error = "Invalid MIDI key"; return {}; }
        }
        if (node.hasProperty ("sampleZone"))
        {
            const double value = (double) node.getProperty ("sampleZone");
            if (! std::isfinite (value) || value < 0 || value >= (double) bank->zones.size ()
                || value != std::floor (value) || node.getNumChildren () != 0)
            { error = "Invalid zone sample reference"; return {}; }
            z.sample = bank->zones[(size_t) value].sample;
            const auto path = node.getProperty ("path").toString ();
            if (juce::File::isAbsolutePath (path)) z.path = juce::File (path);
        }
        else
        {
            bool embedded = false;
            if (node.getNumChildren () != 1 || ! deserializeSampleContainer (node.getChildWithName ("SAMPLE"), z.sample, z.path, embedded, error, remaining)
                || ! z.sample) { if (error.isEmpty ()) error = "Zone requires embedded audio"; return {}; }
            remaining -= z.sample->bytes ();
        }
        bank->zones.push_back (std::move (z));
    }
    if (! bank->valid ()) { error = "Invalid or overlapping key ranges"; return {}; }
    return bank;
}

// One voice's read head. The region (frames) is taken at reset and, in a loop, again at each wrap, so
// moving start/end never jumps the head: a loop pass ends no sooner than one fade after the wrap.
// Loop: the last `fade` frames crossfade into the frames one period earlier, so the wrap never steps.
// One-shot: the last `fade` frames fade out, then the head stops.
struct SamplePlayer
{
    double position = 0, from = 0, to = 0;
    bool finished = false;

    void reset (std::pair<double, double> region) noexcept { std::tie (from, to) = region; position = from; finished = false; }

    std::pair<float, float> nextStereo (const Sample& sample, std::pair<double, double> region, bool loop, double increment) noexcept
    {
        if (finished) return { 0.0f, 0.0f };
        const double maxFade = sample.rate * 0.01;
        if (position >= to)
        {
            if (! loop) { finished = true; return { 0.0f, 0.0f }; }
            const double period = to - from - std::min (maxFade, (to - from) * 0.5);
            position -= period * (std::floor ((position - to) / period) + 1);
            from = region.first;
            to = std::min ((double) sample.data.size(), std::max (region.second, position + maxFade));
        }
        const double fade = std::min (maxFade, (to - from) * 0.5), period = to - from - fade;
        auto [outL, outR] = sample.readFilteredStereo (position, increment);
        if (const double into = position - (to - fade); into > 0)
        {
            const float t = (float) std::min (1.0, into / fade);
            if (loop)
            {
                const auto [wrapL, wrapR] = sample.readFilteredStereo (position - period, increment);
                outL += t * (wrapL - outL);
                outR += t * (wrapR - outR);
            }
            else
            {
                outL *= (1.0f - t);
                outR *= (1.0f - t);
            }
        }
        position += increment;
        return { outL, outR };
    }

    float next (const Sample& sample, std::pair<double, double> region, bool loop, double increment) noexcept
    {
        return nextStereo (sample, region, loop, increment).first;
    }
};
} // namespace bw
