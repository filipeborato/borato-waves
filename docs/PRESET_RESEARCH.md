# Preset construction and factory-bank renovation

## Investigation update: 2026-10-19

The installed target was hashed again and still matches
`7978c9be5b2107e985c24c174000faee87e11483ec989d81dc61b5829b45ed70`.
The earlier investigation below is retained as historical evidence. Its statement
that the original bank was unchanged describes the 2026-10-17 Atelier addition,
not the current renovation. The bank at the start of this renovation contains
**65 patches: 49 original recipes, three FM additions, 12 Atelier recipes, and
the appended Chorus Silk Pad**. This corrects the stale 61-preset historical
total below. The baseline executable reports 65 in
`build-ui/presets-before-renovation.log`; the source names the three additions
FM Glass Keys, FM Iron Bass and FM Bell Tower. All 65 belong in the review and
regression boundary.

### Evidence ledger

These identifiers are local report references, **not REA Evidence IDs**. The
connected REA provider still lacks `GHIDRA_INSTALL_DIR`; existing installed
Ghidra was used directly, without changing server configuration.

| Reference | Observation | Boundary |
|---|---|---|
| P1 | PowerShell `Get-FileHash` on the supplied VST3 matches the SHA-256 above. The saved project's identity is recorded in `build-ui/rea-presets/identity.log`. | Establishes artifact identity; does not demonstrate runtime behavior. |
| P2 | Reused `build-ui/rea-presets/180044f20.c` and `1800425b0.c`: save/load reference plain parameters, scalars, LFO point assignments, embedded audio, clips, tuning, compatibility and module/parameter routing identifiers. | Saved direct Ghidra pseudocode, with type-recovery limitations. Full module schemas and compression are not reconstructed. |
| P3 | Fresh `build-ui/rea-presets/1802fab30.c`, generated using the saved project with `-noanalysis -readOnly`: wavetable serialization references paths, load type, frame/channel counts, sample rate, stored phase, embedded data, interpolation and display name. | Static observation. A bitset selects frame-sized blocks for embedding; its semantic meaning and reconstruction rules are not fully established. Earlier shorthand describing all selected frames as “dirty” should not be treated as a verified format rule. |
| P4 | Read-only inventory of `C:/Users/filipe/Documents/Xfer/Serum 2 Presets/Presets`: 658 `.SerumPreset` and 250 `.fxp` files. | Installed content only; no claim of an exhaustive Serum catalogue. Legacy FXP payloads were not parsed. |
| P5 | All 658 `.SerumPreset` metadata headers parsed successfully: bytes 0–7 are `XferJson`, an unsigned little-endian 64-bit length at offset 9 bounds UTF-8 JSON beginning at offset 17, and `fileType` is `SerumPreset`. | Header metadata only. The compressed/body data was neither decoded nor used as Borato preset recipes. Byte 8 and trailing framing semantics remain unspecified. |

P3 command log: `build-ui/rea-presets/module-analysis.log`; input address list:
`build-ui/rea-presets/module-addresses.txt`. Ghidra processed the existing
`C:/Users/filipe/projects/rea/ghidra/Serum2` project with `Decomp.java`.
The inspection did not launch Serum or modify its installation/resources.

P5 used a bounded parser: require magic, reject lengths above 65,536 bytes or
beyond the file, parse just the indicated JSON region, then require the file
type. The bounded search covered all `.SerumPreset` files recursively beneath
the named Presets directory, with 658 successes and zero failures. It did not
validate payload integrity or the meaning of the metadata's `hash` field.

The observed overlapping header tags were:

| Tag | Files |
|---|---:|
| Wavetable | 558 |
| Poly / Mono | 440 / 218 |
| Multisample | 155 |
| Sample | 73 |
| Granular | 56 |
| Spectral | 43 |
| Embedded-Data | 43 |
| Arp / Clip | 33 / 27 |
| KB-Span / Custom-Tuning | 17 / 2 |

Tags are catalogue observations, not proof that a corresponding engine is
audible in each patch. They nevertheless show why a bank-level imitation cannot
be reduced to matching oscillator knobs. The installed resource tree also has
separate Tables, Multisamples, Samples, Impulses, LFO Shapes, Curves, Clips and
Effect Chains directories. Directory names establish organization, not DSP laws.

### Sound-design implications for original Borato presets

The evidence supports a patch model with four cooperating parts: source
material, playable parameter settings, time-varying modulation, and resources
plus compatibility state. The following are original design choices for Borato,
not recovered Serum recipes:

| Role | Construction and review criterion |
|---|---|
| Bass | Keep a stable low-frequency component, restrict stereo detune in the bass body, and use a separate brighter layer for movement. Check low notes and mono output. |
| Lead | Give the attack an identifiable harmonic contour; make brightness and motion macros useful over their full travel. Check upper notes, repeated attacks and bend. |
| Pad | Combine slow envelope motion with independent spectral movement and controlled spatial effects. Check chord buildup, release tails and mono cancellation. |
| Pluck / keys | Separate the short bright transient from the sustaining body, with velocity shaping articulation. Check low velocity and rapidly repeated notes. |
| Rhythmic / effects | Make modulation periodicity and envelope behavior intentional; check that resonance and effects remain bounded at macro extremes. |
| Init | Keep a neutral predictable starting point, with deliberately assigned macros rather than accidental residual modulation. |

Each recipe needs intentional A/B balance, articulation, filter behavior,
modulation routing, macro names and depths, and effects sends. A shared category
template can provide a starting point but cannot by itself establish distinct
musical identities. Serialization tests must retain custom curves and embedded
original waveforms, in addition to numeric parameter values.

### Investigation checklist and remaining limits

- **Answered:** identity of the installed target; existence and key families of
  structured patch state; read-only catalogue/header inventory; wavetable
  resource persistence; current Borato bank count and review boundary.
- **Partially answered:** how Serum assembles a complete patch. The static
  evidence establishes subsystem/state structure, not every field or processing
  rule. Header tags describe catalogue content, not audible contribution.
- **Unresolved:** complete compressed preset format; every synthesis/filter/FX
  law; individual authors' creative techniques; perceptual equivalence to Serum.
- **Requires separate listening evidence:** identical MIDI/audio comparison of
  selected reference patches at matched level. Numerical Borato tests cannot
  demonstrate equal musical quality or identical timbre.

No proprietary patch payload, waveform or decompiled implementation is included
in Borato's source or new recipes. This report provides an architectural and
sound-design reference, not a Serum preset importer or a claim to have discovered
all sound-design techniques.

## Factory revision: 2026-10-19

All 65 recipes were reviewed and revised in place; [PRESET_REVISION.md](PRESET_REVISION.md)
records the per-patch design intent. The early four musical presets now have complete
macro assignments; Mono-tagged patches actually select mono mode; all musical patches
have explicit timbral velocity response. Names, order, parameter IDs and state format
remain stable. Previously saved full states retain their values and resources but may
no longer match a factory recipe by name.

The new full-bank performance sweep found overloads that the old default-setting
render did not cover. Gains were reduced only for these nine patches, targeting a
measured scenario peak below 0.80 (approximately -1.94 dBFS), without an added limiter:

| Patch | Gain reduction |
|---|---:|
| Glacier Hall | 5.3 dB |
| Dust Choir | 1.9 dB |
| Aurora Strings | 2.1 dB |
| Marimba Grain | 2.1 dB |
| Digital Rain | 3.5 dB |
| Chord Stab | 1.8 dB |
| Rising Tide | 3.6 dB |
| Deep Space Drone | 9.2 dB |
| FM Bell Tower | 6.4 dB |

The sweep combines three rates (44.1/48/96 kHz), three velocities (.25/1/.65)
and simultaneous macro settings (0/1/.5). This is three scenarios per patch,
not the Cartesian product or a guarantee for arbitrary performances. The older
16-second default render also checks loudness and both stereo tails. The bank test
checks metadata/ranges/routes, host-state round trips and, when given an output
directory, portable file round trips and WAV exports. Individual audible macro
response is separately checked for the 12 Atelier patches. Static routing presence
alone does not prove each macro's audibility throughout its travel.

### Completed validation

- Debug `test_presets`: all 65 passed metadata, ranges/routes, factory identity,
  host/file round trips, 195 short performance scenarios and 65 long default renders,
  plus envelope and macro-FX regressions. Log: `build-ui/preset-renovation-final.log`.
- Debug `test_atelier`: all 12 passed individual macro, resource, state and audio checks.
  Log: `build-ui/preset-renovation-atelier.log`. Subsequent gain changes affect only
  non-Atelier patches, verified by the final full-bank run.
- Debug `test_interface`: browser selection, MIDI, automation, state migration,
  table switching and UI checks passed. Log: `build-ui/preset-renovation-interface-v2.log`.
  Glass-current expectations were updated to its revised recipe.
- Release Standalone and VST3 rebuilt successfully after gain calibration.
  Log: `build-ui/preset-renovation-release-final.log`.
- `build-ui/preset-renovation-bank` contains 65 `.bwpreset` files and 65 WAV previews.
  These bank WAVs retain rendered patch gain; unlike the separate Atelier wet/dry
  listening copies, they are not loudness-normalized.

No new DSP callback implementation was introduced. Listening on a physical device,
DAW reopening and a controlled Serum/Borato audio comparison remain unperformed;
the results establish numerical/integration behavior, not equal subjective quality.

## Historical evidence and limits: 2026-10-17

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

## Historical implementation: Atelier addition, 2026-10-17

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
