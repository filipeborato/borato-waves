# Voices, mono/legato and warp

## Voice transitions

JUCE's `Synthesiser` owns sustain pedal (CC64), note-off and all-notes-off (CC123): notes tail off through the amplitude release. Stealing (17th note) hard-stops a voice; `WaveVoice` hands its last output sample to a 5 ms linear fade, so a steal never steps the output. `test_voices` covers pedal held/released, keys still down at pedal-up, all-notes-off with the pedal down, steals at 24 different phases and a 1500-event seeded MIDI fuzz per voice mode that must end in exact silence.

## Mono, legato and glide

`voiceMode` (Poly/Mono/Legato) and `glide` (0–2 s) are appended parameters; older states load as Poly with no glide. Poly ignores glide.

Mono and Legato play one voice with **last-note priority**: releasing the sounding key returns to the most recent key still down; releasing any other key changes nothing.

| Situation | Mono | Legato |
|---|---|---|
| New key while the note is gated (key or pedal) | retrigger envelopes, glide | same envelopes, glide |
| New key while the note is only releasing | retrigger, glide | retrigger, no glide |
| New key from silence | retrigger, no glide | retrigger, no glide |

The sustain pedal holds the gate: with no keys down, pedal-up releases the note, and a key played while the pedal holds a note is legato. Glide is a constant-time linear ramp in semitones, so its time does not depend on the interval. Changing the mode releases all sounding notes (tail-off) and forgets held keys; press them again. The sustain pedal state is kept across mode changes.

## Phase warp (A/B)

Each oscillator has `oscXWarpOn` and a bipolar `oscXWarp` (−1…1), also matrix destinations **A Warp** (21) and **B Warp** (22). They were appended after the three global FX destinations so saved route indices keep their meaning. With `u = 2p − 1`, the read phase is bent to `p − (w/2)·u·(1 − |u|)`, a parabolic stand-in for `w·sin(2πp)/2π` that costs a few multiplies instead of a `sin` per lane per sample. Its slope stays within `1 ± w`, so the map is monotonic, and the cycle start/end and period stay fixed: tuning and wrap continuity hold. Positive values compress the start of the cycle; negative values compress its middle. Because the bend can raise the top harmonic by up to `1 + |w|` (Carson's rule), each lane reads the band-limited level for `frequency × (1 + |w|)`.

Measured in `test_voices`: tuning within 0.5% from MIDI 28 to 96 at 44.1/48/96 kHz; inharmonic energy under −40 dB (or at most 10× the dry table) from MIDI 84 to 103; a 40 Hz full-range LFO sweep steps no harder than either static extreme. Level change at warp 1 on note 48 is asserted within ±3 dB; measured −1.2 dB (sine), +1.4 dB (saw), −0.3 dB (square). A square barely changes, because its edges sit on the bend's fixed points (0 and ½). The warp adds no gain compensation, because the change depends on the waveform; match levels by ear when comparing dry and warped patches.

CPU, measured with `test_modulation --benchmark` (Release, Ryzen 7 5700X, 128 samples, one run): `extreme_no_routes` (16 notes, eight A/B lanes) takes 26.8 / 29.5 / 70.4% of the budget at 44.1 / 48 / 96 kHz. `extreme_warp` (the same patch with both warps on) takes 34.0 / 38.1 / 90.2%, with 149 of 750 blocks over the deadline at 96 kHz. So warp adds about 27% to the oscillator cost. An extreme warped patch at 96 kHz fails the provisional 70% gate. An earlier `std::sin` version cost 47.8% at 44.1 kHz.

## Pending manual checks

Playing bass/lead phrases and listening for clicks in Standalone and a VST3 host has not been done in this pass.
