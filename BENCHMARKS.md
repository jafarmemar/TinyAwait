# TinyAwait Benchmarks

These measurements are regression data from the host test machine, not MCU timing guarantees.

## Environment

- x86_64 Linux
- GCC 14.2.0
- Clang 17.0.0
- default `TINYAWAIT_MAX_TASKS=32`
- default `TINYAWAIT_FRAME_SIZE=128`

## `poll()` cost

`benchmarks/benchmark.cpp` runs 5,000,000 calls per occupancy. Eleven complete GCC runs were collected after the v1.1.1 idle fast-path change; the median is shown.

| Active timers | Median ns / `poll()` |
|---:|---:|
| 0 | **0.574 ns** |
| 1 | **11.989 ns** |
| 32 | **27.880 ns** |

The empty case returns before reading the clock or scanning the timer table. This is useful for firmware loops that call `poll()` continuously even when no TinyAwait task is waiting.

A coarse 32-coroutine ready-resume measurement produced a median of **27.250 ns/resume** on the same host.

## Coroutine frame size

GCC 14.2 host instrumentation measured:

- simple sleeper: **56 B**
- largest frame in the nested sample: **72 B**

Frame size is compiler-, ABI-, optimization-, parameter-, and local-variable-dependent. The default slot is 128 B; applications with larger coroutine frames must increase `TINYAWAIT_FRAME_SIZE`.

## Static memory

On a typical 32-bit MCU, the default configuration is approximately:

```text
frame pool       32 × 128 = 4096 B
frame flags      32 × 1   =   32 B
timer table      32 × 12  =  384 B
active count                 ~4 B
nominal total              ~4516 B
```

Exact size depends on target alignment and coroutine-handle size. The pool is static and reusable; it does not grow, shrink, or fragment a heap.

On the measured x86_64 host, `TimerSlot` is 16 B and `FrameSlot` is 128 B.

## Host linked-size proxy

`-Os -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wl,--gc-sections`:

```text
                  text   data    bss
baseline          1205    520      8
TinyAwait sample  2557    552   4680
increment        +1352    +32  +4672
```

The sample uses the default 32-task pool and nested child awaiting. ELF numbers are only a regression signal; actual MCU Flash/RAM usage depends on the target toolchain.

## Heap test

`test_no_heap` performs 10,000 parent/child create/suspend/resume/complete sequences while global `operator new` is instrumented.

Result: **0 additional global heap allocations in the TinyAwait path.**

## Stress and randomized scheduling

The deterministic stress test executes **800,000** scheduled suspension/resumption cycles. A separate randomized test repeatedly schedules 32 timers with varied delays, including starts near the 32-bit clock wrap boundary, and verifies that no task resumes early or remains pending after its due time.
