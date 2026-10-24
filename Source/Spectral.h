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
    std::vector<float> spectralFlux;
    std::vector<int> transientSamples; // source positions shared by playback and display

    static int frameCount (int sourceFrames) noexcept { return (sourceFrames + kSpectralPad - 1) / kSpectralHop + 1; }
    static size_t estimateBytes (int sourceFrames, int channels) noexcept
    { return (size_t) frameCount (sourceFrames) * ((size_t) (channels == 2 ? 5 : 2) * kSpectralBins * sizeof (float) + sizeof (float) + sizeof (int)); }
    size_t bytes () const noexcept { return (magnitude.size () + phase.size () + reference.size () + spectralFlux.size ()) * sizeof (float) + transientSamples.capacity () * sizeof (int); }
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
    data->spectralFlux.resize ((size_t) data->frames);
    data->transientSamples.reserve ((size_t) data->frames);
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
    for (int m = 1; m < data->frames; ++m)
    {
        if ((m & 63) == 0 && shouldCancel && shouldCancel ()) { error = "Spectral analysis cancelled"; return {}; }
        double rise = 0, total = 0;
        for (int ch = 0; ch < data->channels; ++ch)
            for (int k = 1; k < kSpectralBins - 1; ++k)
            {
                const double current = data->magnitudes (ch, m)[k];
                rise += std::max (0.0, current - data->magnitudes (ch, m-1)[k]);
                total += current;
            }
        data->spectralFlux[(size_t) m] = total > 1e-8 ? (float) (rise / total) : 0;
    }
    for (int m = 1; m < data->frames - 1; ++m)
    {
        const float flux = data->spectralFlux[(size_t) m];
        double mean = 0; const int start = std::max (0, m - 8);
        for (int f = start; f < m; ++f) mean += data->spectralFlux[(size_t) f];
        if (flux < std::max (.05, .5 * mean / (m - start))) continue;
        const int first = std::max (0, m*kSpectralHop-kSpectralPad), last = std::min (sample.numFrames (), m*kSpectralHop-kSpectralPad+kSpectralFftSize);
        auto riseAt = [&] (int n)
        {
            if (n < 0 || n >= sample.numFrames ()) return 0.0;
            double rise = std::abs (sample.data[(size_t) n]) - (n > 0 ? std::abs (sample.data[(size_t) n-1]) : 0);
            if (data->channels == 2) rise += std::abs (sample.right[(size_t) n]) - (n > 0 ? std::abs (sample.right[(size_t) n-1]) : 0);
            return rise;
        };
        double largestRise = 0;
        for (int n = first; n < last; ++n) largestRise = std::max (largestRise, riseAt (n));
        // Flux gates the region; local rises locate every attack, including two attacks in one FFT window.
        const double refractory = std::max ((double) kSpectralHop, sample.rate*.005);
        for (int n = first; n < last; ++n)
        {
            const double rise = riseAt (n);
            if (rise <= std::max (1e-6, largestRise*.5) || rise <= riseAt (n-1) || rise < riseAt (n+1)) continue;
            if (data->transientSamples.empty () || n-data->transientSamples.back () >= refractory) data->transientSamples.push_back (n);
        }
    }
    if (shouldCancel && shouldCancel ()) { error = "Spectral analysis cancelled"; return {}; }
    if (onProgress) onProgress (1);
    return data;
}

enum class SpectralStageType { off, binShift, tilt };
inline juce::StringArray spectralStageNames () { return { "Off", "Bin Shift", "Spectral Tilt" }; }
struct SpectralStage
{
    SpectralStageType type = SpectralStageType::off;
    float amount = 0; // -1..1: +/-64 bins or +/-12 dB/octave
    bool bypass = false;
};
using SpectralBuffer = std::array<float, 2 * kSpectralFftSize>;
// Complex bin translation includes the inter-hop phase advance of the frequency offset.
inline void applySpectralStage (SpectralBuffer& bins, SpectralBuffer& scratch, const SpectralStage& stage,
                                double binHz, double shiftPhase) noexcept
{
    const double a = std::isfinite (stage.amount) ? juce::jlimit (-1.0, 1.0, (double) stage.amount) : 0;
    if (stage.bypass || stage.type == SpectralStageType::off || (a == 0 && shiftPhase == 0)) return;
    if (stage.type == SpectralStageType::tilt)
    {
        for (int k = 1; k < kSpectralBins - 1; ++k)
        {
            const float gain = (float) std::pow (10.0, juce::jlimit (-36.0, 24.0, 12*a*std::log2 (k*binHz/1000)) / 20);
            bins[2*(size_t) k] *= gain; bins[2*(size_t) k+1] *= gain;
        }
        return;
    }
    scratch = bins;
    const double shift = a * 64, cs = std::cos (shiftPhase), sn = std::sin (shiftPhase);
    for (int k = 1; k < kSpectralBins - 1; ++k)
    {
        const double source = k - shift; const int lo = (int) std::floor (source); const double t = source-lo;
        double re = 0, im = 0;
        for (int j = 0; j < 2; ++j)
            if (const int bin = lo+j; bin >= 1 && bin < kSpectralBins-1)
            {
                const double weight = j == 0 ? 1-t : t;
                re += weight*scratch[2*(size_t) bin]; im += weight*scratch[2*(size_t) bin+1];
            }
        bins[2*(size_t) k] = (float) (re*cs-im*sn); bins[2*(size_t) k+1] = (float) (re*sn+im*cs);
    }
}

struct SpectralControls
{
    double startFrame = 0, endFrame = 0; // region, in frames (SpectralData::frameFor)
    double position = 0;                 // 0..1 within the region: note-on start, and the held frame in freeze
    double speed = 1;                    // analysis frames per synthesis hop at equal rates
    double pitch = 1;                    // frequency ratio, including key tracking
    bool freeze = false, loop = false;
    bool phaseLock = true, transientReset = false;
    std::array<SpectralStage, 2> stages {};
    bool reverseOrder = false;
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
        stageCurrent = stagePrevious = controls; stageTransition = 1; stagePhases = {};
        for (size_t k = 0; k < 2; ++k) { stageAmounts[k] = controls.stages[k].amount; stageWet[k] = controls.stages[k].amount == 0 ? 0 : 1; }
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
        const double origin = position*kSpectralHop-kSpectralPad;
        const double analysisOrigin = nearest*kSpectralHop-kSpectralPad;
        const auto firstAttack = std::lower_bound (data->transientSamples.begin (), data->transientSamples.end (), analysisOrigin);
        const auto lastAttack = std::lower_bound (firstAttack, data->transientSamples.end (), analysisOrigin+kSpectralFftSize);
        const bool alignedTransient = controls.transientReset && ! controls.freeze && std::abs (controls.speed-1) > 1e-9 && firstAttack != lastAttack;
        if (alignedTransient)
            for (int k = 0; k < kSpectralBins; ++k)
                synthPhase[(size_t) k] = reference[juce::jlimit (0, kSpectralBins-1, (int) std::round (k/alpha))];
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
                            const double ph = controls.phaseLock ? base + phase[s] : synthPhase[(size_t) d] + phase[s] - reference[s];
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
            if (alignedTransient)
            {
                // Reconstruct the attack before pitch mapping, then place each time support independently.
                // This keeps attack timing independent of both pitch and the source/host sample-rate ratio.
                const auto* analysisPhase = data->phases (ch, nearest);
                const double cutoff = (kSpectralBins-1)/std::max (1.0, alpha);
                out.fill (0);
                for (int k = 0; k < kSpectralBins; ++k)
                {
                    const float antiAlias = alpha <= 1 ? 1 : (float) juce::jlimit (0.0, 1.0, (cutoff-k)/8);
                    const float mag = magnitude[(size_t) ch][(size_t) k]*antiAlias;
                    out[2*(size_t) k] = mag*std::cos (analysisPhase[k]); out[2*(size_t) k+1] = mag*std::sin (analysisPhase[k]);
                }
                out[1] = 0; out[2*(kSpectralBins-1)+1] = 0;
                fft.performRealOnlyInverseTransform (out.data ());
                stageScratch = out; out.fill (0);
                const double step = std::max (.01, stepFor (controls));
                for (auto attack = firstAttack; attack != lastAttack; ++attack)
                {
                    const double from = attack == firstAttack ? 0 : .5*(*(attack-1)+*attack)-analysisOrigin;
                    const double to = attack+1 == lastAttack ? kSpectralFftSize : .5*(*attack+*(attack+1))-analysisOrigin;
                    const double target = (*attack-origin)/step, source = *attack-analysisOrigin;
                    // shortcut: gain assumes interior windows; account for region/loop edges when matching boundary levels.
                    double weight = 0;
                    auto windowAt = [] (double at)
                    {
                        if (at < 0 || at >= kSpectralFftSize-1) return 0.0;
                        const auto index = (size_t) at; const double fraction = at-index;
                        return spectralWindow[index]+fraction*(spectralWindow[index+1]-spectralWindow[index]);
                    };
                    for (int hop = -4; hop <= 4; ++hop)
                    {
                        const double frame = std::max (0.0, position+hop*step), lower = std::floor (frame), fraction = frame-lower;
                        const double at = *attack-(lower*kSpectralHop-kSpectralPad);
                        const double analysisWeight = (1-fraction)*windowAt (at)+fraction*windowAt (at-kSpectralHop);
                        weight += .5*analysisWeight*windowAt (target-hop*kSpectralHop);
                    }
                    const float gain = (float) (1/std::max (.125,weight));

                    for (int n = 0; n < kSpectralFftSize; ++n)
                    {
                        const double read = source+(n-target)*alpha;
                        if (read < from || read >= to || read < 0 || read >= kSpectralFftSize-1) continue;
                        const auto lo = (size_t) read; const float fraction = (float) (read-lo);
                        out[(size_t) n] += gain*(stageScratch[lo]+fraction*(stageScratch[lo+1]-stageScratch[lo]));
                    }
                }
                fft.performRealOnlyForwardTransform (out.data (), true);
            }
            transformStages (out, controls, ch == 0);
            out[1] = 0; out[2 * (kSpectralBins - 1) + 1] = 0;
            fft.performRealOnlyInverseTransform (out.data ());
            auto& acc = accumulator[(size_t) ch];
            for (size_t n = 0; n < (size_t) kSpectralFftSize; ++n) acc[n] += .5f*out[n]*spectralWindow[n];
        }
    }

    const SpectralData* data = nullptr;
    void transformStages (SpectralBuffer& out, const SpectralControls& controls, bool update) noexcept
    {
        const double step = std::min (1.0, kSpectralHop * rateRatio / (data->rate * .02));
        if (update)
        {
            auto same = [] (const SpectralControls& a, const SpectralControls& b)
            {
                return a.reverseOrder == b.reverseOrder && a.stages[0].type == b.stages[0].type && a.stages[1].type == b.stages[1].type
                    && a.stages[0].bypass == b.stages[0].bypass && a.stages[1].bypass == b.stages[1].bypass;
            };
            if (stageTransition >= 1 && ! same (controls, stageCurrent)) { stagePrevious = stageCurrent; stageCurrent = controls; stageTransition = 0; }
            stageTransition = std::min (1.0, stageTransition + step);
            for (size_t k = 0; k < 2; ++k)
            {
                const double target = std::isfinite (controls.stages[k].amount) ? juce::jlimit (-1.0, 1.0, (double) controls.stages[k].amount) : 0;
                stageAmounts[k] += juce::jlimit (-step, step, target-stageAmounts[k]);
                stageWet[k] += juce::jlimit (-step, step, (target == 0 ? 0.0 : 1.0) - stageWet[k]);
                stagePhases[k] = stageWet[k] == 0 ? 0 : principalArgument (stagePhases[k] + stageAmounts[k]*64*juce::MathConstants<double>::halfPi);
            }
        }
        auto apply = [&] (SpectralBuffer& bins, const SpectralControls& c)
        {
            for (int i = 0; i < 2; ++i)
            {
                const size_t index = (size_t) (c.reverseOrder ? 1-i : i);
                auto stage = c.stages[index]; stage.amount = (float) stageAmounts[index];
                if (stageWet[index] == 0) continue;
                stageDry = bins;
                applySpectralStage (bins, stageScratch, stage, data->rate / (rateRatio*kSpectralFftSize), stagePhases[index]);
                if (stageWet[index] < 1)
                    for (size_t k = 0; k < bins.size (); ++k) bins[k] = stageDry[k] + (float) stageWet[index]*(bins[k]-stageDry[k]);
            }
        };
        if (stageTransition < 1)
        {
            stageOld = out; apply (stageOld, stagePrevious); apply (out, stageCurrent);
            for (size_t k = 0; k < out.size (); ++k) out[k] = stageOld[k] + (float) stageTransition * (out[k]-stageOld[k]);
        }
        else apply (out, stageCurrent);
    }
    SpectralControls stageCurrent, stagePrevious;
    double stageTransition = 1;
    std::array<double, 2> stageAmounts {}, stagePhases {}, stageWet {};
    SpectralBuffer stageScratch {}, stageOld {}, stageDry {};
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
