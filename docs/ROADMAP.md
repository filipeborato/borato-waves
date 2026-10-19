# Remaining feature gaps

Audit: 2026-10-17, after the filter/effects/content implementation. This project has a playable wavetable foundation; it does not yet cover Serum 2's full feature set.

| Area | Working now | Remaining work |
|---|---|---|
| Sources | A/B with independent banks, frame position, pitch/fine/pan; sub sine; white noise; MIDI bend; eight-lane unison, detune, spread and phase/randomness; A/B phase warp (bend); FM from B into A ([VOICES.md](VOICES.md)); a looping sample source ([SOURCES.md](SOURCES.md)) | Other warp modes, more sub/noise modes, multisample zones, anti-aliased sample transposition |
| Wavetables | Eight original banks, band-limited mip levels, smooth table switching, 2D/3D/FFT display, metadata search; WAV/AIFF/FLAC import per oscillator (2048-sample frames or Serum `clm ` frame size, up to 256 frames), embedded in state/presets; pitch-detecting import (YIN period, octave correction, one cycle per period); frame drawing and 32-harmonic editor with WAV export | FFT resynthesis import, background import |
| Modulation | Amplitude ADSR/velocity, Env 2 (mod envelope), 2 saved LFO curves, Hz/BPM sync and Note/Free/Transport modes, 16 routes over 24 destinations (incl. Filter 2, A/B warp, FM depth and macro-driven FX drive/delay/reverb mix), 4 named macros, drag assignment and depth display | More envelopes/LFOs, per-voice FX modulation, advanced per-point modulation |
| Filters | Two per-voice TPT SVFs (Low/High 12/24, Band, Notch), serial/parallel routing, per-source destination (Filter 1/2/Direct), exact digital response display | Ladder/comb/formant models, drive |
| Effects | Distortion (Soft/Hard/Fold, 2x oversampled), stereo/ping-pong delay with tempo sync, reverb; 20 ms crossfaded bypass, automatable, tails after note-off; stereo chorus (two swept taps a side); EQ (low shelf, bell, high shelf; zipper-free SVF) and stereo-linked peak compressor with gain-reduction display | Reorder; more EQ bands |
| Presets | 65 original factory presets in 9 categories (three FM, one chorus), including 12 Atelier patches with custom curves and four original embedded tables (data in `Source/FactoryPresets.cpp`, loudness/headroom/tail checked by `test_presets`); searchable browser with category filter over factory and user presets (`borato_preset_search`); `.bwpreset` files with `formatVersion` | Tag editing, ratings, more factory content and wavetables |
| Performance | 16 voices with click-free stealing, sustain pedal, Poly/Mono/Legato with glide ([VOICES.md](VOICES.md)), MIDI keyboard, ASIO Standalone, mono/stereo output | Host listening checks for mono/legato, MPE, configurable bend/voice limits, clipping/headroom policy |
| Advanced synthesis | Wavetable playback, A/B FM, single-sample playback | Oscillator C, multisample/granular/spectral engines, arp and clip playback |
| Release quality | Numerical DSP and interface tests; Release Standalone/VST3 | Listening/DAW automation, CPU profiling under full polyphony, long sessions, installer/signing and cross-platform validation |

## Recommended implementation order

1. **Expand modulation:** extra envelopes and additional destinations, preserving the working LFO/matrix foundation.
2. **Oscillator depth:** measured alias control and oversampling before warp/FM; refine discrete mip transitions under pitch modulation.
3. **Tone and space:** done for filters, distortion, chorus, delay, reverb, EQ and compressor. Next: rack reorder.
4. **Content workflow:** WAV import, immutable table publication and preset files, pitch-detecting import and undo/redo done. Next: resynthesis import, preset tags.
5. **Advanced engines:** separate specifications and validation for sample, granular, spectral and sequencing features.

The [investigation ledger](OSCILLATOR_RESEARCH.md) separates new static evidence from prior context and unresolved algorithms. Further decompilation should target a concrete open question; feature presence alone does not establish a DSP formula.
