# CPU profiling for new sound engines

Build with `cmake --build build-ui --config Release --target test_modulation`, then run `build-ui/test_modulation_artefacts/Release/test_modulation.exe --benchmark`. Substitute your configured build directory for `build-ui` if needed. It renders approximately one second per row with a 128-sample block. `light` uses one note and one A lane; `layered` uses four notes, two A/B lanes and four modulation routes; `extreme` uses 16 notes, eight A/B lanes and 16 routes; `extreme_fx` also enables distortion, delay and reverb. Table changes, Macro 1 and B pitch are automated every 13 blocks. The timer surrounds `processBlock` only; preparation, parameter changes and output checks are excluded.

`budget_pct` is summed processing time divided by rendered audio time. `worst_block_pct` compares the slowest block to its own deadline; `over_budget_blocks` counts blocks slower than that deadline. These are **offline timing observations**, not ASIO glitches or a DAW's CPU meter. They omit device scheduling, GUI, host and other plug-ins. The older figures in [MODULATION.md](MODULATION.md) timed the whole loop and are not directly comparable.

## Reference run

AMD Ryzen 7 5700X (8 cores/16 threads), Windows 11 Pro, MSVC Release/LTO, 128 samples, 17 October 2026. Three one-second runs of each case; ranges below cover the three runs. All rows rendered finite audio.

| Profile | 44.1 kHz budget | 48 kHz budget | 96 kHz budget | 96 kHz blocks over deadline |
| --- | ---: | ---: | ---: | ---: |
| light | < 1% | < 1% | ~2% | 0 |
| layered | ~4% | ~4% | ~9% | 0 |
| extreme | ~37% | ~41% | 88–91% | 12–51 / 750 |
| extreme_fx | ~38% | ~41% | ~90% | 43–44 / 750 |

The extreme 96 kHz workload has little headroom even before another source engine. The number of over-deadline blocks is sensitive to OS scheduling, so repeat measurements on the target machine. As a provisional gate for a new engine or OSC C, require a representative target patch to use at most 70% of the offline single-thread budget across three runs, with no over-deadline blocks; also require a separate dropout-free device/host check. Do not use this gate to claim that an offline pass guarantees real-time performance.

## Device and host check

In both Borato Wave Standalone and REAPER with the Borato Wave VST3, select the same physical audio device, ASIO driver, 128-sample buffer and sample rate. Record driver, device, OS, CPU, host version and plug-in build. Set A/B, unison, matrix routes and effects to match a benchmark profile, then hold the stated note count for at least 30 seconds using a reproducible MIDI clip or input. Repeat at 44.1, 48 and 96 kHz. In REAPER record its real-time CPU/performance meter and any underrun indication; in Standalone listen or record loopback for clicks and note loss. Log each run as `date | app | device/driver | rate | buffer | profile | notes | CPU | underruns/clicks | observations`.

This device/host check remains **pending** for the reference run above. Do not treat the offline `over_budget_blocks` value as an observed ASIO dropout count.
