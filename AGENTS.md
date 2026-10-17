# Repository Guidelines

## Project Structure & Module Organization

Borato Wave is a C++20 wavetable synthesizer built with JUCE, targeting VST3 and Standalone formats. `Source/PluginProcessor.*` contains the audio processor and synthesizer voices; `Source/PluginEditor.*` implements the interface. `Source/Wavetable.h` owns wavetable generation, band-limited levels, and interpolation. `tests/test_wavetable.cpp` contains DSP checks. `build/` holds generated projects and binaries; make build changes in the root `CMakeLists.txt`.

Read `docs/ARCHITECTURE.md` before extending synth features or reorganizing modules. It describes the implementation roadmap and JUCE component mapping. Keep code and artwork original when using Serum 2 as a feature reference. No separate asset directory currently exists.

## Build, Test, and Development Commands

Use CMake 3.22 or newer, a C++20 compiler, and a local JUCE checkout. On Windows, run from a Visual Studio development environment.

- `cmake -S . -B build -DJUCE_DIR=C:/JUCE`: configure; substitute your JUCE checkout path.
- `cmake --build build --config Debug`: build the plugin, standalone app, and test executable.
- `cmake --build build --config Debug --target test_wavetable`: build only the wavetable test.
- `./build/test_wavetable_artefacts/Debug/test_wavetable.exe`: run assertions; success prints `ok`.

Launch the Standalone executable under `build/BoratoWave_artefacts/Debug/` for interactive checks. Use `--config Release` for release builds.

## Coding Style & Naming Conventions

Follow the existing JUCE-style C++: four-space indentation, braces on separate lines for multiline definitions, and a space before function-call parentheses. Use PascalCase for types, camelCase for methods and variables, and `k`-prefixed constants such as `kLevels`. Keep DSP utilities in namespace `bw`. No formatter or linter configuration is present; preserve surrounding style.

## Testing Guidelines

Tests use standard `assert`, with no external test framework, CTest registration, or coverage threshold. Run Debug tests so assertions remain enabled. Add deterministic numerical checks in `tests/test_*.cpp`; register additional executables in CMake. Cover affected sample rates, interpolation boundaries, normalization, and harmonic limits. For audio or UI changes, also verify MIDI playback and parameter behavior in Standalone or a VST3 host.

## Commit & Pull Request Guidelines

Git metadata is unavailable in this checkout, so historical commit conventions cannot be verified. Use concise, imperative commit subjects. PRs should explain the behavior change, link relevant issues, list build and test results, and include screenshots for UI changes.

## Agent skills

### Issue tracker

Tasks live as local Markdown files under `.scratch/`. See `docs/agents/issue-tracker.md`.

### Triage labels

Use the five default triage labels. See `docs/agents/triage-labels.md`.

### Domain docs

Use a single root domain context. See `docs/agents/domain.md`.
