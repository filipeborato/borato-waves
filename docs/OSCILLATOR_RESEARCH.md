# Oscillator architecture: focused investigation

Investigated 2026-10-17. This is a feature/architecture reference for original Borato Wave DSP, not a reproduction of Serum source or factory content.

## Target and method

The installed `C:/Program Files/Common Files/VST3/Serum2.vst3` SHA-256 was independently checked: `7978c9be5b2107e985c24c174000faee87e11483ec989d81dc61b5829b45ed70`, matching [SERUM2_CONTEXT.md](../SERUM2_CONTEXT.md). The saved `../rea/ghidra/Serum2` project was reused with Ghidra 12.1.4, `-process Serum2.vst3 -noanalysis -readOnly`, and its existing `Decomp.java` script. No re-import, full analysis, target execution, or project modification was needed.

REA MCP `binary_session` was called again: REA **6.4.0**, server started `2026-10-17T17:18:40.134Z`, no open binary. Its Ghidra provider still reports `not_configured`: the live process lacks `GHIDRA_INSTALL_DIR`. The locally installed headless Ghidra works. Consequently, the fresh decompilation below is **direct Ghidra evidence**, not a REA Evidence record. Reconnecting with that environment variable would restore native analysis through MCP; reinstalling REA is unnecessary.

## Fresh evidence ledger

Local evidence artifacts are in `build-ui/rea-oscillators/` (generated investigation output, not plugin source). Both headless commands exited successfully.

| Local evidence | Observed behavior | Interpretation and limits |
|---|---|---|
| `1803aa7b0.c` | Constructs five oscillator processors, stores separate pointers, selects one by type. | Confirms reusable per-type processors. Together with context section 4, supports separating persistent configuration from voice instances. Does not establish every type's audio algorithm. |
| `1803afe00.c` | Calls one generator 16 times and writes separate double values at offsets `0x538` through `0x5b0`. | Corroborates the context's per-unison random-phase initialization. Distribution and musical phase scaling were not recovered. |
| `1803b5550.c` | Calls parameter preparation, loops over oscillator lanes, calls the wavetable kernel `1803b8a90`, then calls `1803bb9c0`. It interpolates changing state before kernel reads. | Supports separate phase/lane rendering and stereo accumulation, with smoothing. Exact interpolation constants and oversampling remain unresolved. |
| `1803bb9c0.c` | One-lane path copies the same signal to two destinations. Multi-lane paths accumulate samples using two separate coefficient sets, then write two destinations. | Consistent with stereo unison mixing; exact panning/detune laws and final A/B summation are not established. |
| `1803b66f0.c` | Updates state conditionally and calls `1803ba420` with a coefficient destination and lane count. | Evidence of parameter-dependent stereo coefficients; function is large and not fully semantically recovered. |

The context additionally identifies three independent oscillator states, separate wavetable identity/position, octave/pitch/fine controls, initial/random phase, unison settings, and per-voice filters. These remain **prior-report observations**, not newly verified parameter mappings. Static pseudocode does not demonstrate runtime sound or numerical equivalence.

## Implementation implications

Use independent A/B parameter sets, table readers, phases, pitch, gain, and stereo position. Share only the immutable table bank. Preallocate unison lanes; choose mip levels from each lane's actual frequency, including pitch bend and detune. Smooth audible changes and keep search, FFT display, allocations, and table construction off the audio thread. Preserve old parameter IDs and restore newly introduced controls to defaults when loading old sessions.

The particular pan law, detune distribution, transition time, maximum lane count, and source-mixing headroom are Borato design choices and should have numerical tests. No exact Serum behavior is claimed for them.

## Gap inventory at investigation start

Source inspection showed one wavetable oscillator, amplitude ADSR, one low-pass filter, output gain, and eight procedural tables. GUI previews substantially exceeded working DSP. Prioritize these independently testable increments:

1. **Oscillator foundation:** real B, independent table/position/level/pitch/pan, A/B enable, MIDI pitch bend, stereo unison, sub and noise sources.
2. **Modulation:** saved LFO shapes/rate/retrigger/sync, additional envelopes, destination matrix, macros, visible modulation depth. The existing editable LFO graph is not a DSP source.
3. **Filtering and effects:** selectable filter modes and routing; actual distortion, chorus, delay/reverb, bypass and order. Rack graphics alone do not process audio.
4. **Content workflow:** WAV frame import, user preset save/load/indexing, undo, wavetable frame/harmonic editing. Current table search is not a filesystem preset library.
5. **Advanced synthesis:** warp with measured alias control, oscillator C, sample/multisample/granular/spectral engines, arp/clip playback, MPE, oversampling and quality options.

This list is a baseline audit, not a claim that every feature was implemented in this revision. Consult current implementation documentation and tests for completed scope. Exact Serum A/B mixing, detune distribution, warp kernels, and runtime sonic matching remain unresolved.
