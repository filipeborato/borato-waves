# Dual oscillators and source mixer

OSC A and B share the immutable eight-table bank but own their phase, table transition, position, pitch and gain. Each offers enable, coarse tuning (-24 to +24 semitones), fine tuning (-100 to +100 cents) and stereo pan. The browser targets the oscillator that opened it; waveform dragging, 2D/3D/FFT and host automation work independently.

The sub is a sine source at octave -2, -1 or 0. Noise is per-voice white noise from a local xorshift generator. Both have enable and level controls. MIDI pitch bend spans +/-2 semitones for A, B and sub, including the wheel position at note-on.

## Signal and lifecycle

`A + B + sub + noise -> stereo low-pass -> ADSR x velocity -> master`

The mixer uses equal-power pan scaled to unity per channel at centre, preserving the former A level. Mono averages left/right. Sub and noise are centred. Summing is additive; master level controls headroom, and there is no limiter. Factory patches now demonstrate B layering, detuned stereo, sub and subtle noise; Init resets all source parameters.

Enable/level, position, tuning, pan, resonance and bend use 20 ms ramps. Tables retain 15 ms phase-preserving transitions. Stable pitch/pan coefficients are cached. Filters and table readers are prepared before rendering; new source rendering adds no allocation, locking, file access or GUI calls. JUCE's existing synthesiser/keyboard synchronization remains in the surrounding engine.

Each source selects bandwidth from its actual frequency. Fundamentals fade over the last 5% below Nyquist and are silent above it. A valid transposed sub can still play when A/B are outside the representable band. Phase wraps remain bounded. Discrete mip transitions and rapid frame modulation still warrant future spectral-quality work; this is not an alias-free modulation claim.

## Persistence and validation

Existing IDs and ordering are preserved; 16 source parameters are appended. State loading fills missing IDs from declared defaults, so legacy sessions restore A on and B/sub/noise off. Factory matching includes every new source parameter.

`test_sources` checks A/B isolation and equivalence, stereo extremes, coarse/fine tuning, bend, additive mixing, sub frequency, noise DC, Nyquist behavior, automation at 8–192 kHz and state migration. `test_interface` checks independent B browser/drag gestures, enable controls, patch reset, restored labels and actual editor rendering. Run Debug assertions alongside `test_wavetable`.

The implementation is original. [Fresh Ghidra findings](OSCILLATOR_RESEARCH.md) informed architecture, not exact Serum algorithms. Hardware listening and DAW automation remain manual validation.

![Dual sources](images/dual-sources.png)

A/B now also provide eight-lane unison, detune, spread, phase/randomness and matrix destinations; see [MODULATION.md](MODULATION.md) for current modulation and lane behavior.
