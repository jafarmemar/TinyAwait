# Verification Report — TinyAwait 1.1.1

This report records the final host verification performed on 2026-08-10.

## Core configuration under review

- header-only C++20 implementation
- fixed frame pool; default 32 × 128 B
- no heap fallback
- integer-millisecond `co_await`
- parent/child `co_await child()`
- 32-bit wrap-safe timer accounting
- maximum single delay: 4,294,967,295 ms
- idle `poll()` fast path

## Host compilers

### GCC 14.2.0

- Release CMake build: passed
- 12/12 CTest tests: passed
- `-Wall -Wextra -Wpedantic -Werror`: passed
- `-fno-exceptions -fno-rtti`: passed

### Clang 17.0.0

- Release CMake build: passed
- 12/12 CTest tests: passed
- `-Wall -Wextra -Wpedantic -Werror`: passed
- `-fno-exceptions -fno-rtti`: passed

## Sanitizers

All 12 tests were built and run with GCC using AddressSanitizer and UndefinedBehaviorSanitizer.

Result: **passed**.

## Tests

- `test_basic`
- `test_wraparound`
- `test_capacity`
- `test_no_heap`
- `test_stress`
- `test_frame_size`
- `test_nested`
- `test_max_delay`
- `test_default_capacity`
- `test_delay_validation`
- `test_randomized`
- `test_multitu`

Coverage includes zero/short delays, sequential delays, child awaiting, immediate child completion, frame/timer reuse, default 32-way capacity, capacity failure, oversized frames, maximum delay, clock wraparound, no-heap operation, repeated lifecycle stress, invalid-delay handling, deterministic randomized timer schedules, and shared header-only state across multiple translation units.

## Arduino-style examples

The four examples also passed local C++20 syntax builds with an Arduino API shim using GCC and Clang:

- `SingleDelay`
- `RepeatingDelay`
- `SequentialDelays`
- `NestedDelay`

GitHub Actions compiles the same examples with pinned Arduino-ESP32 3.3.11 as one real Arduino C++20-capable compile target. This is compile verification, not hardware execution.

## Measurements

Eleven-run GCC medians after the idle fast path:

- `poll()` / 0 active timers: 0.574 ns
- `poll()` / 1 active timer: 11.989 ns
- `poll()` / 32 active timers: 27.880 ns
- coarse ready-resume estimate: 27.250 ns/resume
- simple coroutine frame: 56 B
- largest nested-sample frame: 72 B

Host linked-size proxy: text 2557 B, data 552 B, bss 4680 B. Baseline: text 1205 B, data 520 B, bss 8 B.

## Important production constraints

- TinyAwait is cooperative, single-threaded, and not ISR-safe.
- `poll()` must not be called reentrantly.
- All TinyAwait configuration macros must be identical in every translation unit.
- `TINYAWAIT_ON_ERROR()` is a fatal hook in production and should not return.
- The configured frame size must fit every compiler-generated coroutine frame.
- Keep coroutine locals at normal ABI alignment unless an over-aligned layout has been verified with the target compiler. An explicit host probe showed that extended-alignment coroutine-frame behavior is compiler-dependent.
- More than one full 32-bit millisecond clock cycle must not elapse between `poll()` calls while timers are active.
- Targets not explicitly listed as verified remain expected until tested with their real toolchain or hardware.
