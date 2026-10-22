# Build and release Borato Waves

Requires CMake 3.22+, C++20, and JUCE 9.0.4. The CMake target is `BoratoWave`; the product/bundle name is `Borato Waves`.

## Local builds

From a Visual Studio development environment on Windows:

```powershell
cmake -S . -B build -DJUCE_DIR=C:/JUCE
cmake --build build --config Debug --parallel 2
ctest --test-dir build -C Debug --output-on-failure --parallel 1
```

`BORATOWAVE_BUILD_TESTS` defaults to ON. Tests use assertions: run Debug, not Release. All 12 executables are registered with CTest, with a 30-minute timeout per test. On headless Linux use `xvfb-run --auto-servernum ctest --test-dir build --output-on-failure --parallel 1` after a Debug build. Run tests sequentially because some fixtures use shared temporary paths.

To build without a local JUCE checkout:

```powershell
cmake -S . -B build-fetch -DBORATOWAVE_FETCH_JUCE=ON -DBORATOWAVE_BUILD_TESTS=OFF
cmake --build build-fetch --config Release --target BoratoWave_VST3 BoratoWave_Standalone --parallel 2
```

Fetch mode takes precedence over JUCE_DIR and downloads the pinned upstream commit for JUCE 9.0.4. With fetch disabled, an invalid JUCE_DIR is an error. Windows retains ASIO support. Builds never install plugins automatically.

Linux uses Ninja and `-DCMAKE_BUILD_TYPE=Debug` or `Release`. Install the development packages listed in `.github/workflows/ci.yml`; tests additionally need Xvfb and xauth. macOS uses Xcode, with an explicit arm64 or x86_64 architecture and deployment target 11.0.

Outputs are under `build/BoratoWave_artefacts/<configuration>/VST3` and `Standalone`. No AU or CLAP targets are included.

## GitHub Actions

Exactly two workflows:

- `ci.yml`: PRs to main/master; Ubuntu 24.04 Release VST3 compilation only. No artifacts. A newer commit cancels the previous run for that PR.
- `build-release-artifacts.yml`: only pushed `v*` tags; Windows x64, Linux x64, macOS arm64 and Intel Release builds, plus the complete Debug test suite on Ubuntu/Xvfb. The platform build matrix starts only after those tests pass, avoiding expensive builds when the Linux gate fails. No CPU benchmark gate on shared runners.

Release tags must exactly match `v` plus the version in `project(BoratoWave VERSION ...)`. A mismatch fails before builds start. Change that version deliberately for a new release, commit the intended source, then create and push its tag, for example for version 0.1.0:

```sh
git tag v0.1.0
git push origin v0.1.0
```

A GitHub remote and Actions enabled on that repository are prerequisites. Do not reuse a release tag for different source. The workflow uses GITHUB_TOKEN; only the final release job has contents:write permission.

Each platform uploads one Actions artifact (14-day retention) containing two archives, installation instructions and SHA-256 checksums. Eight archives reach a draft GitHub Release only after every build and test succeeds and every checksum passes. Download files from Actions or the Releases draft. Review the draft and test the packages in real hosts before publishing manually.

Windows archives use ZIP; macOS ZIPs preserve bundles using ditto; Linux TAR.GZ preserves executable permissions. macOS binaries are ad-hoc signed and architecture-checked, not notarized. Windows binaries are unsigned. Gatekeeper/SmartScreen may require user intervention. Linux packages target Ubuntu 24.04, not arbitrary older glibc distributions. A successful build is not proof of audio-device/DAW compatibility.

## Workflow validation

Run `actionlint` at the repository root. Check a fresh local-JUCE configuration and a fresh fetch-JUCE configuration. On GitHub, verify a PR runs only the cheap Ubuntu job, ordinary branch pushes run neither workflow, and a matching version tag runs the release pipeline. Never create a test release tag merely to lint YAML.

`.scratch/` and `docs/` are local, ignored working material; release/build instructions live in this tracked file.

## Local portability preflight

Before pushing build fixes, use Ubuntu 24.04 in WSL (or a native Ubuntu machine) to run the same GCC/Ninja build locally. Install every development package listed in the workflows, including `libxi-dev` for JUCE 9 XInput2 headers. Keep the build directory on the Linux filesystem. Configure Debug with tests enabled, build, and run CTest sequentially through Xvfb. Configure Release separately and build both distribution targets.

Windows x64 can be built locally with Visual Studio. macOS arm64 and Intel (x86_64, not 32-bit x86) require Xcode on a Mac for actual compilation and binary validation. Static checks from Windows do not prove macOS compatibility. Do not create release tags to probe compilation; group fixes and validate locally first.
