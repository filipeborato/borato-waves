# Sample playback, velocity layers and granular synthesis

## Filtered transposition

`Sample::readFilteredStereo` evaluates a symmetric Blackman-windowed sinc with
32 zero crossings on each side and 1,024 tabulated fractional phases. Linear
interpolation between phases avoids coefficient steps. The table is immutable
and initialized before processing starts.

For source increment `r`, the kernel uses `s = max(1, r)`, weights
`sinc((position - frame) / s) * Blackman((position - frame) / (32*s))`, and
normalizes their sum. Support grows with `s`, while cutoff contracts by `1/s`.
Both stereo channels use the same weights. Buffer edges extend the nearest
sample; loop boundaries retain the existing 10 ms overlap fade. Start/end
changes apply on the next wrap without clamping the current head to the newly
selected region. The reader is shared by single samples and mapped zones.

This is a centered reader of already loaded audio: it adds no host latency or
phase delay. It needs future samples from the asset and can exhibit pre-ringing.
Useful passband tests exclude the Nyquist transition. It is not universally
alias-free, particularly under rapid time-varying modulation or at boundaries.

Work is bounded to 4,096 taps per read. As protection against extreme root-note
and source-rate combinations, increments between 48 and 64 fade to silence;
increments of 64 or higher are silent while the head continues advancing.
Normal -24 to +24 semitone transposition at tested rates is below this guard.
A mipmapped source would be needed to extend extreme ratios economically.

## Modulation

Existing destination IDs 0 through 23 are unchanged. The appended destinations
are 24 (Sample Level), 25 (Sample Pan), and 26 (Sample Pitch), followed by
27 (Grain Position), 28 (Grain Size), 29 (Grain Density), and 30 (Grain Pitch). Global effect
destinations remain exactly 18 through 20. The original ticket's proposed
26/27/28 numbering did not match the existing implementation.

Level, pan and pitch use the existing per-voice modulation and smoothing path.
A zero effective level keeps the read head advancing. Disabling SAMPLE may
pause it after its enable ramp. Start/end are not modulation destinations.
The Sample page accepts modulation drag-and-drop and displays modulation arcs.

## Mapping samples

Load a sample, then use **ADD ZONE** to split the selected key range in half.
Both zones initially share the audio. Select the new zone and use **LOAD SAMPLE**
to replace only its audio through the existing cancellable background loader.
Edit LOW, HIGH and ROOT as MIDI note numbers. Ranges are inclusive; overlapping
or inverted ranges are rejected and highlighted, except identical key ranges
with separate velocity layers. The keyboard bar shows mapped
ranges and the most recent voice's zone. Unmapped notes produce no sample audio.

Banks support up to 128 zones. Each zone owns its root, start/end and loop
metadata. Adding zones captures the current single-sample region and loop
settings. Global root/start/end/loop controls are disabled in mapped mode;
ROOT in the mapping row edits the selected zone. CLEAR returns to the empty
single-sample source. Loading via the legacy `loadSample` API also returns to
single-sample mode, preserving existing root/region automation semantics.

Zones are selected on note-on and at the next render segment after legato
retargeting. A short linear crossfade keeps both readers alive; subsequent swaps
wait until the current fade finishes. Immutable banks use the same generation
acknowledgment mechanism as imported samples. No bank ownership operations,
allocation, file access or new locks occur in the audio callback.

The 256 MiB asset budget counts unique sample buffers across current banks,
retired banks, pending decodes and undo history. Bank edits participate in undo.
Portable `SAMPLE_BANK` version 2 embeds each distinct shared asset once, with
backward references across zones and round-robin slots. Version 1 remains readable
with full velocity ranges and one variant. It validates ranges, roots, references,
version, PCM size, finite samples and hashes before publication. Legacy SAMPLE
states remain loadable and are exposed as an equivalent full-range zone by the
bank API; they retain the existing single-sample playback/automation path.

## Velocity layers and round robin

**ADD LAYER** splits the selected velocity interval; **ADD ZONE** splits the key
range of all its velocity layers together. Velocity bounds are inclusive 1..127,
with crossfade widths 0..63. Base intervals cannot overlap; expanded crossfade
intervals allow at most two layers. Crossing layers share a crossfade law.

Weights are selected from MIDI velocity at note-on, including legato retargeting,
and are held during the note. Explicit bank edits can update the selection.
Velocity zero is note-off. Linear weights are `1-t, t`, preserving the gain of
identical in-phase audio. Equal Power uses `cos(pi*t/2), sin(pi*t/2)`, preserving
energy for uncorrelated audio; correlated layers can therefore gain up to 3 dB.

Each layer has 1..4 round-robin slots. LOAD SAMPLE and RELINK affect the selected
slot. Missing slots remain in the sequence and produce silence. Only note-on
with positive layer weight advances that zone's sequential counter, starting
at RR 1. Note-off, sustain, unrelated zones and mono return to a held note do
not advance it. Prepare, all-notes-off and state restoration reset counters;
restoration applies on the next audio block. No random seed affects round robin.
The UI displays the last triggered slot. Mapping edits support undo/redo.

## Granular synthesis

Select **Granular** with MODE at the top of the Sample page. Grains need a loaded
sample and SAMPLE ON; the status line says which is missing. The sample (or its
grains) is one more source: it sums with OSC A, OSC B, SUB and NOISE into its
route, so the oscillators still sound unless switched off. The OSC A / OSC B
toggles on the Sample page and the SOUNDING line show what is heard.

Defaults are 40 grains/s and 100 ms, giving four nominal overlapping grains.
Controls (GRAINS page) are density 1..100 grains/s, size 5..500 ms, position 0..1,
position spread 0..1 (default 0.05), scan -2..2 times source speed, pitch -24..24
semitones, timing jitter 0..100 % (default 20 %), pan spread 0..100 % (default 0)
and integer seed 0..65535. POSITION is drawn on the waveform with its spread band;
click or drag there to set it. Old states keep their saved values; their jitter
and pan read the defaults. Controls that cannot affect the sound in the current
mode are dimmed: the loop window, crossfade, fades and direction in Granular and
Spectral, and on the TIMING page whatever the density and length modes ignore.

Each voice has one static pool of 32 grains, including both velocity layers:
16 voices are capped at 512 grains. A full pool refuses the new emission without
stealing an audible grain. The UI shows occupancy, expected density-times-size,
and a saturation flag retained until the voice resets. Existing grains retain
their source assets through a sample swap and drain when granular mode is disabled.

Grains use a 1,025-entry interpolated Hann window and the filtered sample reader.
The mix is divided by `sqrt(max(1, sum of active Hann windows))`: overlapping
grains of real material are decorrelated, so the cloud level holds within a
fraction of a dB from 2 to 10 overlaps; perfectly correlated input (DC) rises by
the square root of the window sum, about +12 dB for 32 grains. Sparse clouds
keep their individual envelopes. Without G-LOOP a grain is moved back to end
inside the region, and shortened when the region is shorter than the grain, so
no grain holds the last frame. Scan wraps the emission cursor independently of
pitch; position spread clamps to the region. Jitter delays each grain by a random
[0, jitter x event interval) after its event; pan spread gives each grain a random
equal-power pan. Both draw from the voice seed and are zero-cost when zero.

Position, size and source selection are captured at birth. Density sets the next
emission interval; changes do not reschedule an interval already underway.
Pitch, sample level and pan continue to modulate active grains. Scan and spread
affect future birth positions. Seed applies on the next note, using SplitMix64
with voice index and note serial. Scheduled attempts consume RNG even at capacity.
The fractional sample scheduler is independent of audio block boundaries.

Grain markers use fixed atomic snapshots published once per block, with no new
audio-thread locks or heap operations. Portable state includes assets, mappings,
variants, parameters and routes; it restarts the deterministic sequence rather
than storing live grain positions. The 256 MiB budget includes every owned variant.

### Granular timing (TIMING page)

These are Borato contracts, not claims about any other synthesizer's laws.
DENSITY: Free Hz (the density control, 1..100/s), BPM Sync (one event per
division, `EVERY`) or Target Overlap N (1..32): event rate = N / (length x
grains per event), clamped to 1/64..1000 events/s and to the 32-grain pool.
LENGTH: Free ms (5..500), BPM Sync (`FOR`, the division's duration) or Percent
(1..400 %) of the interval between events; with Target Overlap, Percent measures
the Free density's interval, since the overlap rate depends on the length.
Lengths are then key tracked, `x 2^(-(note - root)/12 x KEY TRACK)`, and clamped
to 5..2000 ms. Divisions are the LFO ones: dotted lengthens by 1.5 (rate x 2/3),
triplet shortens to 2/3 (rate x 1.5). Tempo is the host's, else the internal BPM.

With the host transport playing, synced events fire when the host ppq crosses a
division line (a seek that crosses one fires at once); otherwise, and in the
other modes, events count from the note at the current tempo. PER EVENT emits
1..8 grains spread over the event interval: Together (all at the event), Even
(k/M), Exponential ((2^k - 1)/(2^M - 1), a finite geometric spacing, not random
exponential timing) or Random (uniform, from the voice seed). JUMP pre-populates
round(event rate x grains per event x length) grains, at most 32, with ages spread
across one length, so a sustained cloud sounds from the first sample.
G-LOOP wraps a grain inside the region instead of fitting it before the end. A full
pool keeps refusing new grains, including scheduled batch grains. The labels show
the resolved events/s, length and expected occupancy at the root note. With
jitter at zero the timing defaults reproduce the original scheduling exactly.

## Region, loop window and direction

START/END bound the playable region. With LOOP on, an inner window LOOP START/END
loops; with LINK on (default) its points are fractions of the region and move
with it, with LINK off they are fractions of the file, clamped into the region.
Crossed points are reordered and the window is widened to at least 64 frames.
Defaults (window 0..1, linked, Forward, 10 ms crossfade, 10 ms fade-out, no
fade-in) reproduce the original whole-region loop, so old states play as before.

LOOP modes: Forward wraps from the window end to its start; the last crossfade
frames blend into the frames one period earlier (period = window - crossfade),
so the wrap never steps and reads stay inside the window. Reverse plays forward
until the window end, bounces once, then loops backwards with the mirrored
crossfade. Ping-Pong bounces at both ends (continuous position, no crossfade).
Tailed loops forwards while the note is held, sustain pedal included; the release
is checked when the head enters the crossfade (or reaches the window end with no
crossfade): once released it plays on to the region end, fades out and stops, so
it can never stay stuck in the loop. A release inside the crossfade finishes that
wrap first. The ADSR release runs independently; Tailed does not lengthen it.

DIR Reverse mirrors the whole region (it starts at END and plays towards START);
the loop window is mirrored with it. Direction is fixed at note-on. FADE IN ramps
the first frames played; FADE OUT applies before the region end when not looping
(or in the tail). Start/end/loop edits take effect at the next wrap, bounce or
note, never jumping the head. Zones store their own window, mode, link and
direction in the portable bank (older banks load the original forward loop);
crossfade and edge fades are global. Granular and Spectral modes keep using the
region and the LOOP toggle only.

## Manual slices

SLICE turns the current sample into slices: markers (frames) start each slice,
the first is always frame 0, and slice i plays on note ROOT + i. SINGLE SLICE
stops at the next marker (one-shot fade-out); PLAY TO END runs on to the end of
the file. Double-click the waveform to add a marker, drag one to move it (one
undo step per drag), right-click to remove it. Markers past the file end, within
64 frames of the end, or closer than 64 frames to a kept marker are dropped;
maps that would pass MIDI note 127 are refused and the current map stays. At most
128 slices. Notes outside the slices are silent.

Slices are ordinary zones of one bank that all reference the same sample, so the
audio is held and saved once; the bank also stores the markers, root and mode
(SLICES node) to edit them again, with undo/redo ("SLICE EDIT"). Editing zones by
hand turns the bank into plain zones. UNSLICE returns to the single sample. A
slice edit under held notes keeps the old bank alive until the voices let go,
with the usual short zone crossfade. Automatic slicing by transient threshold is
not included.

## SFZ import (subset)

LOAD SAMPLE also accepts `.sfz`; it is parsed and its samples decoded on the
sample worker (progress, cancellation by a newer load), then published as a
bank. Export is the portable Borato bank; SFZ export is not offered.

Supported headers: `<control>` (only `default_path`), `<global>`, `<master>`,
`<group>`, `<region>`. Precedence is global < master < group < region; a header
resets the levels below it (`<master>` clears group, `<global>` clears both).
Supported opcodes: `sample`, `default_path`, `key` (sets lokey, hikey and
pitch_keycenter; explicit ones override it), `lokey`, `hikey`, `pitch_keycenter`,
`lovel`, `hivel`, `loop_mode`, `loop_start`, `loop_end`, `offset`, `end`, `tune`
(-100..100 cents), `pan` (-100..100, equal power), `volume` (-144..6 dB). Notes
are numbers 0..127 or names with c4 = 60 (`#` and `b` accidentals, octave -1..9).
`end` and `loop_end` are inclusive frames. `loop_continuous` maps to a Forward
loop, `loop_sustain` to Tailed; `no_loop` and `one_shot` play once (`one_shot`
still ends with the envelope release, which is reported). A loop without points
loops the whole region (reported).

Diagnostics are never silent: other headers (with their opcodes), other opcodes,
`#define`/`#include`, malformed or out-of-range values, regions without `sample`,
and paths that leave the `.sfz` folder (absolute or `..`) are listed; the UI
shows the summary. Missing files become missing zones (silent, listed as
MISSING, relinkable like other zones). Overlapping regions other than two
velocity layers on the same key range are refused with an explanation rather
than imported partially. Limits: 1 MiB of text, 128 regions (extra dropped and
reported), the 256 MiB budget across every decoded file; each referenced file
is decoded once and shared between regions. Values end at the next opcode,
header or line end, so sample paths may contain spaces. Synchronous import
(`importSfz`) is one undo step; the background import publishes like a sample
load. No file I/O happens on the audio thread.

## Spectral resynthesis

Select **Spectral** in the Sample page MODE (appended after Granular; saved states
keep their index, but a host automation lane written against the two-choice
parameter maps its normalised value differently). The current sample, or zone 1
of a bank, is analysed on the sample worker: STFT, FFT 2048, hop 512 (4x overlap),
periodic root-Hann analysis and synthesis windows. The overlapped squared window
sums to exactly 2, so overlap-add scaled by 0.5 is the identity. The input is
delayed by 1536 zero frames (reads past the end are zeros), so the first and
last frames receive all four overlaps. A voice primes three frames at note-on,
so the engine adds **0 samples** of latency and reports none to the host.

Storage per frame and bin is magnitude and phase per channel, plus for stereo one
reference phase (the stronger of L+R and L-R in that bin): 8 bytes per bin mono,
20 stereo, about 16 (mono) or 40 (stereo) bytes per source frame. The estimate is
checked against the 256 MiB aggregate budget before any allocation; a refusal is
shown as REFUSED with the required size and the previous analysis stays playing.
Analyses are published only when complete, with progress (ANALYZING n%) and
cooperative cancellation by newer requests, sample loads and shutdown. MISSING
means Spectral mode without a sample. Leaving Spectral mode cancels work and
releases the analysis once voices let go; returning reanalyses.

Controls: SPECTRAL POS 0..1 (note-on start within the sample region; the held
frame in FREEZE), FREEZE, SPEED 0.5..2x and SPECTRAL PITCH -12..+12 st, plus the
sample's own pitch, key tracking against the root note, region, and loop. All
four continuous controls are 20 ms smoothed and appended matrix destinations
(Spectral Pos 31, Spectral Speed 32, Spectral Pitch 33); FFT size and hop are not
automatable. A cursor shows the display voice's read position on the waveform.

Phase policy: one accumulator per output bin, shared by both channels, advanced
by the reference's instantaneous frequency (unwrapped phase difference between
adjacent frames, linearly interpolated between frames and bins). Each channel's
bin takes the accumulator plus its analysed phase relative to the reference, so
freeze, scrub and speed changes never restart phases and stereo keeps the
analysed L/R relation. At unit speed and pitch on integer frames the accumulator
telescopes to the analysed phase: playback is the identity reconstruction.
Pitch shifts each spectral peak's region (peaks on summed channel magnitudes,
regions split at midpoints) by whole bins, rounded from the peak's measured
frequency, with phases locked to the peak (Laroche and Dolson). Content shifted
past Nyquist is dropped. Speed only moves the analysis read position.

Known limits: transients smear when stretched (no transient detection or phase
reset in this version), formants move with pitch, rounding the shift to whole
bins leaves a residual within-frame frequency error of up to half a bin, the
shifted frequency trajectory of a sweep trails by about half a hop (256 samples,
so a 300 Hz/s output sweep reads 2 to 4.5 cents flat against the unaligned
target and under 2 cents once aligned), and frames where a bin's reference switches
between L+R and L-R give that bin a wrong frequency while frozen there. The
analysis is a cache: states save the audio and `spectralAlgorithmVersion`, never
the analysis, so states from another version are simply reanalysed.

## Verification

Run the Debug `test_sample` target (assertions must remain enabled). It covers:

- 44.1/48/96 kHz outputs, 48 kHz source, -24/-12/-5/0/+7/+12/+19/+24 semitones;
- multitone analytic bandlimited reference, residual FFT, passband gain,
  above-Nyquist rejection, chirp with continuously changing increment;
- impulse alignment, finite edge/extreme-ratio reads, Hermite timing comparison;
- preserved destination IDs, zero-depth null, bipolar limits, mute/head continuity,
  rapid macro automation and state restoration;
- zone boundary notes 36/59/60/72, outside-map silence, legato crossfade,
  retired-bank retention, undo, 128-zone cap, overlap rejection and 256 MiB refusal;
- portable roundtrip after deleting original files, shared-buffer references,
  malformed references, UI route binding and key-range edits;
- existing sample regression suite, including callback heap-operation checks.

The layer/granular checks additionally cover all 127 velocities (linear null and
equal-power RMS variation approximately 0.00000026 dB on orthogonal fixtures),
100 round-robin triggers, missing variants, reset reproducibility, and UI undo,
relink and modulation routing. DC ripple is below 0.1 dB at 44.1/48/96 kHz.
Pool tests reach exactly 32/512 grains without callback allocation or saturation
gain jumps. Ten-second density measurements meet 1% accuracy. Engine output and
spawn timing are bit-identical across 37/64/128/512-frame blocks. Processor
roundtrips meet -90 dBFS RMS error; opening the editor leaves audio bit-identical.
Portable playback is tested after deleting the original source file.

These are local Windows Debug checks, not a real-time CPU qualification. Dense
cloud cost scales with grain count, velocity layers and transposition ratio.
Release profiling, native macOS/Linux builds and live host listening remain
necessary before claiming production polyphony at every supported sample rate.

The measured stationary residual was approximately -110 dB relative to the
analytic reference, with approximately -107 dB stopband rejection on these
fixtures. A local Windows Debug microbenchmark of 16,384 mono reads measured
about 0.9 ms for Hermite and 97 ms for sinc at increment 4. At a fixed increment the work per read is independent of output rate; the
measured sinc cost projects to approximately 261/284/568 ms of CPU per second
for one mono reader at 44.1/48/96 kHz. Debug timing is not
a production CPU guarantee: the sinc reader intentionally trades additional
work for rejection. No native macOS build or listening test was performed for
this change.
