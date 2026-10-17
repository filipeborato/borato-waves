# Preset construction and the Atelier collection

## Evidence and limits

On 2026-10-17, the installed `C:/Program Files/Common Files/VST3/Serum2.vst3`
hashed to SHA-256 `7978c9be5b2107e985c24c174000faee87e11483ec989d81dc61b5829b45ed70`,
matching the existing Serum 2 context. The REA MCP session reported Ghidra unavailable
because its process did not have `GHIDRA_INSTALL_DIR`. Investigation continued with
the installed Ghidra 12.1.4 headless executable against the saved `Serum2` project,
using `-process Serum2.vst3 -noanalysis -readOnly` and the existing `Decomp.java`.
This is direct Ghidra evidence, not a successful REA native-provider session.
The outputs are under `build-ui/rea-presets/`; no target code was executed or modified.
The saved program's `getExecutableSHA256()` and executable path were also read back
through Ghidra and matched the installed binary (`identity.log`).

Fresh decompilation observations:

| Function | Observed state fields / behaviour | Implication for Borato |
|---|---|---|
| `0x180044f20` | Serialisation references `plainParams`, `scalars`, `lfoPointModAssignments`, `embedded_audio`, `activeClips`, tuning and MPE fields | A complete patch includes structured data and resources beyond knob positions. |
| `0x1800425b0` | Loading checks product/version, handles `serum1ChunkVersion`, tuning and compatibility flags; routing includes `destModuleTypeString`, `destModuleID`, `destModuleParamName` | Preserve patch data and compatibility explicitly; keep modulation destinations meaningful. |

Both functions have decompiler type-propagation warnings. Field references establish
their presence, not a complete format specification. Module-specific schemas, exact
compression, macro response laws and the authors' sound-design process remain unknown.
No individual Serum patch or factory waveform was extracted or copied. The supplied
`BORATO_PRESET_SEARCH_CONTEXT.md` describes cataloguing, not musical quality.

## Original implementation

Atelier adds twelve patches after the existing factory bank, preserving old indices,
parameter IDs and recipes. Each patch supplies four intentional macro mappings and
a custom periodic curve, with velocity/envelope modulation appropriate to its role.
The original 49 presets are retained unchanged.

Four original 16-frame tables are generated once outside the audio callback:

- **Reed:** odd-harmonic core with progressively stronger even partials.
- **Prism:** moving harmonic ridge above a stable fundamental.
- **Tine:** sparse integer-harmonic clusters; this is not physical or inharmonic modelling.
- **Silk:** smooth, exponentially tapered harmonic fan.

They use the existing FFT band-limiting and immutable user-table playback path.
Factory metadata holds optional curves and shared immutable tables. Saved sessions
and `.bwpreset` files embed curves and full-band cycles using the existing format;
no new host parameter or state-version migration is required. Matching checks the
cycles with a small tolerance for FFT reconstruction, rather than trusting names.
Loading an old patch clears the embedded slots. Invalid/nonfinite saved samples are
rejected before FFT reconstruction.

## Listening and validation

Build `test_atelier` in Debug and run it with an absolute output directory:

```powershell
cmake --build build-ui --config Debug --target test_atelier test_presets
./build-ui/test_atelier_artefacts/Debug/test_atelier.exe "$PWD/build-ui/atelier-previews"
./build-ui/test_presets_artefacts/Debug/test_presets.exe
```

The Atelier test covers patch identity, curves, embedded tables, state/file round trips,
three sample rates, three velocities, macro endpoints/midpoints, low/high notes and
chords. The rate/velocity/macro-combination grid is pairwise, not exhaustive. Individual
macros are also checked separately. Longer renders check both stereo tails.

The output contains twelve portable `.bwpreset` files and wet/dry WAV pairs with
category-appropriate phrases. Listening copies target -23 dBFS active RMS, capped at
-1 dBFS peak; this does not alter patch gain or claim perceptual LUFS matching. Raw
rendered peaks are tested before scaling. Compare articulation, harmonic identity,
macro usefulness and stereo/mono behaviour by ear. Numerical success alone cannot
establish musical quality or guarantee headroom for every possible MIDI performance.

Validation completed on Windows, 2026-10-17:

- Debug `test_presets`: all 61 patches passed the existing loudness/headroom/tail checks.
- Debug `test_atelier`: all 12 patches passed; 252 short scenario renders plus 24
  long wet/dry renders, state/curve/table checks and rejection of a NaN table sample.
- Debug `test_interface`: browser selection, custom table caption, custom curve,
  MIDI and the existing UI checks passed; screenshot in `build-ui/atelier-ui/atelier-patch.png`.
- Release Standalone and VST3 built successfully. No new audio-callback allocation
  or locking path was introduced; table synthesis and matching run on control/UI paths.
- `build-ui/atelier-previews` contains 24 WAVs and 12 portable preset files.

Human listening through an ASIO device and a real DAW was not performed. The automated
tests validate numerical behaviour and integration, not subjective sound quality or
worst-case real-time CPU performance.
