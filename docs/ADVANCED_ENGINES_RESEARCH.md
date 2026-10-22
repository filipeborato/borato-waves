# Advanced sample-based engines: shipped-artifact evidence

Investigated 2026-10-19 for original Borato Wave implementations and implementation tickets.

## Identity, method and confidence

Target: `C:/Program Files/Common Files/VST3/Serum2.vst3`, SHA-256
`7978c9be5b2107e985c24c174000faee87e11483ec989d81dc61b5829b45ed70`.
The hash was checked during this session; saved-project identity was previously
verified in `build-ui/rea-presets/identity.log`.

REA's connected Ghidra provider lacks `GHIDRA_INSTALL_DIR`. Existing Ghidra
12.1.4 was therefore used directly on the matching saved project
`C:/Users/filipe/projects/rea/ghidra/Serum2`, with
`-process Serum2.vst3 -noanalysis -readOnly`. No Serum execution, installation
changes or proprietary resource import into Borato occurred.

Evidence is retained in `build-ui/rea-advanced/`: `inventory.txt` contains
vtable slots and string references; `parameter-strings.txt` contains full enum
strings; address-named `.c` files contain focused decompiler output. Input lists
and `analysis.log`, `followup.log`, `final.log` make the three passes repeatable.
All three passes exited successfully; 18 focused functions were decompiled.
`sha256.csv` records hashes of the retained analysis files.
These are **local evidence identifiers, not REA Evidence IDs**. Decompiler
warnings include incomplete type recovery and removed unreachable blocks.

**Observed** means bytes, strings or operations actually present in static
analysis. **Inferred** means an interpretation of that evidence. **Design**
means an original Borato choice. Static evidence does not demonstrate runtime
sound, performance or equivalence. Enum names prove available named concepts;
they do not establish every audible response law.

## Shared source and state model

Observed vtables separate immutable/configuration state from voice renderers:

| Engine | State serializer | State loader | Renderer slot 7 |
|---|---|---|---|
| Sample | `1802ea7a0` | `1802ea990` | `1803c13c0` |
| Granular | `1802ea7a0` | `1802ea990` | `1803c66d0` |
| Multisample | `1803221c0` | `180323f60` | `1803bc8a0` |
| Spectral | `1802effe0` | `1802f0b00` | `1803cea10` |

The shared Sample/Granular serializer calls `1802a5ed0`. That helper explicitly
writes `samplePathRelative`, `samplePath`, `numChannels`, `numFrames`,
`sampleRate`, `baseNote`, `baseDetune`, `trimHead`, `trimTail`, `edgeFadeMs`,
`reversed`, and `embedded_audio` with a `data` member. This supports retaining
source identity, tuning and edits with a patch. Exact binary/audio encoding and
the defaults/units of every field remain unverified.

Common oscillator enum registration at `1802ae880` names:

- Loop modes: `kNone`, `kForward`, `kReverse`, `kPingPong`, `kTailed`.
- Start/end, reverse, scan rate and BPM scan rate, position, loop start/end,
  crossfade, loop linking, and separate single-slice/play-to-end controls.
- Key-zone min/max/fold/warp and velocity-zone min/max; slicing enable,
  auto-slice threshold and slicing root note; loop end at release; manual
  position, wrap position, scan range/tempo lock/base tempo.

`OscState` serializer `18029fb10` references `sliceMarkers`. These are shared
oscillator controls; this does **not** prove that every type uses every control
identically. Direction changes, release-tail transitions and slice-boundary
rules require focused runtime tests.

## Sample engine: extend the working Borato path

Observed Serum renderer `1803c13c0` dispatches sample helpers including
`1803c1910`, `1803c34a0`, `1803c3f10`, `1803c43b0`, `1803c5190`; their complete
interpolation and loop algorithms were not recovered in this pass. Shared
state and enums above establish a materially richer sample model than a single
pitched one-shot. Do not infer time-stretch quality from the name “scan rate”.

Borato already has an implemented source in `Source/Sample.h` and
`Source/PluginProcessor.cpp`: mono mixdown on import, immutable samples,
Catmull-Rom/Hermite interpolation, fixed root MIDI 60, pitch/sample-rate
tracking, a minimum 64-frame region, one-shot fade and forward-loop crossfade
up to 10 ms, plus replacement crossfading. Existing host controls are
`sampleOn`, `sampleLevel`, `samplePitch`, `sampleStart`, `sampleEnd`, `sampleLoop`.
This is an extension task, not a missing engine from zero.

**Design:** preserve old parameter IDs and old mono/root-60 session behavior;
introduce explicit stereo buffers, root-note/fine-tuning metadata, region vs
loop bounds, direction and release policy, and saved slice markers. Use an
immutable asset plus per-voice transport/read heads. Define channel downmix and
missing-resource behavior explicitly. Implement forward/one-shot first, then
reverse/pingpong/release tails with bounded crossfades.

**Acceptance:** impulses and ramps at each loop boundary; tiny/invalid regions;
forward/reverse index bounds; stereo identity; pitch ratios at 44.1/48/96 kHz;
source replacement under sustained notes; state round trips retaining markers
and tuning; old session compatibility. An anti-aliasing quality requirement for
pitched sample playback must be specified rather than assuming Hermite provides it.

## Granular engine

Observed enum registration `1802eb340` exposes independent density and duration:

| Domain | Exact labels |
|---|---|
| Density | `kDensityFree`, `kDensityBPM`, `kDensityGrains` |
| Length | `kLengthFree`, `kLengthBPM`, `kLengthPercent` |
| Trigger distribution | `kTogether`, `kEven`, `kExponential`, `kRandom` |
| Window | `kWindowHann`, `kWindowWelch`, `kWindowGaussian`, `kWindowBlackmanHarris`, `kWindowSinc`, `kWindowTukey`, `kWindowTriangle`, `kWindowTrapezoid`, `kWindowExpDec`, `kWindowExpDecRev` |

Further names include maximum grains, jump-start grains, loop grains,
length keytracking, dotted/triplet settings for length and density, reverse,
window amount/skew, two warp slots, and random offset/direction/pitch/length/
pan/gain/window amount/window skew/warp. These are exact named capabilities;
their unit conversions and probability distributions are not recovered.

`1803cdb90` has ten switch branches. Several remap the input phase in two
pieces around a third parameter, while others have distinct branching. It is
called by the granular renderer and granular helper/UI functions. This
corroborates a window evaluator with an adjustable shape/skew, but decompiled
register arguments are imperfect: no formula is asserted here for all shapes,
especially endpoint behavior at zero/one skew.

`1803c66d0` iterates grain-like records with a stride of 800 bytes, checks
activity flags, invokes window/sample helpers, and calls `1803c8e60` when
starting a record. A byte-indexed allocation search is bounded near 256 records.
**Inference:** a bounded reusable grain pool rather than allocating each grain.
This is not proof that 256 is the public maximum under every mode. The start
helper `1803c8e60` prepares record state and delegates to `1803cc560`,
`1803cc800`, `1803cca60`; parameter preparation is `1803c74c0`.

**Design:** use a preallocated bounded grain pool, deterministic seeded RNG,
sample-accurate launch accumulator and per-grain read position, increment,
window phase/duration, gain and stereo pan. Source scanning and per-grain pitch
must be independent. Specify density as grains/second first; BPM and target
overlap are explicit conversions. Select and document an original normalization
law: constant power, constant peak or density compensation are different goals.
Begin with a mathematically defined Hann window and bounded skew; add other
windows only with reference tests. Do not transplant the recovered pseudocode.

**Acceptance:** identical launch counts across host block partitions; bounded
CPU at pool saturation with defined stealing/drop policy; deterministic seed;
zero/near-zero density; overlapping grains and loudness; source-edge behavior;
pitch/scan independence; sample-rate and tempo changes; no callback allocation,
locking, file I/O or final ownership destruction; complete state round trips.

## Multisample engine

`1803221c0` serializes `sfzPath`, `sfzPathRelative`, `embedded_audio`, channel/
frame/rate metadata, and `loopStart`/`loopEnd`. `180323f60` loader references
`libsfz::Sample` callbacks in the inventory. This establishes SFZ-backed sample
resources with optional embedded audio. It does not establish a complete list
of supported SFZ opcodes or full SFZ interoperability.

Renderer `1803bc8a0` prepares with `1803bd6f0`, loops multiple active sample
records (stride `0x250`), sums their results through `1803c02e0`/`1803c0a80`, and
contains separate transition-count handling. This supports layered sample
playback, but exact key/velocity zone precedence, round robin, release triggers,
and crossfade selection laws remain unresolved. Common key/velocity-zone
controls alone cannot establish multisample-region selection semantics.

**Design:** define an original `SampleMap` with immutable regions containing
key/velocity bounds, root/fine tune, gain/pan and loop data; select all matching
regions with an explicit deterministic overlap policy. Precompute key lookup
on the control thread and bound voices/layers. Reuse the stereo sample reader.
Start with a native versioned map format; an SFZ subset importer is a separately
scoped adapter with explicit supported opcodes and visible diagnostics.

**Acceptance:** every key/velocity boundary including 0/127, overlapping and
missing zones, root tuning, layered headroom, missing assets, malformed paths,
out-of-range loops, portable embedding, memory budget and replacement while
voices sustain. Round robin/release triggers are separate features until their
semantics and tests are specified.

## Spectral engine

`1802effe0` stores `embeddedFilterTableData`, filter table path/relative path,
channel/frame counts and sample rate. Enum registration `1802f4110` names two
warp slots, spectral-filter shift/wet-dry, low/high frequency, phase lock,
transients, pre/post low/high handling and smoothing. Warp names include
detune, smear, spread, harmonic/subharmonic additions, gate, robotize, spectral
shift/mirror, peak operations, Shepard/comb, pitch shift, phase twist, formant
shift, mask and vocode routing. Names do not establish those algorithms.

The renderer path includes `1803cea10`; focused transform processing is in
`1803d4210` -> `1803d5820` -> `1803d7f90`. The latter accesses adjacent real/
imaginary-like float pairs, iterates `0x800` pairs, computes sums of squares
and square roots, and scales/interpolates pairs. **Inference:** complex-spectrum
processing, with magnitude-sensitive operations. Different switch cases have
different behavior; no complete warp-to-formula mapping is claimed.

`1803d76d0` and `1802ac6a0` both call `18041cdb0` with different direction
arguments (0 vs 1); the latter additionally scales output by a size-dependent
factor. **Inference:** forward/inverse transform adapters. The underlying FFT
kernel and exact normalization constants were not independently reconstructed.

At the end of **one observed path** in `1803d4210`, after `1802ac6a0`, the code
shifts a buffer by `0x1000` bytes, clears a trailing region, then adds
window-weighted samples across `0x4000` bytes. Interpreting these as float32
gives a 4096-sample synthesis span and 1024-sample shift. **Inference:** windowed
overlap-add with a quarter-frame step on this path. This is not proof of the
engine's universal FFT size, hop, latency or mode-dependent oversampling.
The window table, analysis phase convention, transient reset and phase-lock
laws remain unresolved.

**Design:** implement an original offline STFT asset builder and preallocated
spectral voice resynthesizer. Specify FFT size, hop, analysis/synthesis windows,
normalization, phase accumulation, latency and memory budget explicitly. Prove
identity reconstruction before adding pitch/time independence, freeze, or
spectral filters. Start with one documented phase strategy; transient locking
and formant handling are later measurable increments. JUCE FFT is an available
building block, not evidence that its defaults match Serum.

**Acceptance:** impulse/sine/noise identity reconstruction and null error;
DC/Nyquist behavior; window-sum normalization; block-size independence; latency;
long sustained phase stability; transients; extreme pitch and scan; stereo phase;
bounded multi-voice CPU/memory; cancellation of stale offline analysis; saved
asset/analysis version compatibility. Real-time CPU measurements must include
coincident frame work across voices, not only average throughput.

## Controlled experiments that can falsify the inferences

No runtime comparison was performed in this investigation. Use original test
signals imported into the installed target, fixed MIDI, a recorded version and
sample rate, dry output with unrelated modules disabled, and repeatable renders.
Retain parameter settings and resulting audio/hashes alongside measurements.

| Question | Controlled measurement and falsification |
|---|---|
| Density and duration | Short impulse train or constant source, fixed duration, sweep one density mode at a time. Count envelope onsets across tempo/sample-rate changes. Failure of proportional onset count falsifies a simple grains/second or BPM conversion. |
| Grain gain normalization | Constant or noise source, fixed grain shape/pitch, sweep density and duration separately. Compare peak/RMS vs overlap; linear, square-root and constant trends distinguish candidate laws. Repeat with correlated and decorrelated material because phase cancellation confounds RMS. |
| Window and skew | Isolate one grain on a constant source, remove other envelopes/effects, capture its envelope. Compare peak position, endpoint behavior and integrated power for skew endpoints/midpoint. The ten names alone do not establish the formula or normalization. |
| Grain scheduling | Render repeated notes with the same seed/retrigger settings and change block size/tempo. Compare onset offsets and saturation behavior; changed counts falsify sample-accurate block independence. Seed controls may be unavailable, so separate deterministic from statistical conclusions. |
| Sample loops/slices | Use a numbered impulse/ramp source and marker boundaries, test reverse/pingpong/release. Read actual sample order and crossfade duration; repeated or skipped indices disprove assumed inclusive/exclusive rules. |
| Multisample selection | An original map of distinguishable tones with overlapping key/velocity regions; sweep exact boundaries. Measure which tones appear, their gains and repeat ordering. This falsifies presumed first-match/all-match, crossfade or round-robin rules. |
| Spectral size/hop/phase | Impulse plus bin-aligned/off-bin sines; freeze/scan/pitch sweeps with phase lock and transient settings toggled. Measure latency, periodic artifacts and phase progression. A different periodicity falsifies extrapolating the observed 4096/1024 path to that mode. |
| Spectral reconstruction | Compare dry sample playback with spectral no-warp playback at matched rate/position, align measured latency, then null. Residual/transient behavior falsifies a simple identity STFT or assumed window/phase strategy. |

Record uncertainty from host scheduling, unknown internal modulation and render
randomness. Null failure alone does not identify which algorithm differs.

## Completion boundary for implementation planning

All four engines now have artifact-grounded state/feature evidence and focused
render-function observations. Sample and granular share a sample serialization
path; multisample adds SFZ/layer structure; spectral adds transform processing
and filter-table resources. This is enough to scope original implementations
and explicit validation gates. It is **not** a complete reverse engineering of
all four engines, a claim of identical timbres, or permission to mark runtime
parity and worst-case performance gates complete. Unresolved algorithms above
must remain visible in the tickets and be addressed by experiments or explicit
original design choices.
