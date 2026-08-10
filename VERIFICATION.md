# Verification Report — 2026-08-10

This report records what was actually executed while assembling the TinyAwait v1.1 candidate.

## Implementation

- Architecture: header-only, fixed-memory C++20 coroutine delay primitive.
- Public API: `Async`, integer-millisecond `co_await`, `co_await child()`, `tinyawait::poll()`, `tinyawait::max_delay_ms`.
- Core file: `src/TinyAwait.h`.
- Core size: about 104 logical implementation lines under the README counting rule.
- External library dependencies: none.
- Default frame pool: 32 × 128 B.
- Default failure behavior: fail-fast; no heap fallback.
- Maximum single delay: 4,294,967,295 ms.

## Host builds actually run

### GCC

- Compiler: GCC 14.2.0
- Architecture: x86_64
- CMake Release build: passed
- `-fno-exceptions`: passed
- `-fno-rtti`: passed
- warnings: `-Wall -Wextra -Wpedantic -Werror`, passed
- 9/9 CTest tests passed

### Clang

- Compiler: Clang 17.0.0
- Architecture: x86_64
- CMake Release build: passed
- `-fno-exceptions`: passed
- `-fno-rtti`: passed
- warnings: `-Wall -Wextra -Wpedantic -Werror`, passed
- 9/9 CTest tests passed

## Tests actually run

- `test_basic`
- `test_wraparound`
- `test_capacity`
- `test_no_heap`
- `test_stress`
- `test_frame_size`
- `test_nested`
- `test_max_delay`
- `test_default_capacity`

Coverage includes:

- immediate start
- zero/1/500 ms awaits
- sequential awaits
- coroutine completion
- infinite coroutine behavior
- fixed-capacity failure behavior
- frame and timer reuse
- default 32-way simultaneous capacity
- nested `co_await child()`
- immediate child completion
- parent continuation
- child-frame destruction/reuse
- maximum `uint32_t` delay
- 32-bit clock wraparound
- no premature resume
- oversized-frame failure
- no residual frame/timer slots after completion
- no TinyAwait heap allocation in nested operation
- repeated 32-way lifecycle stress

## Sanitizers actually run

All nine tests were separately compiled and executed with GCC using:

```text
-fsanitize=address,undefined
```

Result: passed; no ASan/UBSan error was reported.

ThreadSanitizer was not run because TinyAwait is intentionally single-threaded and not thread-safe.

## Heap test

Global `operator new` was instrumented during 10,000 parent/child create/suspend/resume/complete sequences.

Result: **0 additional global heap allocations** in the tested TinyAwait path.

## Stress test

- 250 rounds
- 32 concurrent coroutines per round
- 100 suspension/resumption cycles per coroutine
- 800,000 scheduled suspension/resumption cycles per test execution

Result: passed; active frame and timer counts returned to zero after every round.

## Maximum-delay verification

The simulated clock schedules `tinyawait::max_delay_ms` (`4,294,967,295 ms`), crosses the 32-bit wrap boundary, advances in multiple chunks, verifies no resume one millisecond early, and verifies resume at the exact maximum delay.

Result: passed with GCC, Clang, ASan, and UBSan host runs.

## Nested-await verification

`test_nested` verifies:

1. Parent begins immediately.
2. Child begins immediately.
3. Parent suspends while awaiting the child.
4. Only the child owns the active timer during that wait.
5. Child completion resumes the parent.
6. The child's frame is destroyed before the parent continues its next timed wait.
7. All frames and timers return to zero at completion.
8. A child that completes immediately can also be awaited safely.

Result: passed with GCC, Clang, ASan, and UBSan host runs.

## Measurements actually run

Median of five GCC benchmark runs:

- `poll()` / 0 active timers: 20.466 ns
- `poll()` / 1 active timer: 19.369 ns
- `poll()` / 32 active timers: 27.527 ns
- coarse ready-coroutine resume estimate: 37.875 ns/resume
- simple sleeper frame: 56 B
- maximum observed frame after nested sample: 72 B
- host `TimerSlot`: 16 B
- host `FrameSlot`: 128 B

Host linked-size proxy (`-Os`, nested sample, default capacity 32):

- baseline: text 1205 B, data 520 B, bss 8 B
- TinyAwait: text 2532 B, data 552 B, bss 4680 B
- increment: text +1327 B, data +32 B, bss +4672 B

These are x86_64 ELF measurements, not MCU Flash/RAM measurements.

## Embedded verification status

No ESP-IDF, Arduino-ESP32, Pico SDK, STM32, AVR, or generic ARM/RISC-V cross toolchain was installed in the local build environment. Therefore no embedded target is falsely labeled as locally compile-tested or hardware-tested.

GitHub Actions includes an Arduino-ESP32 compile job for all supplied Arduino examples.

## Resource-release semantics

When a timer becomes ready, its timer slot is cleared before coroutine resumption. When a coroutine completes, its frame slot is returned to the TinyAwait pool. Nested child frames are likewise destroyed/released before the awaiting parent continues.

The **static pool itself remains reserved RAM** for the configured maximum capacity. TinyAwait does not dynamically return that memory to a heap because the design intentionally avoids heap allocation and fragmentation.

## Known limitations

- C++20 `<coroutine>` support is mandatory.
- Capacity is fixed at compile time; default is 32 simultaneously-live frames.
- Frame size is fixed at compile time; default is 128 B per slot.
- `Async` is void-only; child await does not return a value.
- Single-threaded; not ISR-safe; `poll()` must not be reentrant.
- Millisecond resolution.
- The fixed pool reserves RAM even when slots are currently free.
- Embedded compatibility outside the host verification remains expected/unverified until real toolchain or hardware builds are run.
