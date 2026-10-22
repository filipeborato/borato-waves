# Sample playback and key zones

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
are 24 (Sample Level), 25 (Sample Pan), and 26 (Sample Pitch). Global effect
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
or inverted ranges are rejected and highlighted. The keyboard bar shows mapped
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
Portable `SAMPLE_BANK` version 1 embeds each distinct shared asset once, with
backward references from other zones. It validates ranges, roots, references,
version, PCM size, finite samples and hashes before publication. Legacy SAMPLE
states remain loadable and are exposed as an equivalent full-range zone by the
bank API; they retain the existing single-sample playback/automation path.

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

The measured stationary residual was approximately -110 dB relative to the
analytic reference, with approximately -107 dB stopband rejection on these
fixtures. A local Windows Debug microbenchmark of 16,384 mono reads measured
about 0.9 ms for Hermite and 97 ms for sinc at increment 4. At a fixed increment the work per read is independent of output rate; the
measured sinc cost projects to approximately 261/284/568 ms of CPU per second
for one mono reader at 44.1/48/96 kHz. Debug timing is not
a production CPU guarantee: the sinc reader intentionally trades additional
work for rejection. No native macOS build or listening test was performed for
this change.
