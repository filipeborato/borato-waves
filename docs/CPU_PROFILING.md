# CPU profiling for new sound engines

Build with `cmake --build build-ui --config Release --target test_modulation`, then run `build-ui/test_modulation_artefacts/Release/test_modulation.exe --benchmark`. Substitute your configured build directory for `build-ui` if needed. It renders approximately one second per row with a 128-sample block. `light` uses one note and one A lane. `notes16` changes only the note count to 16; `unison8` changes only A's lane count to eight. `layered` uses four notes, two A/B lanes and four routes. `extreme_no_routes` uses 16 notes and eight A/B lanes; `extreme` adds 16 routes; `extreme_fx` adds distortion, delay and reverb. Table changes, Macro 1 and B pitch are automated every 13 blocks. The timer surrounds `processBlock` only; preparation, parameter changes and output checks are excluded.

`budget_pct` is summed processing time divided by rendered audio time. `worst_block_pct` compares the slowest block to its own deadline; `over_budget_blocks` counts blocks slower than that deadline. These are **offline timing observations**, not ASIO glitches or a DAW's CPU meter. They omit device scheduling, GUI, host and other plug-ins. The older figures in [MODULATION.md](MODULATION.md) timed the whole loop and are not directly comparable.

## Reference run

AMD Ryzen 7 5700X (8 cores/16 threads), Windows 11 Pro, MSVC Release/LTO, 128 samples, 17 October 2026. Three one-second runs of each case; ranges below cover the three runs. Full per-run timings, worst blocks and over-deadline counts are in [CPU_PROFILE_RESULTS.csv](CPU_PROFILE_RESULTS.csv). All rows rendered non-silent, finite audio within the test's amplitude bound.

| Profile | 44.1 kHz budget | 48 kHz budget | 96 kHz budget | 96 kHz blocks over deadline |
| --- | ---: | ---: | ---: | ---: |
| light | < 1% | < 1% | ~2% | 0 |
| notes16 | ~12% | ~13% | ~27% | 0 |
| unison8 | ~2% | ~2% | ~4% | 0 |
| layered | ~4% | ~4% | ~9% | 0 |
| extreme_no_routes | 25–26% | ~28% | 66–67% | 0–6 / 750 |
| extreme | 38–39% | 41–42% | 90–91% | 37–49 / 750 |
| extreme_fx | ~39% | ~42% | 92–93% | 80–90 / 750 |

The isolated comparisons show the cost of more notes (`light` → `notes16`), more unison (`light` → `unison8`), modulation (`extreme_no_routes` → `extreme`) and effects (`extreme` → `extreme_fx`) under their stated patches. They do not establish a universal per-note or per-route cost. The extreme 96 kHz workload has little headroom even before another source engine. Over-deadline counts are sensitive to OS scheduling, so repeat measurements on the target machine. As a provisional gate for a new engine or OSC C, require a representative target patch to use at most 70% of the offline single-thread budget across three runs, with no over-deadline blocks; also require a separate dropout-free device/host check. An offline pass does not guarantee real-time performance.

## Device and host check

In both Borato Wave Standalone and REAPER with the Borato Wave VST3, select the same physical audio device, ASIO driver, 128-sample buffer and sample rate. Record driver, device, OS, CPU, host version and plug-in build. Set A/B, unison, matrix routes and effects to match a benchmark profile, then hold the stated note count for at least 30 seconds using a reproducible MIDI clip or input. Repeat at 44.1, 48 and 96 kHz. In REAPER record its real-time CPU/performance meter and any underrun indication; in Standalone listen or record loopback for clicks and note loss. Log each run as `date | app | device/driver | rate | buffer | profile | notes | CPU | underruns/clicks | observations`.

For a close host reproduction, start from a fresh default state. Set attack/decay
to 1 ms, sustain to 1, cutoff to 18 kHz, `level` to 0.5 and output gain to
−30 dB. Set A/B detune to 50 and spread to 1; enable B only for `layered`,
`extreme_no_routes`, `extreme` and `extreme_fx`. Use the note count and A/B unison
shown by the benchmark output, with MIDI notes ascending from 48. For each of
the first N routes, where N is the profile's route count, assign route `i`
source choice `(i % 8) + 1`, destination choice `i - 1`, depth 0.5 and bipolar
on. For `extreme_fx`, enable distortion/delay/reverb with mixes 0.5/0.3/0.3.
The offline harness also changes both tables, Macro 1 and B pitch every 13
blocks; document whether the host run reproduces that automation. A manually
played patch without it is a useful device stress check, but not a numerically
identical benchmark.

`extreme_warp` is `extreme_no_routes` with both A/B phase warps on; see [VOICES.md](VOICES.md) for its single-run figures (about +27% over `extreme_no_routes`).

This device/host check remains **pending** for the reference run above. Do not treat the offline `over_budget_blocks` value as an observed ASIO dropout count.

### FM and sample profiles (18 October 2026)

Measured with the same machine and settings, after tasks 05–07. Three runs agreed within a few percent; the table shows one of them.

| Profile | 44.1 kHz | 48 kHz | 96 kHz | 96 kHz blocks over deadline |
| --- | ---: | ---: | ---: | ---: |
| notes16 | 13% | 14% | 29–32% | 0 |
| sample16 (16 notes, sample source only, looped) | 19% | 20% | 40% | 0 |
| extreme_no_routes | 29% | 32–33% | 82–85% | 72 / 750 |
| extreme_fm (FM depth 0.7) | 39% | 40% | 94% | 170 / 750 |
| extreme_sample (sample source added) | 40% | 42% | 93% | 163 / 750 |
| extreme | 49–55% | 55–56% | 124–127% | 654 / 750 |

- **Sample voice:** with interpolation and the loop crossfade, one sample voice costs about 1.4× a one-lane wavetable voice.
- **Extreme profiles:** FM and the sample source each add about 30% to `extreme_no_routes`. All extreme profiles fail the provisional 70% gate at 96 kHz.
- **Regression:** `extreme_no_routes` is 10–18% higher, and `extreme` about 35% higher, than the 17 October reference. Tasks 02–07 add per-sample work to every voice (glide, warp and FM smoothing, the steal residual, the sample-source smoothers). This run does not separate the cost of each task; an A/B against commit `5d17e59`–`a49454b` would.


### Chorus profiles (18 October 2026)

`layered_chorus` and `extreme_fx_chorus` are `layered` and `extreme_fx` with the chorus on (defaults: 0.6 Hz, depth 0.5, mix 0.5). One run, same machine and settings:

| Profile | 44.1 kHz | 48 kHz | 96 kHz | 96 kHz blocks over deadline |
| --- | ---: | ---: | ---: | ---: |
| layered | 4.2% | 4.6% | 9.6% | 0 |
| layered_chorus | 4.6% | 4.9% | 10.8% | 0 |
| extreme_fx | 44.2% | 48.2% | 105.0% | 471 / 750 |
| extreme_fx_chorus | 44.9% | 49.3% | 109.1% | 520 / 750 |

The chorus is a global stage, so its cost does not grow with notes: about 0.4–1 point of budget at 44.1/48 kHz and 1–4 at 96 kHz. Output level with it on (mix 0.5): −2.2 dB on a broadband saw, −0.3 dB on a four-voice unison pad (`test_effects`).
