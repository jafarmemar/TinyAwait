# TinyAwait Benchmarks — 1.1.0

These measurements are engineering regression data from an x86_64 Linux host. They are useful for comparing implementations under the same conditions, but they are not MCU timing guarantees.

## What changed in 1.1.0

TinyAwait 1.0.x used equal-size coroutine-frame slots. At the default configuration, every live coroutine occupied one 128-byte slot whether its compiler-generated frame was small or large.

TinyAwait 1.1.0 keeps a fixed total frame-memory budget but stores variable-size frames inside one static arena. This removes the hard per-frame slot ceiling without introducing the heap.

The timer scheduler itself is unchanged. The nearest-deadline `poll()` fast path and wrap-safe timing logic remain the same as 1.0.1.

## Historical scheduler baseline

The following values are the published 1.0.0 GCC 14.2 medians at capacity 32:

| Case | Median |
|---|---:|
| `poll()` with 0 active timers | 0.575 ns/call |
| `poll()` with 32 waiting timers, same tick | 1.127 ns/call |
| `poll()` with 32 waiting timers, advancing clock | 1.505 ns/call |
| frame alloc/free near 32-frame capacity | 0.918 ns/op |
| expire 32 timers at the same deadline | 97.749 ns/call |

Those numbers are retained as historical scheduler data. They should not be presented as fresh 1.1.0 measurements.

## Frame allocator comparison

The 1.1.0 allocator was compared with the fixed-slot allocator on the same host using GCC 14.2 and `-O2 -fno-exceptions -fno-rtti`.

Representative results from that comparison:

| Case | 1.0.x fixed slots | 1.1.0 variable arena |
|---|---:|---:|
| isolated 56 B alloc/free | ~2.34 ns | ~4.6 ns |
| mixed 32-frame LIFO batch | baseline | about 8% slower |
| full coroutine create/suspend/resume/destroy cycle | ~20.29 ns | ~20.30 ns |

The isolated allocator operation is slower because 1.1.0 tracks variable spans instead of popping one equal-size slot. In the measured end-to-end coroutine lifecycle, that difference was lost in normal runtime work and remained within host measurement noise.

Do not treat sub-nanosecond differences as portable performance claims. The useful result is the shape of the trade-off: variable frame sizing adds allocator work, while the normal coroutine lifecycle and scheduler hot path remain close to the previous implementation.

## Allocation paths

The arena is designed around common embedded lifetimes:

- **Fresh allocation:** extend the bump frontier. O(1).
- **LIFO release:** retreat the bump frontier. O(1).
- **Hole reuse:** search the sorted free-span list for a large enough span.
- **Adjacent free spans:** coalesce when released.
- **Free tail:** reclaimed lazily when a later allocation needs more contiguous space.

A fragmented free list can make hole reuse O(number of free spans). This is an intentional difference from the old equal-slot free-list, whose allocation/release operations were strictly O(1).

`TINYAWAIT_MAX_TASKS` still bounds the number of live frames, so the amount of allocator bookkeeping work is bounded by a compile-time configuration.

## RAM model

Default compatibility settings remain:

```cpp
#define TINYAWAIT_MAX_TASKS 32
#define TINYAWAIT_FRAME_SIZE 128
```

Without an explicit pool budget, they produce about 4 KiB of coroutine-frame storage, adjusted to the target's maximum fundamental alignment.

New projects can set the budget directly:

```cpp
#define TINYAWAIT_MAX_TASKS 16
#define TINYAWAIT_FRAME_POOL_BYTES 2048
```

The important difference is how that memory is used.

With fixed 128-byte slots, a compiler frame larger than 128 bytes required increasing every slot. With the variable arena, a larger frame consumes its aligned size from the shared budget while smaller frames keep their smaller allocations.

Host validation included coroutine frames in the roughly 360–376 byte range running from the default-size arena without increasing every task slot.

## Fragmentation

The arena never compacts live frames because coroutine frame addresses must remain stable. That means external fragmentation is possible.

For example, several small free spans may add up to 500 bytes while no single span is large enough for a 300-byte frame. In that situation allocation fails even though the total free-byte count looks sufficient.

The allocator reduces this risk by keeping free spans address-sorted, merging adjacent spans, and reclaiming the free tail. Workloads that keep many mixed-size frames alive for long periods should still leave headroom and test their real lifecycle pattern.

## Linked-size proxy

Using the same style of host size-oriented build (`-Os -flto -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wl,--gc-sections`), the variable-arena implementation adds roughly **0.55 KiB of `.text`** over the 1.0.x fixed-slot implementation. Static data remains essentially unchanged at the default budget, because one fixed frame-storage region is still reserved at build time.

This is a host linker regression signal. Exact Flash and RAM use should be measured with the target MCU toolchain.

## No-heap verification

TinyAwait still overrides coroutine frame allocation inside `Async::promise_type`. The frame arena is static and there is no fallback to global `new` or `malloc`.

The no-heap regression test instruments global allocation while repeatedly running nested TinyAwait coroutine sequences. The TinyAwait execution path records no additional global heap allocations.

## Stress and correctness coverage

Allocator-focused verification covers:

- variable requests from very small frames through frames larger than the old 128-byte slot;
- alignment and arena-bound checks;
- overlap checks for simultaneously live frames;
- one million deterministic variable-size allocation/free operations;
- non-LIFO release order;
- split-span reuse and adjacent-span coalescing;
- full-arena recovery after fragmented activity;
- live-frame count exhaustion;
- explicit pool budgets;
- legacy small `TINYAWAIT_FRAME_SIZE` configurations;
- large parent/child and detached coroutine lifetimes;
- ASan/UBSan coverage.

Scheduler verification continues to cover ordinary and wrap-during-resume clock wraparound, maximum delay, sparse polling, same-deadline timers, chained scheduling, randomized schedules, and nearest-deadline recomputation.

## Reproducing the benchmark matrix

Build the repository benchmarks:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The matrix benchmark supports these modes:

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

It can also be compiled directly with a different task limit:

```bash
g++ -std=c++20 -O2 -Isrc \
  -DTINYAWAIT_MAX_TASKS=32 \
  -fno-exceptions -fno-rtti \
  benchmarks/benchmark_matrix.cpp -o bench

./bench poll_wait_same
./bench alloc_mixed
./bench lifecycle
```

Collect several complete runs and compare medians or another stable representative statistic. Keep compiler, flags, host load, and configuration identical when comparing implementations.

## Scheduler scaling limit

When no timer is due, the nearest-deadline fast path avoids scanning the timer table. When a deadline is due, the fixed timer table is still scanned so all due coroutines can resume.

That design remains deliberate for the intended small embedded timer populations. A timing wheel, priority heap, executor, or general scheduler would add machinery that TinyAwait does not currently need.
