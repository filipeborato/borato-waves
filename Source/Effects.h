#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Modulation.h"

namespace bw
{
struct EffectParams
{
    std::atomic<float> *distOn, *distMode, *distDrive, *distMix;
    std::atomic<float> *delayOn, *delayTime, *delaySync, *delayDivision, *delayFeedback, *delayPingPong, *delayMix;
    std::atomic<float> *reverbOn, *reverbSize, *reverbDamping, *reverbWidth, *reverbMix;
    std::atomic<float> *chorusOn, *chorusRate, *chorusDepth, *chorusMix;
    std::atomic<float> *eqOn, *eqLow, *eqFreq, *eqGain, *eqQ, *eqHigh;
    std::atomic<float> *compOn, *compThreshold, *compRatio, *compAttack, *compRelease, *compMakeup;
};

inline juce::StringArray distortionModes() { return { "Soft", "Hard", "Fold" }; }

// Waveshaper normalised so a full-scale input stays at full scale for every drive.
inline float shape (int mode, float x, float drive) noexcept
{
    if (mode == 1) return juce::jlimit (-1.0f, 1.0f, x * drive);
    if (mode == 2) return std::sin (x * drive * juce::MathConstants<float>::halfPi);
    return std::tanh (x * drive) / std::tanh (drive);
}

// Trapezoidal state-variable band (Simper): stays stable and click-free while its settings move.
struct EqBand
{
    enum Type { lowShelf, bell, highShelf };
    static constexpr double kShelfQ = 0.7071, kMaxFraction = 0.49; // of the sample rate
    double g = 0, k = 1, a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;

    void set (Type type, double hz, double q, double db, double rate)
    {
        const double a = std::pow (10.0, db / 40), w = std::tan (juce::MathConstants<double>::pi * juce::jmin (hz, kMaxFraction * rate) / rate);
        if (type == bell) { g = w; k = 1 / (q * a); m0 = 1; m1 = k * (a * a - 1); m2 = 0; }
        else if (type == lowShelf) { g = w / std::sqrt (a); k = 1 / q; m0 = 1; m1 = k * (a - 1); m2 = a * a - 1; }
        else { g = w * std::sqrt (a); k = 1 / q; m0 = a * a; m1 = k * (1 - a) * a; m2 = 1 - a * a; }
        a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    float process (double& ic1, double& ic2, float x) const noexcept
    {
        const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        return (float) (m0 * x + m1 * v1 + m2 * v2);
    }
    // Exact magnitude at hz: the analog prototype at the prewarped frequency.
    double magnitude (double hz, double rate) const
    {
        const std::complex<double> s (0, std::tan (juce::MathConstants<double>::pi * juce::jmin (hz, kMaxFraction * rate) / rate) / g);
        return std::abs (m0 + (m1 * s + m2) / (s * s + k * s + 1.0));
    }
};

// Low shelf at kLowHz, a bell, a high shelf at kHighHz; gains in dB.
struct EqSettings
{
    static constexpr double kLowHz = 200, kHighHz = 5000;
    float low = 0, freq = 1000, gain = 0, q = 1, high = 0;

    std::array<EqBand, 3> bands (double rate) const
    {
        std::array<EqBand, 3> b;
        b[0].set (EqBand::lowShelf, kLowHz, EqBand::kShelfQ, low, rate);
        b[1].set (EqBand::bell, freq, q, gain, rate);
        b[2].set (EqBand::highShelf, kHighHz, EqBand::kShelfQ, high, rate);
        return b;
    }
    double magnitude (double hz, double rate) const
    {
        double m = 1;
        for (const auto& band : bands (rate)) m *= band.magnitude (hz, rate);
        return m;
    }
};

// Static compressor curve: gain change in dB (<= 0) for a level in dB, with a 6 dB soft knee.
inline float compressorGain (float levelDb, float thresholdDb, float ratio) noexcept
{
    constexpr float kKnee = 6;
    const float over = levelDb - thresholdDb, slope = 1 / ratio - 1;
    if (2 * over < -kKnee) return 0;
    if (2 * over <= kKnee) return slope * (over + kKnee / 2) * (over + kKnee / 2) / (2 * kKnee);
    return slope * over;
}

// Fixed order: distortion > chorus > delay > reverb > EQ > compressor. Each stage crossfades in and out over 20 ms;
// a fully bypassed stage does no work and leaves the signal untouched.
class Effects
{
public:
    static constexpr double kMaxDelaySeconds = 4.0;
    // Chorus taps sweep kChorusCentre +/- depth * kChorusSweep seconds.
    static constexpr double kChorusCentre = 0.010, kChorusSweep = 0.004;

    void prepare (double sampleRate, int maxBlock)
    {
        rate = sampleRate;
        blockLimit = juce::jmax (1, maxBlock);
        dry.setSize (2, blockLimit);
        oversampler.initProcessing ((size_t) blockLimit);
        delay.setMaximumDelayInSamples ((int) std::ceil (sampleRate * kMaxDelaySeconds) + 1);
        delay.prepare ({ sampleRate, (juce::uint32) blockLimit, 2 });
        reverb.setSampleRate (sampleRate);
        chorusLine.setMaximumDelayInSamples ((int) std::ceil (sampleRate * (kChorusCentre + kChorusSweep)) + 4);
        chorusLine.prepare ({ sampleRate, (juce::uint32) blockLimit, 2 });
        for (auto* s : { &distortion, &chorus, &echo, &space, &tone, &dynamics }) { s->active.reset (sampleRate, 0.02); s->active.setCurrentAndTargetValue (0); s->running = false; }
        drive.reset (sampleRate, 0.02); distMix.reset (sampleRate, 0.02);
        delaySamples.reset (sampleRate, 0.08); feedback.reset (sampleRate, 0.02); delayMix.reset (sampleRate, 0.02);
        chorusDepth.reset (sampleRate, 0.05); chorusMix.reset (sampleRate, 0.02);
        for (auto* s : { &eqLow, &eqFreq, &eqGain, &eqQ, &eqHigh }) s->reset (sampleRate, 0.03);
        makeup.reset (sampleRate, 0.02);
        primed = false;
        reset();
    }

    void reset()
    {
        oversampler.reset();
        delay.reset();
        reverb.reset();
        chorusLine.reset();
        chorusPhase = 0;
        eqState = {};
        envelopeDb = 0;
        gainReduction.store (0, std::memory_order_relaxed);
    }

    // Latest compressor gain change in dB (<= 0), for the meter.
    std::atomic<float> gainReduction { 0 };

    // offsets: normalised modulation for drive, delay mix and reverb mix.
    void process (juce::AudioBuffer<float>& buffer, const EffectParams& p, double bpm, std::array<float, 3> offsets = {})
    {
        const auto offset = [&offsets] (int i, float value, float lo, float hi)
        { return offsets[(size_t) i] == 0 ? value : juce::jlimit (lo, hi, value + offsets[(size_t) i] * (hi - lo)); };
        const int channels = juce::jmin (2, buffer.getNumChannels());
        const double seconds = p.delaySync->load() >= .5f
            ? divisionBeats (juce::roundToInt (p.delayDivision->load())) * 60 / juce::jmax (1.0, bpm)
            : p.delayTime->load() * 0.001;
        drive.setTargetValue (juce::Decibels::decibelsToGain (offset (0, p.distDrive->load(), 0, 36)));
        distMix.setTargetValue (p.distMix->load());
        delaySamples.setTargetValue ((float) juce::jlimit (1.0, rate * kMaxDelaySeconds, seconds * rate));
        feedback.setTargetValue (juce::jlimit (0.0f, 0.95f, p.delayFeedback->load()));
        delayMix.setTargetValue (offset (1, p.delayMix->load(), 0, 1));
        chorusDepth.setTargetValue (p.chorusDepth->load());
        chorusMix.setTargetValue (p.chorusMix->load());
        chorusIncrement = p.chorusRate->load() / rate;
        // Frequency and Q smooth in octaves so sweeps move evenly.
        eqLow.setTargetValue (p.eqLow->load()); eqGain.setTargetValue (p.eqGain->load()); eqHigh.setTargetValue (p.eqHigh->load());
        eqFreq.setTargetValue (std::log2 (p.eqFreq->load())); eqQ.setTargetValue (std::log2 (p.eqQ->load()));
        threshold = p.compThreshold->load(); ratio = juce::jmax (1.0f, p.compRatio->load());
        attackCoeff = std::exp (-1 / (0.001 * juce::jmax (0.01f, p.compAttack->load()) * rate));
        releaseCoeff = std::exp (-1 / (0.001 * juce::jmax (1.0f, p.compRelease->load()) * rate));
        makeup.setTargetValue (juce::Decibels::decibelsToGain (p.compMakeup->load()));
        // The first block after prepare starts at the settings instead of gliding up from zero.
        if (! primed)
            for (auto* s : { &drive, &distMix, &feedback, &delayMix, &chorusDepth, &chorusMix, &makeup, &eqLow, &eqFreq, &eqGain, &eqQ, &eqHigh, &delaySamples })
                s->setCurrentAndTargetValue (s->getTargetValue());
        const float mix = offset (2, p.reverbMix->load(), 0, 1);
        juce::Reverb::Parameters settings;
        settings.roomSize = p.reverbSize->load(); settings.damping = p.reverbDamping->load();
        settings.width = p.reverbWidth->load(); settings.wetLevel = mix;
        settings.dryLevel = 0.5f * (1 - mix); // juce::Reverb doubles the dry level internally
        reverb.setParameters (settings);

        for (int start = 0; start < buffer.getNumSamples(); start += blockLimit)
        {
            const int count = juce::jmin (blockLimit, buffer.getNumSamples() - start);
            juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) channels, (size_t) start, (size_t) count);
            stage (distortion, p.distOn, block, [&] { distort (block, juce::roundToInt (p.distMode->load())); }, [this] { oversampler.reset(); });
            stage (chorus, p.chorusOn, block, [&] { ensemble (block); }, [this] { chorusLine.reset(); });
            stage (echo, p.delayOn, block, [&] { echoes (block, p.delayPingPong->load() >= .5f); }, [this] { delay.reset(); });
            stage (space, p.reverbOn, block, [&]
            {
                if (channels == 1) reverb.processMono (block.getChannelPointer (0), count);
                else reverb.processStereo (block.getChannelPointer (0), block.getChannelPointer (1), count);
            }, [this] { reverb.reset(); });
            stage (tone, p.eqOn, block, [&] { equalise (block); }, [this] { eqState = {}; });
            stage (dynamics, p.compOn, block, [&] { compress (block); },
                   [this] { envelopeDb = 0; gainReduction.store (0, std::memory_order_relaxed); });
        }
        primed = true;
    }

private:
    struct Stage { juce::SmoothedValue<float> active; bool running = false; };

    template <typename Process, typename Clear>
    void stage (Stage& s, std::atomic<float>* enabled, juce::dsp::AudioBlock<float>& block, Process&& process, Clear&& clear)
    {
        const float on = enabled->load() >= .5f ? 1.0f : 0.0f;
        if (primed) s.active.setTargetValue (on); else s.active.setCurrentAndTargetValue (on); // no fade-in at start
        if (! s.running && s.active.getTargetValue() == 0 && ! s.active.isSmoothing()) return;
        s.running = true;
        const int count = (int) block.getNumSamples(), channels = (int) block.getNumChannels();
        const bool fading = s.active.isSmoothing() || s.active.getCurrentValue() < 1;
        if (fading)
            for (int ch = 0; ch < channels; ++ch) dry.copyFrom (ch, 0, block.getChannelPointer ((size_t) ch), count);
        process();
        if (! fading) return;
        for (int i = 0; i < count; ++i)
        {
            const float a = s.active.getNextValue();
            for (int ch = 0; ch < channels; ++ch)
            {
                auto* out = block.getChannelPointer ((size_t) ch);
                out[i] = dry.getSample (ch, i) + a * (out[i] - dry.getSample (ch, i));
            }
        }
        if (s.active.getCurrentValue() == 0 && s.active.getTargetValue() == 0) { s.running = false; clear(); }
    }

    void distort (juce::dsp::AudioBlock<float>& block, int mode)
    {
        // Dry and wet are mixed at the oversampled rate so both share the resampling filters' phase.
        auto up = oversampler.processSamplesUp (block);
        float gain = 1, wet = 0;
        for (size_t n = 0; n < up.getNumSamples(); ++n)
        {
            if ((n & 1) == 0) { gain = drive.getNextValue(); wet = distMix.getNextValue(); }
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                const float x = up.getSample ((int) ch, (int) n);
                up.setSample ((int) ch, (int) n, x + wet * (shape (mode, x, gain) - x));
            }
        }
        oversampler.processSamplesDown (block);
    }

    void echoes (juce::dsp::AudioBlock<float>& block, bool pingPong)
    {
        const bool stereo = block.getNumChannels() > 1;
        auto* left = block.getChannelPointer (0);
        auto* right = stereo ? block.getChannelPointer (1) : left;
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const float time = delaySamples.getNextValue(), fb = feedback.getNextValue(), wet = delayMix.getNextValue();
            const float dl = delay.popSample (0, time), dr = stereo ? delay.popSample (1, time) : dl;
            const float inL = left[i], inR = right[i];
            if (stereo && pingPong) { delay.pushSample (0, 0.5f * (inL + inR) + fb * dr); delay.pushSample (1, fb * dl); }
            else { delay.pushSample (0, inL + fb * dl); if (stereo) delay.pushSample (1, inR + fb * dr); }
            left[i] = inL + wet * (dl - inL);
            if (stereo) right[i] = inR + wet * (dr - inR);
        }
    }

    // Two taps per side, half a sweep apart; the right side runs a quarter sweep behind the left.
    void ensemble (juce::dsp::AudioBlock<float>& block)
    {
        const int channels = (int) block.getNumChannels();
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const float sweep = chorusDepth.getNextValue() * (float) (kChorusSweep * rate), wet = chorusMix.getNextValue();
            for (int ch = 0; ch < channels; ++ch)
            {
                auto* io = block.getChannelPointer ((size_t) ch);
                chorusLine.pushSample (ch, io[i]);
                float taps = 0;
                for (int tap = 0; tap < 2; ++tap)
                {
                    const double phase = chorusPhase + 0.5 * tap + 0.25 * ch;
                    const float lfo = (float) std::sin (juce::MathConstants<double>::twoPi * phase);
                    taps += chorusLine.popSample (ch, (float) (kChorusCentre * rate) + sweep * lfo, tap == 1);
                }
                // Uncorrelated taps sum to the input's power; correlated ones (low notes) to +3 dB.
                io[i] += wet * (juce::MathConstants<float>::sqrt2 * 0.5f * taps - io[i]);
            }
            chorusPhase += chorusIncrement;
            chorusPhase -= std::floor (chorusPhase);
        }
    }

    // Coefficients follow the smoothed settings every sample while they move: high shelf gains turn
    // coarser updates into audible zipper steps. Settled settings are computed once per block.
    void equalise (juce::dsp::AudioBlock<float>& block)
    {
        const int channels = (int) block.getNumChannels(), count = (int) block.getNumSamples();
        for (int start = 0, n = 0; start < count; start += n)
        {
            const bool moving = eqLow.isSmoothing() || eqFreq.isSmoothing() || eqGain.isSmoothing() || eqQ.isSmoothing() || eqHigh.isSmoothing();
            n = moving ? 1 : count - start;
            for (auto* s : { &eqLow, &eqFreq, &eqGain, &eqQ, &eqHigh }) s->skip (n);
            const EqSettings settings { eqLow.getCurrentValue(), std::exp2 (eqFreq.getCurrentValue()), eqGain.getCurrentValue(),
                                        std::exp2 (eqQ.getCurrentValue()), eqHigh.getCurrentValue() };
            const auto bands = settings.bands (rate);
            for (int ch = 0; ch < channels; ++ch)
            {
                auto* io = block.getChannelPointer ((size_t) ch) + start;
                for (size_t b = 0; b < bands.size(); ++b)
                {
                    auto& state = eqState[(size_t) ch][b];
                    for (int i = 0; i < n; ++i) io[i] = bands[b].process (state[0], state[1], io[i]);
                }
            }
        }
    }

    // Stereo-linked peak detector. The gain only ever comes down, so MAKEUP is the only way up.
    void compress (juce::dsp::AudioBlock<float>& block)
    {
        const int channels = (int) block.getNumChannels();
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            float peak = 0;
            for (int ch = 0; ch < channels; ++ch) peak = juce::jmax (peak, std::abs (block.getSample (ch, (int) i)));
            const float target = compressorGain (juce::Decibels::gainToDecibels (peak, -120.0f), threshold, ratio);
            envelopeDb = target + (target < envelopeDb ? attackCoeff : releaseCoeff) * (envelopeDb - target);
            const float g = juce::Decibels::decibelsToGain ((float) envelopeDb) * makeup.getNextValue();
            for (int ch = 0; ch < channels; ++ch) block.getChannelPointer ((size_t) ch)[i] *= g;
        }
        gainReduction.store ((float) envelopeDb, std::memory_order_relaxed);
    }

    double rate = 44100, chorusPhase = 0, chorusIncrement = 0;
    double attackCoeff = 0, releaseCoeff = 0, envelopeDb = 0;
    float threshold = 0, ratio = 1;
    std::array<std::array<std::array<double, 2>, 3>, 2> eqState {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> eqLow, eqFreq, eqGain, eqQ, eqHigh;
    int blockLimit = 512;
    bool primed = false;
    juce::AudioBuffer<float> dry;
    Stage distortion, chorus, echo, space, tone, dynamics;
    juce::dsp::Oversampling<float> oversampler { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    juce::SmoothedValue<float> drive, distMix, feedback, delayMix, chorusDepth, chorusMix, makeup;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delaySamples;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> chorusLine;
    juce::Reverb reverb;
};
} // namespace bw
