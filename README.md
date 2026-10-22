# Borato Waves

A wavetable synthesizer built with C++20 and JUCE, available as a **VST3** plugin and a **Standalone** application.

## Features

- Wavetable oscillators, sub oscillator, and noise, with unison, FM, and wave shaping.
- Sample playback, a wavetable editor, and audio import.
- Envelopes, LFOs, a modulation matrix, and macros.
- Filters and effects, including chorus, EQ, and compression.
- Factory presets and user preset saving.
- MIDI input and ASIO support in the Windows Standalone application.

## Building

Requires CMake 3.22 or later, a C++20 compiler, and development tools for your platform. On Windows, use a Visual Studio development environment.

The following commands download the pinned JUCE 9.0.4 revision:

```sh
cmake -S . -B build -DBORATOWAVE_FETCH_JUCE=ON -DBORATOWAVE_BUILD_TESTS=OFF
cmake --build build --config Release --target BoratoWave_VST3 BoratoWave_Standalone --parallel 2
```

Binaries are generated in `build/BoratoWave_artefacts/Release/`. For Linux setup, dependencies, Debug tests, and using a local JUCE checkout, see [BUILDING.md](BUILDING.md).

## Installation and usage

Copy the complete `Borato Waves.vst3` bundle to your platform's plugin directory:

| Platform | VST3 directory |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3\` |
| macOS | `~/Library/Audio/Plug-Ins/VST3/` |
| Linux | `~/.vst3/` |

Rescan plugins in your DAW and load Borato Waves on a MIDI instrument track. In the Standalone application, configure your audio and MIDI devices before playing.

## Automated builds

- **Pull requests to main/master:** VST3 compilation on Ubuntu.
- **Version tags matching `v*`:** VST3 and Standalone builds for Windows x64, Linux x64, macOS Apple Silicon, and macOS Intel, plus Debug tests on Ubuntu.
- Packages and checksums are collected in a draft release only after validation succeeds.

The tag must match the version declared in CMake. See [BUILDING.md](BUILDING.md) for the release procedure.

## Project status

Under active development. Windows packages are not commercially signed; macOS packages are ad-hoc signed but not notarized. Testing in real DAWs and with physical audio devices complements the automated tests.

Sample playback includes filtered transposition, per-voice level/pan/pitch modulation, and portable key-zone banks with up to 128 zones. See [SAMPLING.md](SAMPLING.md) for mapping, measurements, and reader limits. Velocity-layer multisampling, granular synthesis, and spectral resynthesis are not included.
