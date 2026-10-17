# Wavetable reference and REA verification

Verified 2026-10-17. This records architectural reference material, not a reproduction of Serum code or factory content.

## Existing evidence

`SERUM2_CONTEXT.md`, sections 5.1–5.5, reports the following from earlier static analysis:

- Wavetables contain frames of 2048 floating-point samples, with up to 256 frames; table identity and frame position are separate state.
- Loading, editor actions, and graph updates use explicit UI/DSP messages. Table updates are applied at a processing boundary.
- Creation methods include formulas, harmonic editing, audio import, and morphing between frames. Phase warp is a separate oscillator control.
- The report distinguishes confirmed observations from inference. In particular, the second interpolated table read and mipmap construction are not fully resolved.

These findings were read from the supplied report, not independently decompiled again. `../rea/ghidra/Serum2.gpr` and `Serum2.rep/` exist; `analysis.log` records a successful import, analysis, and save of `Serum2.vst3` on this date (1179 seconds). No new native analysis session was opened or existing session closed.

## Application to Borato Wave

Original procedural table families, independent selection per oscillator, continuous frame position, and a waveform display reading the same data as the oscillator provide a useful first implementation. Construct band-limited table data outside the audio callback. Keep table identifiers persistent and route GUI changes through host parameters. Spectral resynthesis, arbitrary file import, and Serum's exact interpolation implementation are separate future work; the supplied evidence does not establish equivalence.

`../rea/borato_preset_search/` is an original JUCE module for filesystem preset indexing and text/tag/category/rating filtering. It contains no wavetable DSP or waveform loader. Its searches allocate and acquire a critical section, so it belongs in a browser/background workflow, not the audio callback. Ratings are memory-only and keyed by path.

## REA installation status

The registered public launcher was inspected and executed with `doctor --client codex --json`:

- REA package **6.4.0**, installed skill **37**, Codex registration **aligned/healthy**.
- Node **24.22.0**; Ghidra **12.1.4** configured and available at `C:/Users/filipe/tools/ghidra_12.1.4_PUBLIC`.
- Java **21.0.12.1**, native decompiler, headless launcher, and Windows authority checks passed.
- `rea` is absent from the shell PATH, but the registered `rea.mjs` launcher works through `C:/nvm4w/nodejs/node.exe`.

The parent agent additionally called the connected MCP `binary_session`: REA **6.4.0** is running (started `2026-10-17T16:00:05.095Z`) and no binary is open. In that live server, Ghidra reports `not_configured` because `GHIDRA_INSTALL_DIR` is absent, although Java is available. This differs from the shell doctor's available Ghidra result: installation and live server environment are separate checks. A future server restart with `GHIDRA_INSTALL_DIR=C:/Users/filipe/tools/ghidra_12.1.4_PUBLIC` is needed for native Ghidra work through that connection. No installation or configuration was changed. Missing optional Hopper/IDA providers are unrelated. The supplied context warns that opening this binary through the Ghidra provider repeats expensive analysis; reuse the saved project for future focused investigation.
