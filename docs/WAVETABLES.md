# Wavetable engine and browser

## Implemented behaviour

OSC A now has an independent, automatable `wtTable` choice and continuous `wtPos` frame position. Click its table name to open the custom library, or use the previous/next table buttons. The library filters names, categories and descriptions; typing multiple words requires all words to match. Enter loads the first result. Category buttons narrow the results. Hover or focus a row to inspect its waveform; selection loads it and returns to the synth.

Drag horizontally in the waveform to scan frames. Double-click resets position. 2D and 3D show the same immutable table data used by the oscillator. FFT shows harmonics 1–64 of the current interpolated full-band frame, with a -60 dB floor; it is a source-table display, not an output analyser.

| Table | Character of the frame scan |
|---|---|
| Basic Shapes | Original sine → saw → square morph; legacy sound preserved |
| Sine Triangle | Adds alternating odd harmonics to a sine |
| Saw Harmonics | Progressively introduces higher saw harmonics |
| Odd Harmonics | Progressively introduces odd square-wave harmonics |
| Pulse Sweep | Pulse width from 50% to 8% |
| Formant Sweep | A harmonic resonance moves over a fundamental |
| Spectral Bells | Moving integer-harmonic clusters; no inharmonic oscillator |
| Phase Bend | Increasing periodic phase distortion, baked into frames |

Every table contains 16 original procedural frames of 2048 samples with 11 FFT band-limit levels. None uses Serum factory samples or decompiled implementation code. The five factory patches now choose suitable tables; existing host sessions retain their parameter values and original Basic Shapes sound when `wtTable` is absent.

## DSP and state

`WavetableBank` constructs all tables before playback. Voices share the immutable bank, whose lifetime exceeds theirs. `TableOscillator` owns per-voice transition state and reads at most two tables per sample. A linear 15 ms crossfade preserves oscillator phase during a bank change. Rapid changes retain the latest pending choice and finish the current fade before starting another, reaching a settled final choice within approximately 30 ms plus the parameter polling interval. Frame position keeps the existing 20 ms smoothing.

Bank generation, FFT displays, text filtering and browser allocations run outside the audio callback. The new sample path has no locks, allocation, I/O or table destruction. Existing JUCE synthesizer/keyboard synchronization remains separate from this change. Notes with fundamentals at or above Nyquist are silent, preventing invalid phase reads at unusually low device sample rates.

The host saves `wtTable` through APVTS. Its choice list is append-only: preserve existing indices when adding tables. Old state without this field explicitly restores index 0. GUI parameter attachments use the value delivered by the callback, since APVTS raw atomics may update later in listener order.

Band-limit levels restrict stored harmonics. This is not a claim of alias-free fast modulation: frame-scanning sidebands and linear-interpolation images still need separate oversampling/interpolation work.

## Search module and reference

`Source/Library/borato_preset_search.*` is a local copy of the supplied module from `../rea/borato_preset_search`. The browser uses its pure `PresetIndex::matches` metadata matching on the message thread. It does not scan preset folders or parse Serum presets. This project builds without the sibling checkout.

See [WAVETABLE_RESEARCH.md](WAVETABLE_RESEARCH.md) for the Serum context, saved Ghidra evidence and REA installation checks. REA is installed and connected; its live MCP process needs reconnection with the existing user environment to see the installed Ghidra provider.

## Validation and limits

Debug tests cover every bank/frame/mip, spectral cutoff, DC, finite samples, peak normalization, interpolation, legacy equality, bank distinctness, transition continuity and rapid switching at 8–192 kHz. Processor/UI tests cover actual MIDI output differences, held-note automation, state migration, GUI selection labels, search, gestures, frame scanning, FFT rendering and resizing. Standalone and VST3 Release builds are produced in `build-ui/BoratoWave_artefacts/Release/`.

WAV import and FFT editing remain pending. Unison and modulation routing are now implemented; see [MODULATION.md](MODULATION.md). Hardware listening and DAW automation remain manual validation.

[Library](images/wavetables.png) | [Filtered library](images/wavetable-search.png) | [Harmonics](images/wavetable-spectrum.png)

Both OSC A and B now select these tables independently; see [SOURCES.md](SOURCES.md).
