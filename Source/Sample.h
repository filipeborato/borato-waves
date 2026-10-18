#pragma once
#include "UserWavetable.h"

namespace bw
{
// Imported audio for the sample source: a mono mixdown at the file's own rate, immutable once published.
struct Sample
{
    std::vector<float> data;
    double rate = 44100;

    // Cubic Hermite read; frames outside the data read as the nearest end.
    float read (double position) const noexcept
    {
        const int last = (int) data.size() - 1;
        const int n = (int) std::floor (position);
        const float t = (float) (position - n);
        auto at = [&] (int i) { return data[(size_t) juce::jlimit (0, last, i)]; };
        const float y0 = at (n - 1), y1 = at (n), y2 = at (n + 1), y3 = at (n + 2);
        return y1 + 0.5f * t * (y2 - y0 + t * (2 * y0 - 5 * y1 + 4 * y2 - y3 + t * (3 * (y1 - y2) + y3 - y0)));
    }
    // Normalised start/end (either order) to frames, at least kMinSampleRegion apart.
    std::pair<double, double> region (float start, float end) const noexcept;
};
constexpr int kMaxSampleFrames = 1 << 22; // 16 MB of mono floats, about 95 s at 44.1 kHz
constexpr int kMinSampleRegion = 64;      // frames between start and end
constexpr int kSampleRootNote = 60;       // the note that plays the file at its own pitch

inline std::pair<double, double> Sample::region (float start, float end) const noexcept
{
    const double length = (double) data.size();
    const double lo = juce::jlimit (0.0, length - kMinSampleRegion, std::min (start, end) * length);
    return { lo, juce::jlimit (lo + kMinSampleRegion, length, std::max (start, end) * length) };
}

// Decodes on the calling (non-audio) thread. Longer files are refused rather than truncated.
inline std::shared_ptr<const Sample> loadSample (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) { error = "Unsupported or unreadable audio file"; return {}; }
    if (reader->lengthInSamples > kMaxSampleFrames)
    {
        error = "Sample too long (max " + juce::String (kMaxSampleFrames) + " frames)";
        return {};
    }
    if (reader->lengthInSamples < kMinSampleRegion) { error = "Sample too short"; return {}; }
    const int length = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, length);
    buffer.clear();
    reader->read (&buffer, 0, length, 0, true, true);
    auto sample = std::make_shared<Sample>();
    sample->rate = reader->sampleRate > 0 ? reader->sampleRate : 44100;
    sample->data.assign ((size_t) length, 0.0f);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        juce::FloatVectorOperations::addWithMultiply (sample->data.data(), buffer.getReadPointer (ch), 1.0f / (float) buffer.getNumChannels(), length);
    return sample;
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
    float next (const Sample& sample, std::pair<double, double> region, bool loop, double increment) noexcept
    {
        if (finished) return 0;
        const double maxFade = sample.rate * 0.01;
        if (position >= to)
        {
            if (! loop) { finished = true; return 0; }
            const double period = to - from - std::min (maxFade, (to - from) * 0.5);
            position -= period * (std::floor ((position - to) / period) + 1);
            from = region.first;
            to = std::min ((double) sample.data.size(), std::max (region.second, position + maxFade));
        }
        const double fade = std::min (maxFade, (to - from) * 0.5), period = to - from - fade;
        float out = sample.read (position);
        if (const double into = position - (to - fade); into > 0)
        {
            const float t = (float) std::min (1.0, into / fade);
            out = loop ? out + t * (sample.read (position - period) - out) : out * (1 - t);
        }
        position += increment;
        return out;
    }
};
} // namespace bw
