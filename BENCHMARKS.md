# TinyAwait Benchmarks — 1.1.x

These measurements are engineering regression data from an x86_64 Linux host. They are useful for comparing implementations under the same conditions, but they are not MCU timing guarantees.

## 1.1.1 configuration cleanup

Version 1.1.1 does not change the scheduler or frame allocator algorithm. It only simplifies the public memory configuration.

New code normally needs no memory macro. TinyAwait keeps the same default behavior: 32 live tasks reserve roughly 4 KiB of frame storage, and lowering `TINYAWAIT_MAX_TASKS` lowers that default budget proportionally.

When an explicit budget is useful, configure the shared arena directly:

```cpp
#define TINYAWAIT_MAX_TASKS 16
#define TINYAWAIT_FRAME_POOL_BYTES 2048
```

`TINYAWAIT_FRAME_SIZE` is deprecated. It is still recognized for source compatibility with older projects, but it is no longer documented as a current configuration option.

Because 1.1.1 does not change the runtime paths, the 1.1.0 performance measurements remain the relevant allocator comparison.

## What changed in 1.1.0

TinyAwait 1.0.x used equal-size coroutine-frame slots. Version 1.1.0 replaced them with one fixed-memory arena that stores variable-size frames. This removed the hard per-frame slot ceiling without introducing the heap.

The timer scheduler itself remained unchanged. The nearest-deadline `poll()` fast path and wrap-safe timing logic are the same as 1.0.1.

## Historical scheduler baseline

The following values are the published 1.0.0 GCC 14.2 medians at capacity 32:

| Case | Median |
|---|---:|
| `poll()` with 0 active timers | 0.575 ns/call |
| `poll()` with 32 waiting timers, same tick | 1.127 ns/call |
| `poll()` with 32 waiting timers, advancing clock | 1.505 ns/call |
| frame alloc/free near 32-frame capacity | 0.918 ns/op |
| expire 32 timers at the same deadline | 97.749 ns/call |

Those numbers are retained as historical scheduler data. They should not be presented as fresh 1.1.x measurements.

## Frame allocator comparison

The variable-size allocator was compared with the fixed-slot allocator on the same host using GCC 14.2 and `-O2 -fno-exceptions -fno-rtti`.

Representative results:

| Case | 1.0.x fixed slots | 1.1.x variable arena |
|---|---:|---:|
| isolated 56 B alloc/free | ~2.34 ns | ~4.6 ns |
| mixed 32-frame LIFO batch | baseline | about 8% slower |
| full coroutine create/suspend/resume/destroy cycle | ~20.29 ns | ~20.30 ns |

The isolated allocator operation is slower because variable spans require more bookkeeping. In the measured end-to-end coroutine lifecycle, the difference remained within normal host measurement noise.

## Allocation paths

The arena is designed around common embedded lifetimes:

- **Fresh allocation:** extend the bump frontier. O(1).
- **LIFO release:** retreat the bump frontier. O(1).
- **Hole reuse:** search the sorted free-span list for a large enough span.
- **Adjacent free spans:** coalesce when released.
- **Free tail:** reclaim lazily when a later allocation needs more contiguous space.

A fragmented free list can make hole reuse O(number of free spans). `TINYAWAIT_MAX_TASKS` still bounds the number of live frames, so the work remains bounded by a compile-time setting.

## RAM model

With the default task limit, TinyAwait reserves roughly 4 KiB for coroutine frames. The default budget scales with `TINYAWAIT_MAX_TASKS` so small configurations remain convenient:

```cpp
#define TINYAWAIT_MAX_TASKS 8
```

For direct control, set the total pool size:

```cpp
#define TINYAWAIT_MAX_TASKS 16
#define TINYAWAIT_FRAME_POOL_BYTES 2048
```

Each compiler-generated coroutine frame consumes its aligned size from the shared pool. A larger frame therefore does not force every task to reserve the same large slot.

## Fragmentation

The arena never compacts live frames because coroutine frame addresses must remain stable. External fragmentation is therefore possible.

Several small free spans can add up to more than a requested frame while no single span is large enough. The allocator reduces this risk by keeping free spans address-sorted, merging adjacent spans, and reclaiming the free tail.

Workloads with many long-lived mixed-size frames should leave headroom and test their real lifecycle pattern.

## Linked-size proxy

Using `-Os -flto -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wl,--gc-sections`, the variable-arena implementation adds roughly **0.55 KiB of `.text`** over the 1.0.x fixed-slot implementation.

Version 1.1.1 only simplifies configuration logic and should be treated as a code-size regression check rather than a new performance architecture.

Exact Flash and RAM use should be measured with the target MCU toolchain.

## No-heap verification

TinyAwait supplies coroutine frame allocation through `Async::promise_type`. The frame arena is static and there is no fallback to global `new`, `malloc`, or a dynamic container.

The no-heap regression test instruments global allocation while repeatedly running nested TinyAwait coroutine sequences.

## Stress and correctness coverage

Allocator-focused verification covers:

- variable requests from very small frames through frames larger than the old fixed-slot size;
- alignment and arena-bound checks;
- overlap checks for simultaneously live frames;
- one million deterministic variable-size allocation/free operations;
- non-LIFO release order;
- split-span reuse and adjacent-span coalescing;
- full-arena recovery after fragmented activity;
- live-frame count exhaustion;
- explicit pool budgets;
- deprecated configuration compatibility;
- large parent/child and detached coroutine lifetimes;
- ASan/UBSan coverage.

Scheduler verification continues to cover clock wraparound, maximum delay, sparse polling, same-deadline timers, chained scheduling, randomized schedules, and nearest-deadline recomputation.

## Reproducing the benchmark matrix

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The matrix benchmark supports:

```text
poll0
poll_wait_same
poll_wait_adv
expire_same
alloc_one
alloc_near
alloc_mixed
lifecycle
```

For a different task limit:

```bash
g++ -std=c++20 -O2 -Isrc \
  -DTINYAWAIT_MAX_TASKS=32 \
  -fno-exceptions -fno-rtti \
  benchmarks/benchmark_matrix.cpp -o bench

./bench poll_wait_same
./bench alloc_mixed
./bench lifecycle
```

Collect several complete runs and compare stable representative values under identical compiler settings and host conditions.

## Scheduler scaling limit

When no timer is due, the nearest-deadline fast path avoids scanning the timer table. When a deadline is due, the fixed timer table is still scanned so all due coroutines can resume.

That design remains deliberate for the intended small embedded timer populations.
