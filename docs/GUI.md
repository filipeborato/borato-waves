# Borato Wave: interface v1

The interface uses the section/view separation described in `SERUM2_CONTEXT.md` as an architectural reference. All layout, artwork and controls are original, drawn with JUCE `Graphics`, paths, gradients and text. No extracted Serum resources are required.

## Visual system

The 1240 x 940 design scales with the editor between 80% and 140%. Graphite surfaces, fine borders and restrained colour accents separate the sound layers: mint for oscillators, blue for envelopes, coral for filters/effects and violet for modulation. `Source/UI/Theme.h` owns colours and drawing helpers.

- `Controls.*`: custom `Component` implementations for actions, parameter dials and the performance keyboard. No `Slider`, `Label`, `Button`, `ComboBox`, `TabbedComponent` or `MidiKeyboardComponent` is used in the plugin editor.
- `Displays.*`: wavetable, envelope, filter-response and curve-editor views, plus stereo output meters. `ModulationUI.*` implements the persistent curve editors, matrix and macro controls.
- `PluginEditor.*`: page composition, navigation, factory patch browser and editor scaling. `WavetableBrowser.*` implements the searchable source-table library.

## Working features

Both oscillators, sub and noise have custom parameter controls connected through `ParameterAttachment`. A/B each expose enable, table, position, level, coarse/fine tuning and stereo pan; MIDI pitch bend spans two semitones. Vertical dragging, Shift-dragging, wheel, arrow keys and double-click reset generate host automation gestures. Host state restoration updates the controls. Tab moves keyboard focus.

The eight-table library has metadata search and category filters. The wavetable has 2D, 3D and FFT views, with direct dragging to scan frames; see [WAVETABLES.md](WAVETABLES.md). Envelope and filter displays follow parameter changes; the filter curve is explicitly a schematic response. The 61 factory patches set actual engine parameters. Modified or restored values are identified as a matching factory patch or Custom patch.

The four-octave keyboard supports mouse glissando, velocity from click height, octave changes and computer keys `A W S E D F T G Y H U J K` while focused. Focus loss and editor destruction release locally held notes. Output meters read atomic block peaks; painting stays on the message thread.

## Preview scope

The effects rack controls distortion, delay and reverb in the audio engine. LFOs, macros and matrix routes also process audio and persist in host state; see [MODULATION.md](MODULATION.md). Two LFOs use free-point curve editors, and A/B expose eight-lane unison controls.

## Build and validation

The supplied `build/` cache points to an older checkout. Use a fresh build directory:

```powershell
cmake -S . -B build-ui -DJUCE_DIR=C:/JUCE
cmake --build build-ui --config Debug --parallel 4
./build-ui/test_wavetable_artefacts/Debug/test_wavetable.exe
./build-ui/test_sources_artefacts/Debug/test_sources.exe
./build-ui/test_modulation_artefacts/Debug/test_modulation.exe
./build-ui/test_interface_artefacts/Debug/test_interface.exe "$PWD/docs/images"
& './build-ui/BoratoWave_artefacts/Debug/Standalone/Borato Wave.exe'
```

`test_interface` checks host gestures, fine dragging, parameter synchronisation, state restoration, keyboard glissando/release, MIDI rendering, meter values, page navigation, factory patch selection and minimum-size rendering. Its optional absolute output directory receives snapshots rendered by the real editor. Use Debug so assertions run.

The existing synth/keyboard engine retains JUCE's internal synchronisation. This GUI work does not establish hard real-time guarantees for that engine. The new metering path adds two atomic stores and bounded buffer scans, with no UI calls from the audio callback.

## Verified output

### Standalone audio on Windows

ASIO is enabled through `JUCE_ASIO=1`, following `../borato-morphium`.
JUCE supplies the bundled ASIO headers; no separate SDK download is needed.
The bundled Steinberg source licence terms apply to builds using ASIO.

In the Standalone window, open **Options > Audio/MIDI Settings...**, choose
**ASIO** as the audio device type, then select an installed ASIO driver and its
output channels. Adjust sample rate and buffer size there, or use the driver's
control panel when required. The audio interface's Windows ASIO driver must be
installed separately. JUCE persists the selected device settings between launches.
WASAPI remains available. In the VST3, the DAW selects the audio driver.

The Windows integration test verifies ASIO backend registration and lists installed
drivers without opening hardware. Streaming and latency require a device test.

### Interface validation

Debug VST3 and Standalone builds completed without warnings. All four test executables passed. The VST3 manifest helper loaded the built module successfully. Screenshots were inspected at 100% and 80% scale. Native Windows automation was unavailable, so live DAW interaction and listening remain manual checks.

[Synth](images/modulation-unison.png) | [Effects](images/effects.png) | [Matrix](images/modulation-matrix.png) | [Global](images/global.png) | [Patch browser](images/browser.png) | [80% size](images/minimum-size.png)


See [SOURCES.md](SOURCES.md) for the dual-oscillator implementation and [ROADMAP.md](ROADMAP.md) for the remaining feature gaps.
