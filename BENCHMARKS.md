# TinyAwait Benchmarks

All numbers here were measured for TinyAwait v1.1.0 on 2026-08-10. They are host regression measurements, not universal MCU claims.

## Environment

- Architecture: x86_64
- GCC: 14.2.0
- Clang: 17.0.0
- `poll()` benchmark optimization: `-O2`
- linked-size proxy: `-Os -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wl,--gc-sections`
- default `TINYAWAIT_MAX_TASKS`: 32
- default `TINYAWAIT_FRAME_SIZE`: 128

## `poll()` cost

`benchmarks/benchmark.cpp` calls `tinyawait::poll()` 5,000,000 times for each occupancy. Five complete runs were collected; the median is reported.

| Active timers | Median ns / `poll()` |
|---:|---:|
| 0 | 20.466 ns |
| 1 | 19.369 ns |
| 32 | 27.527 ns |

The timer scheduler intentionally scans a small fixed array. Even with the larger default capacity, the measured host difference between an empty table and 32 occupied slots remained small in this environment.

## Resume overhead

Thirty-two ready one-shot coroutines were resumed in one `poll()` call. Dividing the measured interval by 32 produced a median coarse estimate of:

**37.875 ns per resume**

This includes scheduler bookkeeping and host timing overhead.

## Coroutine frame size

GCC 14.2, x86_64 benchmark instrumentation:

| Coroutine | Measured compiler frame |
|---|---:|
| simple sleeper | 56 B |
| maximum observed after nested parent/child sample | 72 B |

Frame size depends on compiler, ABI, optimization, function parameters, and local variables.

## Static memory

Default configuration:

```text
TINYAWAIT_MAX_TASKS  = 32
TINYAWAIT_FRAME_SIZE = 128
```

The fixed state is primarily:

```text
frame_pool = MAX_TASKS × aligned FRAME_SIZE
timers     = MAX_TASKS × sizeof(TimerSlot)
frame_used = MAX_TASKS × sizeof(bool)
```

On a typical 32-bit MCU where a coroutine handle is 4 B, `TimerSlot` is expected to be about 12 B:

```text
frame pool     32 × 128 = 4096 B
frame flags    32 × 1   =   32 B
timer table    32 × 12  =  384 B
nominal total             4512 B
```

Target-specific alignment may change the exact total.

A free slot is immediately reusable, but the fixed pool remains statically reserved. This is intentional: TinyAwait does not use a heap or dynamically shrink/grow its capacity.

On the measured 64-bit host, `TimerSlot` is 16 B and `FrameSlot` is 128 B.

## Host linked-size proxy

GNU `size` output:

```text
   text   data    bss    dec    hex
   1205    520      8   1733    6c5   baseline
   2532    552   4680   7764   1e54   TinyAwait nested sample
```

Increment:

- `.text`: +1327 B
- `.data`: +32 B
- `.bss`: +4672 B

The TinyAwait sample uses the default 32-task pool and exercises nested `co_await child()` flow. These ELF numbers are a regression signal, not MCU Flash/RAM measurements.

## Heap-allocation test

`tests/test_no_heap.cpp` instruments global `operator new`, then performs 10,000 parent/child TinyAwait create/suspend/resume/complete sequences.

Result in the verified host test run:

**0 additional global heap allocations during the TinyAwait path.**

## Maximum-delay test

`tests/test_max_delay.cpp` schedules:

```cpp
co_await tinyawait::max_delay_ms;
```

where `max_delay_ms == 4,294,967,295`.

The simulated clock crosses the 32-bit wrap and advances toward the deadline in several large chunks. The test verifies no early resume at `max_delay_ms - 1` and correct resume at exactly `max_delay_ms`.

## Stress test

`tests/test_stress.cpp` runs:

- 250 rounds
- 32 simultaneous coroutines per round
- 100 suspend/resume cycles per coroutine

Total scheduled suspension/resumption cycles exercised:

**800,000 per test execution**

After each round, both active frame and active timer counts must return to zero.
