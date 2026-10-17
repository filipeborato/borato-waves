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
};

inline juce::StringArray distortionModes() { return { "Soft", "Hard", "Fold" }; }

// Waveshaper normalised so a full-scale input stays at full scale for every drive.
inline float shape (int mode, float x, float drive) noexcept
{
    if (mode == 1) return juce::jlimit (-1.0f, 1.0f, x * drive);
    if (mode == 2) return std::sin (x * drive * juce::MathConstants<float>::halfPi);
    return std::tanh (x * drive) / std::tanh (drive);
}

// Fixed order: distortion > delay > reverb. Each stage crossfades in and out over 20 ms;
// a fully bypassed stage does no work and leaves the signal untouched.
class Effects
{
public:
    static constexpr double kMaxDelaySeconds = 4.0;

    void prepare (double sampleRate, int maxBlock)
    {
        rate = sampleRate;
        blockLimit = juce::jmax (1, maxBlock);
        dry.setSize (2, blockLimit);
        oversampler.initProcessing ((size_t) blockLimit);
        delay.setMaximumDelayInSamples ((int) std::ceil (sampleRate * kMaxDelaySeconds) + 1);
        delay.prepare ({ sampleRate, (juce::uint32) blockLimit, 2 });
        reverb.setSampleRate (sampleRate);
        for (auto* s : { &distortion, &echo, &space }) { s->active.reset (sampleRate, 0.02); s->active.setCurrentAndTargetValue (0); s->running = false; }
        drive.reset (sampleRate, 0.02); distMix.reset (sampleRate, 0.02);
        delaySamples.reset (sampleRate, 0.08); feedback.reset (sampleRate, 0.02); delayMix.reset (sampleRate, 0.02);
        reset();
    }

    void reset()
    {
        oversampler.reset();
        delay.reset();
        reverb.reset();
    }

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
            stage (echo, p.delayOn, block, [&] { echoes (block, p.delayPingPong->load() >= .5f); }, [this] { delay.reset(); });
            stage (space, p.reverbOn, block, [&]
            {
                if (channels == 1) reverb.processMono (block.getChannelPointer (0), count);
                else reverb.processStereo (block.getChannelPointer (0), block.getChannelPointer (1), count);
            }, [this] { reverb.reset(); });
        }
    }

private:
    struct Stage { juce::SmoothedValue<float> active; bool running = false; };

    template <typename Process, typename Clear>
    void stage (Stage& s, std::atomic<float>* enabled, juce::dsp::AudioBlock<float>& block, Process&& process, Clear&& clear)
    {
        s.active.setTargetValue (enabled->load() >= .5f ? 1.0f : 0.0f);
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

    double rate = 44100;
    int blockLimit = 512;
    juce::AudioBuffer<float> dry;
    Stage distortion, echo, space;
    juce::dsp::Oversampling<float> oversampler { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    juce::SmoothedValue<float> drive, distMix, feedback, delayMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delaySamples;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay;
    juce::Reverb reverb;
};
} // namespace bw
