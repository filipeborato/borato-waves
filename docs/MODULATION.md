# Modulation and unison

The instrument now has two editable LFOs, four macros, sixteen modulation routes and up to eight unison lanes independently in A/B. These are original algorithms informed by the architecture investigation, not numerical reproductions of Serum.

## Playing and editing

Choose LFO 1 or 2 on Synth. Double-click the graph to add a point, drag points to move them, select an internal point and press Delete to remove it, or drag a segment vertically to bend it. Up/down edits the selected point; left/right selects its neighbour. Up to 32 points are saved; endpoints share their value to close the cycle. Sine, triangle, up/down ramps and square are starting shapes. The sine preset is a 16-segment approximation. The phase cursor shows the last-started voice and freezes when that voice ends.

Rate spans 0.01–40 Hz. Sync selects straight, dotted or triplet musical durations from four bars through 1/64 (four quarter notes per bar). Note mode restarts for each note. Free continues globally, including silence. Transport derives phase from host PPQ and freezes when the host reports stopped transport. Without PPQ it continues freely. Host BPM takes precedence over the editable internal BPM (120 default). Unsynced Transport maps seconds through current BPM; tempo changes can realign its phase.

Drag an LFO handle onto a compatible Synth dial to create a route and open its depth in Matrix. Drag a macro handle over the Synth tab before dropping it on a dial. Matrix has two pages of eight routes; click a source/target selector to cycle forward, right-click or use arrows to go backwards. A full matrix reports that a route must be disabled; existing assignments are never overwritten. Double-click a macro name to edit, Enter to save, Escape to cancel.

Sources are LFO 1/2, Macro 1–4, amplitude envelope and velocity. Targets are A/B position, level, pan, pitch, detune and spread, plus cutoff, resonance, sub level and noise level. Bipolar converts the source's 0–1 value to -1–1. Route depth is a fraction of the target's normalized parameter range. Contributions sum, clamp to the valid range, then convert to physical units. Base parameters are never overwritten. Violet dial arcs show the possible modulation range, not the instantaneous sum of all voices. Recursive routing and extra envelopes are not implemented.

## Unison

Each oscillator exposes 1–8 voices, 0–50-cent detune, 0–100% spread, initial phase and phase randomness. Lanes distribute detune evenly around the note and pan around the oscillator's base pan. Phase starts are evenly spaced over one cycle; randomness adds a random phase offset. With zero detune and zero spread, evenly spaced phases can cancel some waveforms; add detune/spread for the intended layered sound.

Mixing uses 1/N lane gains and the existing equal-power pan with unity centre. Changing count ramps gains and offsets over 20 ms without resetting surviving lanes. New lanes fade in from retained phase. Each lane chooses bandwidth from its own effective pitch. The wavetable crossfade advances once per output sample, regardless of lane count. Sixteen notes with A/B at eight lanes produce up to 256 wavetable oscillators.

## State and real-time boundaries

New APVTS parameters append to existing IDs. Curves and macro names occupy version 1 of the `MODULATION` state section; point edits are not individual host automation parameters. Invalid curves restore the default. Missing legacy parameters restore disabled routes, zero macros and one unison lane. Factory patch matching includes curves and macro names. Curve/name edits notify the host that non-parameter state changed.

GUI/state writers serialize curve publication outside render. A fixed triple buffer transfers complete snapshots to audio at block boundaries. Voices crossfade curve edits over 20 ms and queue the latest edit during a transition. Gates, macro values, route depth/assignment and lane changes are smoothed; all new render storage is fixed. Existing JUCE synthesiser and keyboard synchronization remains in the surrounding engine.

Host parameter changes are read at render-block boundaries, while MIDI events use sample offsets. Fast modulation can produce sidebands; no oversampling or alias-free pitch/warp claim is made. Output is additive and has no limiter.

## Validation

Run Debug `test_wavetable`, `test_sources`, `test_modulation` and `test_interface`. The latter accepts an absolute snapshot directory. The modulation suite checks curve validity and concurrent publication, live curve edits, polarity/sum/limits, tempo/transport/retrigger, block-size independence, tuning, stereo/mono, count transitions, persistence and legacy migration. Stress covers sixteen notes at 8/44.1/48/96/192 kHz.

The original Release `test_modulation.exe --benchmark` measured 128-sample blocks, sixteen held notes, A/B at eight lanes, sixteen routes and live automation. Those results are offline CPU measurements, not ASIO streaming/latency guarantees. Hardware listening and DAW automation remain manual validation.

The current benchmark also reports light, layered and effects-enabled profiles; see [CPU_PROFILING.md](CPU_PROFILING.md) for its process-only timing method, reference results and device/host procedure. The historical result below used whole-loop timing and must not be compared directly to the new process-only percentages.

Measured on AMD Ryzen 7 5700X, Windows x64, Release/LTO, 2026-10-17 (one approximately one-second audio run per rate):

| Sample rate | Wall time / audio time | Single-thread time budget |
|---|---|---|
| 8 kHz | 0.064 / 0.992 s | 6.4% |
| 44.1 kHz | 0.382 / 0.998 s | 38.2% |
| 48 kHz | 0.420 / 1.000 s | 42.0% |
| 96 kHz | 0.918 / 1.000 s | 91.8% |
| 192 kHz | 1.878 / 1.000 s | 187.8% |

This extreme workload exceeds real-time capacity at 192 kHz and leaves little scheduling headroom at 96 kHz on this CPU. Use fewer simultaneous notes/unison lanes or a lower sample rate for that workload. These short offline measurements do not establish sustained device latency or dropout rates. Raw results: `build-ui/mod-benchmark.log`.

[Synth](images/modulation-unison.png) | [Matrix](images/modulation-matrix.png)

## Env 2 and global destinations

Env 2 (`env2Attack/Decay/Sustain/Release`) is a per-voice ADSR triggered with the amplitude envelope; it is matrix source 9 and has its own drag handle in the Envelopes section. Destinations 16-17 modulate Filter 2 cutoff/resonance per voice. Destinations 18-20 (FX drive, delay mix, reverb mix) are global: the processor evaluates them once per block from Macro 1-4 routes only, because per-voice sources have no single value at the effect rack.

Source and destination lists were extended by appending. Saved states store choice indices and stay valid; host automation of a route's source/destination choice now spans a longer list.
