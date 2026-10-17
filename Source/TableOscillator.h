#pragma once
#include "WavetableBank.h"

namespace bw
{
// Audio-thread-owned transition state; the bank must outlive every oscillator.
class TableOscillator
{
public:
    explicit TableOscillator (const WavetableBank& tables) noexcept : bank (tables) {}

    void prepare (double sampleRate) noexcept
    {
        const double rate = std::isfinite (sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0;
        fadeSamples = (int) std::ceil (juce::jlimit (1.0, 1000000.0, rate * 0.015));
        reset (requested);
    }

    // Use only for a new/silent voice or during prepare, not to change an active note.
    void reset (int index) noexcept { reset (&bank[index]); }
    void reset (const Wavetable* table) noexcept
    {
        current = target = requested = table;
        elapsed = 0;
        fading = false;
    }

    // Tables passed by pointer (user imports) must stay alive until any fade away from them ends.
    void setTable (int index) noexcept { setTable (&bank[index]); }
    void setTable (const Wavetable* table) noexcept
    {
        requested = table;
        if (! fading && requested != current)
            startFade();
    }

    float readCurrent (double phase, float position, int level) const noexcept
    {
        const float a = current->read (phase, position, level);
        if (! fading)
            return a;

        const float mix = (float) elapsed / (float) fadeSamples;
        return a + mix * (target->read (phase, position, level) - a);
    }

    void advance() noexcept
    {
        if (fading && ++elapsed >= fadeSamples)
        {
            current = target;
            fading = false;
            if (requested != current)
                startFade();
        }
    }

    float read (double phase, float position, int level) noexcept
    {
        const float result = readCurrent (phase, position, level);
        advance();
        return result;
    }

private:
    void startFade() noexcept
    {
        target = requested;
        elapsed = 0;
        fading = true;
    }

    const WavetableBank& bank;
    const Wavetable* current = &bank[0];
    const Wavetable* target = current;
    const Wavetable* requested = current;
    int elapsed = 0, fadeSamples = 662;
    bool fading = false;
};
} // namespace bw
