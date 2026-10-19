# Borato Wave — architecture map

Original wavetable synth in JUCE. Serum 2 is the feature reference, not the source:
nothing here is decompiled code, extracted layout, or shipped artwork.

## What Serum 2 is built from (observed)

Source: class names (RTTI), source paths and imported DLLs inside
`Serum2.vst3` (sha256 `7978c9be…45ed70`, 2026-10-14). Names say *which parts exist*, not how
they are implemented.

| Layer | Serum 2 (observed) | Borato Wave (JUCE equivalent) |
|---|---|---|
| UI framework | VSTGUI 4 from the VST3 SDK (`..\libraries\vst3sdk\vstgui4\…`, 180 `VSTGUI::` classes) | `juce_gui_basics` |
| Renderer (Windows) | Direct2D + DirectWrite + DirectComposition (`d2dgraphicscontext.cpp`, `DWriteCreateFactory`, `IDCompositionDesktopDevice`); no OpenGL strings | JUCE 8+/9 Direct2D renderer (default on Windows) |
| Layout | Declarative `.uidesc` file in a `DATA/SERUM2.UIDESC` PE resource (zstd-compressed, 123 KB), loaded by `XferVST3Editor` | Plain C++ `resized()` with `Rectangle` slicing (see `WaveEditor::Interface` in `PluginEditor.cpp`) |
| Section logic | One sub-controller per section: `OscController`, `WTOscController`, `FilterController`, `EnvController`, `LFOController`, `ModMatrixController`, `FXRackController`, `PresetBrowserController`, `ArpController`, `ClipPlayerController`… | Custom components in `Source/UI/`; dials use `ParameterAttachment` |
| Page switching | `XferUIDescriptionViewSwitchController`, `GlobalPageController`, tab controllers | Custom `Action` tabs and `setVisible` swaps |
| Custom displays | `WTDisplay`, `EnvelopeDisplay`, `LFODisplay`, `LFOPathDisplay`, `FilterDisplay`, `SpectralFilterDisplay`, `EQDisplay`, `DelayDisplay`, `DistDisplay`, `IRDisplay`, `FFTDisplay`, `SpectrogramDisplay`, `GranularDisplay`, `SampleDisplay`, `MultiSampleMappingDisplay`, `ZoneEditorDisplay`, `ChaosGraph`, `SNHGraph`, `PZEditorGraph`, `ScalarCurveDisplay`, `VoiceModDisplay`, `CPUGraph` | `Graph::paint()` views, repainted by a 30 Hz `Timer` when parameters change |
| Knobs | `CAnimKnob`/`XferAnimKnob`/`CFracAnimKnob` (filmstrip bitmaps) **and** `VectorKnob`/`XferKnob` | Custom `Dial::paint()` and mouse/keyboard input (vector) |
| Meters | `XferVuMeter`, `CompMeter`, `ValueMeter`, `MidiActivityMeter` | `Component` + lock-free level from the audio thread |
| Value readouts | `XferParamDisplay`, `DraggableParamDisplay` (drag a number to edit) | Custom `Dial` formatted readout with drag, wheel and arrow-key input |
| Lists / browser | `CDataBrowser`, `PresetDataBrowserView`, `PresetLocationTreeView`, `CSearchTextEdit` | Custom `WavetableBrowser` / `SearchField` + `borato_preset_search` matching |
| Modulation drag | `ModSourceView`, per-source `kUIParam…Drag` (LFO1Drag…Macro8Drag) | `DragAndDropContainer` + `DragAndDropTarget` on knobs |
| Undo | `WaveProcessor::beginEdit/endEdit/undo/redo` | Snapshots of parameters, curves, macro names, user tables and sample per gesture; host automation is not recorded |
| Preset store | SQLite `System\presets.db` (Presets/Tags/Locations/UserData) | `borato_preset_search` (in-memory; SQLite only if needed) |

## Build order (what to clone feature-wise, in our own code)

Each step is playable before the next starts.

1. **Done:** wavetable osc (FFT mipmaps, 16 frames) → SVF lowpass → ADSR, 16 voices, vector knobs, WT 3D view, env view.
2. **Sources done:** 2nd osc + sub + noise, independent pitch/pan, MIDI bend. Wavetable import (`.wav`, 2048-sample frames) remains pending.
3. **Modulation done:** 2 editable LFOs, 16 matrix routes, 4 macros, drag-to-modulate; A/B unison up to 8 lanes. Additional envelopes remain pending.
4. Filter types (ladder, comb, formant…) + filter response display.
5. **FX rack done** in fixed order (distortion > chorus > delay > reverb > EQ > compressor), 20 ms crossfaded bypass. Reordering remains pending.
6. Preset browser (wire `borato_preset_search`), undo.
7. Granular / sample / spectral oscillators, arp / clip player.

Unknown until studied from public sources (manual, videos, measuring output): exact DSP of each
filter/FX, unison spread law, warp modes.

## Interface implementation

See [GUI.md](GUI.md) for the current custom-component design, working controls, preview scope and build/validation commands.


## Wavetable bank

OSC A and B each support eight immutable procedural tables, a searchable custom browser, 2D/3D/FFT views, automated frame scanning and phase-preserving 15 ms bank transitions. See [WAVETABLES.md](WAVETABLES.md) for DSP, persistence and validation details.


See [SOURCES.md](SOURCES.md) for source mixing and state compatibility, [OSCILLATOR_RESEARCH.md](OSCILLATOR_RESEARCH.md) for fresh decompilation evidence, and [ROADMAP.md](ROADMAP.md) for current priorities.

## Modulation and unison

`Modulation.h` owns curve evaluation and bounded snapshot exchange. `WaveVoice` owns per-note LFO phase, route transitions and preallocated unison lanes; `WaveProcessor` supplies global/transport timing and persistent extra state. `ModulationUI` owns custom curve/matrix/macro interaction. See [MODULATION.md](MODULATION.md).

## Factory patch content

`FactoryPreset` supports optional custom LFO curves and immutable embedded tables, loaded through the existing control-thread state paths. The Atelier collection adds twelve original patches to the existing bank; see [PRESET_RESEARCH.md](PRESET_RESEARCH.md) for evidence, sound-design choices and validation commands.

## Voices and warp

`WaveSynth` (in `PluginProcessor.h`) extends JUCE's `Synthesiser` with mono/legato last-note priority; `WaveVoice` owns glide, steal declicking, the A/B phase warp and FM from B into A. See [VOICES.md](VOICES.md).

## Sample source and table editor

`Sample.h` holds the immutable decoded sample and the per-voice `SamplePlayer`. `PublishSlot<T>` (in `UserWavetable.h`) publishes user tables and samples to the audio thread; the processor reports after each block when every voice has settled, so replaced data is freed promptly. `TableEdit.h` is the message-thread model behind the `UI/TableEditor` page. `UI/SamplePanel` drives the sample source. See [SOURCES.md](SOURCES.md) and [WAVETABLES.md](WAVETABLES.md).
