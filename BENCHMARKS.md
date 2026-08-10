# TinyAwait Benchmarks — 1.0.0

These measurements describe the initial public TinyAwait 1.0.0 snapshot. They are host regression measurements, **not MCU timing guarantees**.

## Environment and methodology

- Host: x86_64 Linux
- GCC: 14.2.0
- Clang used for correctness cross-checking: 17.0.0
- Runtime benchmarks: GCC `-O2 -fno-exceptions -fno-rtti`
- Size proxy: GCC `-Os -flto -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wl,--gc-sections`
- Default `TINYAWAIT_MAX_TASKS`: 32
- Default `TINYAWAIT_FRAME_SIZE`: 128
- Multiple complete runs were collected; medians are reported.

## Current scheduler results

At the default capacity of 32, representative GCC 14.2 medians are:

| Case | Median |
|---|---:|
| `poll()` with 0 active timers | **0.575 ns/call** |
| `poll()` with 32 waiting timers, same tick | **1.127 ns/call** |
| `poll()` with 32 waiting timers, advancing clock | **1.505 ns/call** |
| frame alloc/free near 32-frame capacity | **0.918 ns/op** |
| expire 32 timers at the same deadline | **97.749 ns/call** |

The nearest-deadline fast path keeps repeated `poll()` calls O(1) while all timers are still in the future. Once at least one timer is due, the fixed timer table is scanned and all due timers are resumed.

The frame allocator uses an intrusive fixed-capacity free-list, so frame allocation and reuse are O(1). Free-list links live inside frame storage that is already free; there is no heap fallback or dynamic container.

## Capacity scaling

`benchmarks/benchmark_matrix.cpp` supports measurements at capacities:

```text
1, 4, 8, 16, 32, 64, 128, 256, 1000
```

Small configurations such as 8, 16, and 32 are the primary design target. Higher capacities are included to make scaling limitations visible.

The fixed-table design remains intentionally simple: when a deadline is actually due, work scales with configured capacity because the timer array is scanned. TinyAwait does not introduce a timing wheel, heap scheduler, executor, or dynamic timer structure.

## Frame allocation / release scaling

Representative allocation+release medians near pool capacity:

| Capacity | ns per pair |
|---:|---:|
| 1 | 0.875 |
| 4 | 0.841 |
| 8 | 0.953 |
| 16 | 0.831 |
| 32 | 0.918 |
| 64 | 0.843 |
| 128 | 0.849 |
| 256 | 0.814 |
| 1000 | 0.825 |

These results show the intended O(1) allocator behavior across the measured capacities.

## RAM / static state

The free-list uses released frame bytes themselves for links. Production state does not require a separate per-frame occupancy array. A small bump index identifies never-used slots, so initialization is also O(1).

For a representative 32-bit MCU with 4-byte coroutine handles, 4-byte pointers, 12-byte `TimerSlot`, and 128-byte frame slots, the raw state estimate is:

| Capacity | Estimated static state |
|---:|---:|
| 1 | 159 B |
| 4 | 579 B |
| 8 | 1139 B |
| 16 | 2259 B |
| 32 | **4499 B** |
| 64 | 8979 B |
| 128 | 17939 B |
| 256 | 35860 B |
| 1000 | 140020 B |

These are structure-level estimates, not linker totals. Target ABI/alignment can change exact `.bss`/`.data` placement.

On the x86_64 size probe at capacity 32, `TimerSlot` is 16 B and `FrameSlot` is 128 B.

## Flash / linked-size proxy

Capacity 32 with the documented size-oriented flags:

```text
.text: 2727 B
.data:  553 B
.bss:  4696 B
```

This is a host linked-code regression signal, not an MCU Flash guarantee. Actual Flash/RAM usage must be measured with the target toolchain when exact embedded numbers matter.

## Coroutine frame size

Host instrumentation has measured the covered coroutine frames well below the default 128-byte slot. Frame size remains compiler-, ABI-, optimization-, parameter-, and local-variable-dependent.

Applications with larger coroutine frames must increase `TINYAWAIT_FRAME_SIZE`.

## Heap test

`test_no_heap` performs 10,000 parent/child create/suspend/resume/complete sequences while global `operator new` is instrumented.

Result: **0 additional global heap allocations in the TinyAwait path.**

## Correctness and stress coverage

The verification suite includes:

- full `uint32_t` maximum delay
- 32-bit clock wraparound
- nearest-deadline recomputation and earlier insertion
- same-deadline timers
- sparse polling across very large elapsed intervals
- deterministic full-range timer reference rounds
- frame-pool exhaustion and complete reuse
- free-list corruption/cycle checks
- 300,000-operation deterministic allocator stress
- 800,000 scheduled suspension/resumption cycles in the lifecycle stress test

## Reproducing capacity measurements

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

Collect multiple complete runs and report a median.

## Scaling limit

The scheduler still scans the fixed timer table when at least one deadline is due. That is deliberate: for small embedded timer populations it keeps the architecture compact, deterministic, heap-free, and easy to audit. The fast path removes the scan from the common "nothing due yet" case.

For very large populations with frequent expirations, a different scheduler data structure may eventually be justified. That is future work, not part of TinyAwait 1.0.0.
