#pragma once
#include "Sample.h"
#include <juce_dsp/juce_dsp.h>

namespace bw
{
// Short-time Fourier analysis of a sample, resynthesised per voice by a phase vocoder.
// Analysis and synthesis both use a periodic root-Hann window w[n] = sqrt (0.5 (1 - cos (2 pi n / N))):
// at hop N/4 the overlapped w^2 sums to exactly 2, so overlap-add scaled by 0.5 is the identity (COLA).
// Frame m covers padded input [m H, m H + N), where the padded input is the sample delayed by kSpectralPad
// zeros (reads past the end are zeros too). After frame m is added, the first H output samples are complete
// and equal padded [m H, m H + H). A voice primes three frames at note-on, so the output has no added latency.
constexpr int kSpectralFftOrder = 11, kSpectralFftSize = 1 << kSpectralFftOrder, kSpectralHop = kSpectralFftSize / 4;
constexpr int kSpectralBins = kSpectralFftSize / 2 + 1, kSpectralPad = kSpectralFftSize - kSpectralHop;
constexpr int kSpectralAlgorithmVersion = 1;
constexpr int kSpectralLatencySamples = 0;

inline const auto spectralWindow = []
{
    std::array<float, kSpectralFftSize> w {};
    for (size_t n = 0; n < w.size (); ++n)
        w[n] = (float) std::sqrt (.5 * (1 - std::cos (juce::MathConstants<double>::twoPi * (double) n / kSpectralFftSize)));
    return w;
} ();

inline double principalArgument (double phase) noexcept
{
    return phase - juce::MathConstants<double>::twoPi * std::round (phase / juce::MathConstants<double>::twoPi);
}

// Per frame and bin: each channel's magnitude and phase, and for stereo the phase of a reference signal,
// whichever of L + R and L - R is stronger in that bin (mono uses its own phase). Instantaneous frequency
// comes from the reference alone, so both channels advance identically and keep their analysed L/R relation.
struct SpectralData
{
    double rate = 44100;
    int channels = 1, frames = 0, sourceFrames = 0;
    uint64_t sourceHash = 0;
    int algorithmVersion = kSpectralAlgorithmVersion;
    std::vector<float> magnitude, phase; // [channel][frame][bin]
    std::vector<float> reference;        // [frame][bin], stereo only

    static int frameCount (int sourceFrames) noexcept { return (sourceFrames + kSpectralPad - 1) / kSpectralHop + 1; }
    static size_t estimateBytes (int sourceFrames, int channels) noexcept
    { return (size_t) (channels == 2 ? 5 : 2) * (size_t) frameCount (sourceFrames) * kSpectralBins * sizeof (float); }
    size_t bytes () const noexcept { return (magnitude.size () + phase.size () + reference.size ()) * sizeof (float); }
    size_t offset (int channel, int frame) const noexcept
    { return ((size_t) juce::jlimit (0, channels - 1, channel) * (size_t) frames + (size_t) juce::jlimit (0, frames - 1, frame)) * kSpectralBins; }
    const float* magnitudes (int channel, int frame) const noexcept { return &magnitude[offset (channel, frame)]; }
    const float* phases (int channel, int frame) const noexcept { return &phase[offset (channel, frame)]; }
    const float* referencePhases (int frame) const noexcept { return channels == 2 ? &reference[offset (0, frame)] : phases (0, frame); }
    // Frame position (in frames) whose first completed output sample is source frame `sourceFrame`.
    static double frameFor (double sourceFrame) noexcept { return (sourceFrame + kSpectralPad) / kSpectralHop; }
};

// Runs on a worker thread. The caller checks estimateBytes against the budget before calling.
inline std::shared_ptr<const SpectralData> analyzeSpectral (const Sample& sample, juce::String& error,
                                                           const std::function<bool()>& shouldCancel = {},
                                                           const std::function<void (float)>& onProgress = {})
{
    if (sample.numFrames () < kMinSampleRegion) { error = "Sample too short for spectral analysis"; return {}; }
    auto data = std::make_shared<SpectralData> ();
    data->rate = sample.rate; data->channels = sample.numChannels (); data->sourceFrames = sample.numFrames ();
    data->frames = SpectralData::frameCount (sample.numFrames ()); data->sourceHash = sample.contentHash;
    const size_t values = (size_t) data->channels * (size_t) data->frames * kSpectralBins;
    data->magnitude.resize (values); data->phase.resize (values);
    if (data->channels == 2) data->reference.resize ((size_t) data->frames * kSpectralBins);
    juce::dsp::FFT fft (kSpectralFftOrder);
    std::array<std::vector<float>, 2> buffers { std::vector<float> (2 * kSpectralFftSize), std::vector<float> (2 * kSpectralFftSize) };
    for (int m = 0; m < data->frames; ++m)
    {
        if ((m & 63) == 0)
        {
            if (shouldCancel && shouldCancel ()) { error = "Spectral analysis cancelled"; return {}; }
            if (onProgress) onProgress ((float) m / (float) data->frames);
        }
        for (int ch = 0; ch < data->channels; ++ch)
        {
            const auto& source = ch == 1 ? sample.right : sample.data;
            auto& buffer = buffers[(size_t) ch];
            std::fill (buffer.begin (), buffer.end (), 0.0f);
            for (int n = 0; n < kSpectralFftSize; ++n)
                if (const int index = m * kSpectralHop + n - kSpectralPad; index >= 0 && index < sample.numFrames ())
                    buffer[(size_t) n] = source[(size_t) index] * spectralWindow[(size_t) n];
            fft.performRealOnlyForwardTransform (buffer.data (), true);
            const size_t at = data->offset (ch, m);
            for (size_t k = 0; k < (size_t) kSpectralBins; ++k)
            {
                const float re = buffer[2 * k], im = buffer[2 * k + 1];
                data->magnitude[at + k] = std::sqrt (re * re + im * im);
                data->phase[at + k] = std::atan2 (im, re);
            }
        }
        if (data->channels == 2)
            for (size_t k = 0; k < (size_t) kSpectralBins; ++k)
            {
                const auto& l = buffers[0]; const auto& r = buffers[1];
                const float midRe = l[2 * k] + r[2 * k], midIm = l[2 * k + 1] + r[2 * k + 1];
                const float sideRe = l[2 * k] - r[2 * k], sideIm = l[2 * k + 1] - r[2 * k + 1];
                const bool mid = midRe * midRe + midIm * midIm >= sideRe * sideRe + sideIm * sideIm;
                data->reference[data->offset (0, m) + k] = mid ? std::atan2 (midIm, midRe) : std::atan2 (sideIm, sideRe);
            }
    }
    if (shouldCancel && shouldCancel ()) { error = "Spectral analysis cancelled"; return {}; }
    if (onProgress) onProgress (1);
    return data;
}

struct SpectralControls
{
    double startFrame = 0, endFrame = 0; // region, in frames (SpectralData::frameFor)
    double position = 0;                 // 0..1 within the region: note-on start, and the held frame in freeze
    double speed = 1;                    // analysis frames per synthesis hop at equal rates
    double pitch = 1;                    // frequency ratio, including key tracking
    bool freeze = false, loop = false;
};

// One voice's resynthesis. Every hop it reads the analysis at a fractional frame, interpolating magnitudes and
// instantaneous frequencies between frames, and advances one phase accumulator per output bin, shared by both
// channels (so freeze, scrub and speed changes never restart phases and stereo keeps its relation). For pitch,
// each spectral peak's region moves by whole bins with its phases locked to the peak (Laroche & Dolson); peaks are
// found on the summed channel magnitudes so both channels share regions. At unit speed and pitch, at integer
// frames, the accumulator telescopes to the analysed reference phase and playback is the identity reconstruction.
class SpectralVoicePlayer
{
public:
    bool finished = true;

    void reset (const SpectralData* source, const SpectralControls& controls, double hostRate) noexcept
    {
        data = source; finished = data == nullptr; accumulator = {}; hopPosition = 0; initialised = false;
        if (finished) return;
        rateRatio = data->rate / (std::isfinite (hostRate) && hostRate > 0 ? hostRate : data->rate);
        const auto [first, last] = bounds (controls);
        framePosition = first + juce::jlimit (0.0, 1.0, finiteOr (controls.position, 0)) * (last - first);
        const double step = controls.freeze ? 0.0 : stepFor (controls);
        framePosition -= 3 * step;
        // Prime the three frames that overlap the first output samples, then drop their completed output.
        for (int i = 0; i < 3; ++i) { synthesize (controls, std::max (0.0, framePosition)); framePosition += step; shift (); }
    }

    std::pair<float, float> process (const SpectralControls& controls) noexcept
    {
        if (data == nullptr) return {};
        if (hopPosition == 0)
        {
            const auto [first, last] = bounds (controls);
            if (controls.freeze) framePosition = first + juce::jlimit (0.0, 1.0, finiteOr (controls.position, 0)) * (last - first);
            else if (framePosition > last)
            {
                if (controls.loop && last > first) framePosition = first + std::fmod (framePosition - first, last - first);
                else finished = true;
            }
            if (! finished) synthesize (controls, framePosition);
            if (! controls.freeze) framePosition += stepFor (controls);
        }
        const auto index = (size_t) hopPosition;
        const std::pair<float, float> out { accumulator[0][index], accumulator[data->channels == 2 ? 1 : 0][index] };
        if (++hopPosition == kSpectralHop) { shift (); hopPosition = 0; }
        return out;
    }

    // Silent once finished and the overlap tail has drained.
    bool idle () const noexcept { return data == nullptr || (finished && tailDrained ()); }
    const SpectralData* source () const noexcept { return data; }
    double position () const noexcept { return framePosition; }

private:
    static double finiteOr (double value, double fallback) noexcept { return std::isfinite (value) ? value : fallback; }
    std::pair<double, double> bounds (const SpectralControls& c) const noexcept
    {
        const double last = data->frames - 1.0;
        const double a = juce::jlimit (0.0, last, finiteOr (c.startFrame, 0)), b = juce::jlimit (0.0, last, finiteOr (c.endFrame, last));
        return { std::min (a, b), std::max (a, b) };
    }
    double stepFor (const SpectralControls& c) const noexcept { return juce::jlimit (0.0, 4.0, finiteOr (c.speed, 1)) * rateRatio; }
    bool tailDrained () const noexcept
    {
        for (const auto& channel : accumulator) for (float v : channel) if (v != 0) return false;
        return true;
    }
    void shift () noexcept
    {
        for (auto& channel : accumulator)
        {
            std::copy (channel.begin () + kSpectralHop, channel.end (), channel.begin ());
            std::fill (channel.end () - kSpectralHop, channel.end (), 0.0f);
        }
    }
    // Reference phase advance per analysis hop, from frame f - 1 to frame f, unwrapped around the bin centre.
    double advance (int f, int k) const noexcept
    {
        constexpr double expected = juce::MathConstants<double>::halfPi; // 2 pi H / N per bin
        if (f == 0) return expected * k;
        const double d = (double) data->referencePhases (f)[k] - data->referencePhases (f - 1)[k];
        return expected * k + principalArgument (d - expected * k);
    }
    bool isPeak (int k) const noexcept
    {
        const float m = combined[(size_t) k];
        if (m <= 1.0e-12f) return false;
        for (int n = k - 2; n <= k + 2; ++n)
            if (n != k && n >= 0 && n < kSpectralBins && (n < k ? combined[(size_t) n] >= m : combined[(size_t) n] > m)) return false;
        return true;
    }

    void synthesize (const SpectralControls& controls, double position) noexcept
    {
        const int i = juce::jlimit (0, data->frames - 1, (int) std::floor (position)), j = juce::jmin (i + 1, data->frames - 1);
        const float t = (float) juce::jlimit (0.0, 1.0, position - i);
        const int nearest = t < .5f ? i : j;
        const double alpha = juce::jlimit (1.0 / 64, 64.0, finiteOr (controls.pitch, 1) * rateRatio);
        const float* reference = data->referencePhases (nearest);
        const int channels = data->channels;
        combined.fill (0);
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* magI = data->magnitudes (ch, i);
            const float* magJ = data->magnitudes (ch, j);
            auto& m = magnitude[(size_t) ch];
            for (size_t k = 0; k < (size_t) kSpectralBins; ++k) { m[k] = magI[k] + t * (magJ[k] - magI[k]); combined[k] += m[k]; }
        }
        // Output bin k reads source bin k / alpha; advances are scaled by alpha (frequency ratio per output hop).
        for (int k = 0; k < kSpectralBins; ++k)
        {
            const double source = k / alpha;
            if (! initialised) { synthPhase[(size_t) k] = reference[juce::jmin (kSpectralBins - 1, (int) std::round (source))]; continue; }
            if (source > kSpectralBins - 1) continue;
            const int b = (int) source, b1 = juce::jmin (b + 1, kSpectralBins - 1);
            const double u = source - b;
            const double a0 = advance (i, b) + t * (advance (j, b) - advance (i, b));
            const double a1 = advance (i, b1) + t * (advance (j, b1) - advance (i, b1));
            synthPhase[(size_t) k] = principalArgument (synthPhase[(size_t) k] + alpha * (a0 + u * (a1 - a0)));
        }
        initialised = true;
        for (auto& s : spectrum) s.fill (0);
        // Peaks own the bins up to the midpoint with their neighbours; each region moves with its peak, and each
        // channel's bin keeps its analysed phase relative to the reference at the peak.
        auto nextPeak = [this] (int k) { while (k < kSpectralBins && ! isPeak (k)) ++k; return k; };
        for (int k = nextPeak (0), regionStart = 0; k < kSpectralBins;)
        {
            const int next = nextPeak (k + 1);
            const int regionEnd = next < kSpectralBins ? (k + next) / 2 : kSpectralBins - 1;
            // Shift by the peak's measured frequency (in bins), not its bin index, so the moved lobe sits within
            // half a bin of alpha * f; the shift is zero at unit pitch, keeping the identity exact.
            const double peakBins = (advance (i, k) + t * (advance (j, k) - advance (i, k))) / juce::MathConstants<double>::halfPi;
            const int shiftBins = (int) std::round ((alpha - 1) * peakBins), target = k + shiftBins;
            if (target >= 0 && target <= kSpectralBins - 1)
            {
                const double base = synthPhase[(size_t) target] - reference[k];
                for (int ch = 0; ch < channels; ++ch)
                {
                    const float* phase = data->phases (ch, nearest);
                    const auto& m = magnitude[(size_t) ch];
                    auto& out = spectrum[(size_t) ch];
                    for (int s = regionStart; s <= regionEnd; ++s)
                        if (const int d = s + shiftBins; d >= 0 && d < kSpectralBins)
                        {
                            const double ph = base + phase[s];
                            out[2 * (size_t) d] += m[(size_t) s] * (float) std::cos (ph);
                            out[2 * (size_t) d + 1] += m[(size_t) s] * (float) std::sin (ph);
                        }
                }
            }
            regionStart = regionEnd + 1;
            k = next;
        }
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& out = spectrum[(size_t) ch];
            out[1] = 0; out[2 * (kSpectralBins - 1) + 1] = 0;
            fft.performRealOnlyInverseTransform (out.data ());
            auto& acc = accumulator[(size_t) ch];
            for (size_t n = 0; n < (size_t) kSpectralFftSize; ++n) acc[n] += .5f * out[n] * spectralWindow[n];
        }
    }

    const SpectralData* data = nullptr;
    juce::dsp::FFT fft { kSpectralFftOrder };
    std::array<std::array<float, kSpectralFftSize>, 2> accumulator {};
    std::array<double, kSpectralBins> synthPhase {};
    std::array<std::array<float, kSpectralBins>, 2> magnitude {};
    std::array<float, kSpectralBins> combined {};
    std::array<std::array<float, 2 * kSpectralFftSize>, 2> spectrum {};
    double framePosition = 0, rateRatio = 1;
    int hopPosition = 0;
    bool initialised = false;
};
} // namespace bw
