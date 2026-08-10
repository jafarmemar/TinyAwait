# TinyAwait Benchmarks — 1.2.0

These are host regression measurements collected while evaluating the 1.2.0 scheduler/allocator changes. They are **not MCU timing claims**.

## Environment and methodology

- Host: x86_64 Linux
- GCC: 14.2.0
- Clang used for correctness cross-checking: 17.0.0
- Runtime benchmarks: GCC `-O2 -fno-exceptions -fno-rtti`
- Size proxy: GCC `-Os -flto -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wl,--gc-sections`
- Baseline commit: `7c524280709d03bb1c557398a151862bf06d007e` (TinyAwait 1.1.1 main before this work)
- Capacities: 1, 4, 8, 16, 32, 64, 128, 256, 1000
- Multiple complete runs were collected; medians are reported.
- A/B/C/D were built with otherwise identical settings.

Variants:

- **A — baseline:** 1.1.1 implementation at the stated baseline commit.
- **B — nearest-deadline only:** corrected wrap-safe epoch/deadline fast path, original frame allocator.
- **C — free-list only:** original timer scheduler, O(1) frame allocator.
- **D — both:** accepted 1.2.0 implementation.

The experimental variants were kept out of the public header so benchmark-only switches do not add production complexity.

## Important rejected design

An earlier global-countdown fast path was faster in a simple waiting loop, but it was rejected during senior review. If a full-range timer was advanced by several large polls and the final poll overshot its deadline after a 32-bit clock wrap, the global countdown could say "due" while stale per-timer elapsed state no longer represented the total elapsed time. Baseline passed the added overshoot test; that candidate failed it.

The accepted design uses a small wrap epoch plus an absolute 32-bit deadline. It keeps the hot path O(1), uses only 32-bit arithmetic, preserves the full `uint32_t` delay range, and passes the overshoot regression test.

## Repeated `poll()` while no timer is due

Same clock tick, ns per `poll()`:

| Capacity | A baseline | B deadline | C free-list | D both |
|---:|---:|---:|---:|---:|
| 1 | 4.044 | 0.928 | 4.010 | **0.836** |
| 4 | 4.613 | 1.106 | 4.354 | **1.112** |
| 8 | 7.031 | 1.470 | 7.372 | **1.151** |
| 16 | 13.988 | 1.190 | 14.350 | **1.229** |
| 32 | 27.307 | 1.398 | 27.836 | **1.127** |
| 64 | 55.492 | 1.138 | 55.400 | **1.203** |
| 128 | 107.200 | 0.849 | 109.475 | **1.388** |
| 256 | 223.867 | 0.861 | 307.939 | **1.514** |
| 1000 | 1203.382 | 0.813 | 1082.610 | **1.493** |

With a 1 ms clock advance on every call, D measured 1.459 ns at capacity 8, 1.505 ns at 16, 1.505 ns at 32, 1.723 ns at 64, and 1.934 ns at 1000. Baseline at the same capacities measured 7.776, 15.246, 27.889, 55.656, and 866.114 ns respectively.

For the important 8/16/32 configurations, the combined waiting-poll improvement was approximately **81–95%** depending on whether the clock stayed constant or advanced.

The existing no-active-timer fast path remains effectively unchanged. At capacity 32 the measured median was 0.580 ns before and 0.575 ns with D; differences at this scale should be treated as measurement noise.

## Same-deadline expiration

Time for one `poll()` that expires all configured timers, ns:

| Capacity | A baseline | B deadline | C free-list | D both |
|---:|---:|---:|---:|---:|
| 1 | 25.685 | 25.807 | 25.389 | **25.346** |
| 4 | 29.539 | 30.306 | 28.784 | **29.357** |
| 8 | 42.579 | 44.320 | 32.684 | **41.262** |
| 16 | 104.449 | 119.100 | 52.010 | **48.833** |
| 32 | 246.647 | 267.074 | 77.020 | **97.749** |
| 64 | 797.430 | 795.342 | 138.876 | **144.674** |
| 128 | 3232.296 | 2848.318 | 313.905 | **270.994** |
| 256 | 11913.843 | 11397.303 | 584.073 | **693.318** |
| 1000 | 149409.847 | 149993.900 | 2258.010 | **2810.050** |

The nearest-deadline logic by itself is roughly neutral/slightly more expensive when a scan is genuinely required. The free-list removes the much larger O(N) frame-release scan that occurred for every completing coroutine. The combined result is already slightly better at 8 and becomes substantially better from 16 upward.

## Frame allocation / release

Allocation+release near pool capacity, ns per pair:

| Capacity | A baseline | B deadline | C free-list | D both |
|---:|---:|---:|---:|---:|
| 1 | 0.563 | 0.653 | 0.836 | **0.875** |
| 4 | 2.991 | 3.212 | 0.833 | **0.841** |
| 8 | 6.886 | 8.112 | 0.894 | **0.953** |
| 16 | 12.056 | 13.364 | 0.857 | **0.831** |
| 32 | 20.200 | 22.563 | 0.835 | **0.918** |
| 64 | 39.164 | 57.346 | 0.865 | **0.843** |
| 128 | 77.991 | 117.105 | 0.822 | **0.849** |
| 256 | 152.957 | 225.594 | 0.822 | **0.814** |
| 1000 | 593.816 | 890.096 | 0.831 | **0.825** |

The one-slot configuration is a real micro-regression: the linear search has only one position to inspect, while the free-list has pointer bookkeeping. The absolute difference is about 0.3 ns in this host benchmark. At capacities 4 and above the free-list wins, and at 8/16/32 the measured improvement is approximately **86%, 93%, and 95%** near capacity.

In repeated allocate-all/free-all cycles, D measured 0.745, 0.823, and 0.767 ns per operation at capacities 8, 16, and 32 versus 2.093, 3.827, and 5.461 ns for A. Deterministic randomized allocation/release also improved from 2.766/4.774/6.131 ns to 1.241/1.206/1.157 ns at 8/16/32.

## RAM / static state

The free-list uses released frame bytes themselves for links. Production no longer needs the `frame_used[MAX_TASKS]` array. A small bump index identifies never-used slots, so initialization is also O(1); the free-list does not require an O(N) startup pass.

For a representative 32-bit MCU with 4-byte coroutine handles, 4-byte pointers, 12-byte `TimerSlot`, and 128-byte frame slots, the raw state estimate is:

| Capacity | A baseline | B deadline only | C free-list only | D both | D vs A |
|---:|---:|---:|---:|---:|---:|
| 1 | 145 B | 155 B | 142 B | 159 B | +14 B |
| 4 | 568 B | 578 B | 557 B | 579 B | +11 B |
| 8 | 1132 B | 1142 B | 1129 B | 1139 B | +7 B |
| 16 | 2260 B | 2270 B | 2249 B | 2259 B | -1 B |
| 32 | 4516 B | 4526 B | 4489 B | **4499 B** | **-17 B** |
| 64 | 9028 B | 9038 B | 8965 B | 8979 B | -49 B |
| 128 | 18052 B | 18062 B | 17925 B | 17939 B | -113 B |
| 256 | 36100 B | 36110 B | 35850 B | 35860 B | -240 B |
| 1000 | 141004 B | 141014 B | 140010 B | 140020 B | -984 B |

These are structure-level estimates, not linker totals. Target ABI/alignment can change exact `.bss`/`.data` placement. The important small-target trade-off is explicit: D costs a few bytes at 1/4/8, is roughly neutral at 16, and saves state from 32 upward.

On the x86_64 size probe at capacity 32, `TimerSlot` remained 16 B and `FrameSlot` remained 128 B.

## Flash / linked-size proxy

Capacity 32, `-Os -flto ... --gc-sections`:

| Variant | `.text` | `.data` | `.bss` |
|---|---:|---:|---:|
| A baseline | 2451 B | 552 B | 4704 B |
| B deadline only | 2749 B | 553 B | 4696 B |
| C free-list only | 2429 B | 552 B | 4688 B |
| D both | **2727 B** | **553 B** | **4696 B** |

D therefore adds **276 B of host `.text`** over A in this size-oriented proxy. Without LTO, the same sample changed from 2557 B to 2902 B of `.text` (+345 B). The free-list itself slightly reduced code size; the wrap-safe deadline logic accounts for the increase.

The Flash increase was accepted because the normal waiting-poll path improves by roughly an order of magnitude or more at the intended 8/16/32 capacities, the allocator becomes O(1), RAM remains essentially flat for those configurations, and correctness remains full-range/wrap-safe. Actual MCU Flash cost must still be checked with the target toolchain.

## Reproducing final-version capacity measurements

`benchmarks/benchmark_matrix.cpp` uses the configured capacity, so the final implementation can be profiled across capacities without changing the public API:

```bash
for n in 1 4 8 16 32 64 128 256 1000; do
  g++ -std=c++20 -O2 -Isrc \
    -DTINYAWAIT_MAX_TASKS=$n \
    -fno-exceptions -fno-rtti \
    benchmarks/benchmark_matrix.cpp -o "bench-$n"

  ./"bench-$n" poll_wait_same
  ./"bench-$n" poll_wait_adv
  ./"bench-$n" expire_same
  ./"bench-$n" alloc_near
done
```

Collect multiple complete runs and report a median. Historical A/B/C/D comparison numbers above were collected during the engineering change against the stated baseline commit; final production code intentionally has no feature switches for reverting individual internals.

## Scaling limit

The final scheduler still scans the fixed timer table when at least one deadline is due. That is deliberate: for small embedded timer populations it keeps the architecture compact, deterministic, heap-free, and easy to audit. The fast path removes the scan from the common "nothing due yet" case.

For very large populations with frequent expirations, a different scheduler data structure may eventually be justified. That is future work, not part of this optimization.
