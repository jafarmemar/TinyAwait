# Verification Report — TinyAwait 1.0.0

This report records verification of the initial public TinyAwait 1.0.0 snapshot.

## Configuration under review

- header-only C++20 implementation
- fixed frame pool; default 32 × 128 B
- no heap fallback
- integer-millisecond `co_await`
- parent/child `co_await child()`
- wrap-safe full-range `uint32_t` timing
- nearest-deadline `poll()` fast path
- intrusive O(1) coroutine-frame free-list

The public programming model is:

```cpp
Async task() {
    co_await 500;
}

Async parent() {
    co_await child();
    co_await 1000;
}

void loop() {
    tinyawait::poll();
}
```

`tinyawait::max_delay_ms` is `UINT32_MAX`.

## Host toolchains

Verification environment:

- x86_64 Linux
- GCC 14.2.0
- Clang 17.0.0
- CMake 3.31.6
- GNU binutils/`size` 2.44

### GCC

- Release-style full test suite: passed.
- 14/14 CTest tests: passed.
- `-Wall -Wextra -Wpedantic -Werror`: passed.
- `-fno-exceptions -fno-rtti`: passed.

### Clang

- Release-style full test suite: passed.
- 14/14 CTest tests: passed.
- `-Wall -Wextra -Wpedantic -Werror`: passed.
- `-fno-exceptions -fno-rtti`: passed.

## Sanitizers

All 14 tests were built and run with GCC AddressSanitizer + UndefinedBehaviorSanitizer and leak detection enabled.

Result: **passed with no reported sanitizer error**.

## Test coverage

The CMake suite contains 14 tests:

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
- `test_next_deadline`
- `test_freelist`

### Nearest-deadline coverage

`test_next_deadline` verifies:

- zero active timers and zero clock reads on empty `poll()`;
- one and multiple future timers;
- insertion of a timer earlier than the currently nearest timer;
- nearest expiration and recomputation;
- same-deadline expiration;
- a resumed coroutine scheduling its next delay;
- clock movement larger than `INT32_MAX` without signed half-range semantics;
- 32-bit wrap boundary;
- exact full-range `max_delay_ms` progress in chunks;
- max-delay overshoot after a complete clock wrap;
- 500 deterministic full-range reference rounds using a 64-bit logical elapsed counter only in the test oracle.

### Free-list coverage

`test_freelist` verifies:

- initial free-list/bump-index state;
- allocate one/all and exhaustion;
- unique live slot ownership;
- arbitrary first/middle/last releases;
- free-list reuse;
- traversal with cycle/duplicate detection;
- 300,000 deterministic randomized allocation/release operations;
- full-capacity reuse after stress.

Existing coroutine lifecycle, nested-await, no-heap, randomized scheduler, frame-size, and capacity tests exercise the allocator through the real coroutine delete path and verify that active frame/timer counts return to zero.

## No-heap guarantee

`test_no_heap` instruments global `operator new` and runs 10,000 nested parent/child create/suspend/resume/complete sequences.

Result: **0 additional global heap allocations in the TinyAwait execution path**.

The free-list is intrusive: its link is stored in bytes of a frame that is already free. There is no `malloc`, `new`, dynamic container, or heap fallback.

## Wraparound / maximum delay

The scheduler does not rely on `now >= deadline` alone and does not cast the deadline difference to signed 32-bit arithmetic. A small modulo wrap epoch distinguishes current-, next-, and overdue-epoch deadlines while preserving the full `uint32_t` delay range.

Verified cases include ordinary wrap, `UINT32_MAX` single delay, large poll gaps, and full-wrap overshoot.

## Performance / RAM / Flash

See `BENCHMARKS.md` for methodology and capacity data. At the default capacity 32 on the documented host:

- waiting `poll()`, same tick: **1.127 ns**;
- waiting `poll()`, clock advancing: **1.505 ns**;
- alloc/free near capacity: **0.918 ns**;
- 32 same-deadline expirations: **97.749 ns**;
- no active timers: **0.575 ns**.

The size-oriented host proxy at capacity 32 records approximately 2727 B `.text`, 553 B `.data`, and 4696 B `.bss` with LTO. Representative raw static state on a 32-bit MCU is estimated at about 4499 B, subject to target ABI/alignment.

These are host regression measurements and structure-level estimates, not MCU timing or exact linker guarantees.

## Embedded validation

GitHub CI compiles all four examples with pinned Arduino-ESP32 3.3.11:

- `SingleDelay`
- `RepeatingDelay`
- `SequentialDelays`
- `NestedDelay`

This is compile verification, not ESP32 hardware execution. Other embedded targets remain expected until their real toolchain or hardware is used.

## Public API

```cpp
Async task();
co_await 500;
co_await child();
tinyawait::poll();
tinyawait::max_delay_ms;
```

No priority queue, STL dynamic container, executor, RTOS dependency, dynamic scheduler, or timing wheel is used.

## Final review checklist

Reviewed specifically for:

- stale nearest-deadline state;
- missed/late timers;
- same-deadline behavior;
- wraparound and full-range delay behavior;
- timers scheduled during resume;
- free-list lost/duplicate slots and cycles;
- allocator exhaustion/reuse;
- nested coroutine lifetime;
- hidden heap use;
- CPU behavior at small capacities;
- RAM/Flash trade-offs;
- multi-translation-unit state;
- source API compatibility.

## Remaining limitations

- Cooperative, single-threaded design; not ISR-safe.
- `poll()` is not reentrant.
- Fixed maximum live-frame count and fixed frame slot size.
- Timer-table scan still occurs when a deadline is due.
- All TinyAwait configuration macros must match across translation units.
- Production `TINYAWAIT_ON_ERROR()` should be fatal/non-returning.
- Target-specific MCU timing, final Flash, and alignment require target-toolchain measurement.
