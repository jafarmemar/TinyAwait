# Verification Report — TinyAwait 1.2.0

This report records the engineering verification performed on 2026-08-10 for the small-scheduler optimization work.

## Change under review

Two lightweight optimizations were evaluated independently and together:

1. a nearest-deadline `poll()` fast path;
2. an O(1) coroutine-frame free-list allocator.

Both were accepted only after A/B/C/D measurement. A first nearest-deadline implementation was rejected after a newly added sparse-poll/full-wrap overshoot test exposed a late-timer bug. The final design uses a wrap epoch and absolute 32-bit deadline instead.

The public programming model remains unchanged:

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

`tinyawait::max_delay_ms` remains `UINT32_MAX`.

## Host toolchains

Local verification environment:

- x86_64 Linux
- GCC 14.2.0
- Clang 17.0.0
- CMake 3.31.6
- GNU binutils/`size` 2.44

### GCC

- Release-style full test suite: passed.
- `-Wall -Wextra -Wpedantic -Werror`: passed.
- `-fno-exceptions -fno-rtti`: passed.

### Clang

- Release-style full test suite: passed.
- `-Wall -Wextra -Wpedantic -Werror`: passed.
- `-fno-exceptions -fno-rtti`: passed.

## Sanitizers

The complete pre-existing suite and both new optimization-specific tests were built/run with GCC AddressSanitizer + UndefinedBehaviorSanitizer and leak detection enabled.

Result: **passed with no reported sanitizer error**.

## Test coverage

The 1.2.0 CMake suite contains 14 tests:

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

### Nearest-deadline-specific coverage

`test_next_deadline` verifies:

- zero active timers and zero clock reads on empty `poll()`;
- one and multiple future timers;
- insertion of a timer earlier than the currently nearest timer;
- nearest expiration and recomputation;
- same-deadline expiration;
- a resumed coroutine scheduling its next delay;
- clock movement larger than `INT32_MAX` without adopting signed half-range semantics;
- 32-bit wrap boundary;
- exact full-range `max_delay_ms` progress in chunks;
- max-delay overshoot after a complete clock wrap;
- 500 deterministic full-range reference rounds using large per-poll deltas and a 64-bit logical elapsed counter only in the test oracle.

The final overshoot case is specifically important: it rejected an earlier candidate that looked faster in the simple benchmark but could execute a full-range timer late.

### Free-list-specific coverage

`test_freelist` verifies:

- initial free-list/bump-index state;
- allocate one/all and exhaustion;
- unique live slot ownership;
- arbitrary first/middle/last releases;
- LIFO free-list reuse;
- traversal with cycle/duplicate detection;
- 300,000 deterministic randomized allocation/release operations;
- full-capacity reuse after stress.

Existing coroutine lifecycle, nested-await, no-heap, randomized scheduler, frame-size, and capacity tests exercise the allocator through the real coroutine delete path and verify that active frame/timer counts return to zero.

## No-heap guarantee

`test_no_heap` instruments global `operator new` and runs 10,000 nested parent/child create/suspend/resume/complete sequences.

Result: **0 additional global heap allocations in the TinyAwait execution path**.

The new free-list is intrusive: its link is stored in the bytes of a frame that is already free. There is no `malloc`, `new`, container, or heap fallback.

## Wraparound / maximum delay

The accepted scheduler does not use `now >= deadline` by itself and does not cast the difference to a signed 32-bit type. A small modulo wrap epoch distinguishes current-, next-, and overdue-epoch deadlines while the existing requirement remains that less than one full 32-bit millisecond counter cycle elapses between clock observations while timers are active.

Verified cases include ordinary wrap, `UINT32_MAX` single delay, large poll gaps, and the full-wrap overshoot regression.

## Performance / RAM / Flash decision

See `BENCHMARKS.md` for the complete tables. At the important default capacity 32 on the host:

- waiting `poll()`, same tick: 27.307 ns -> **1.127 ns**;
- waiting `poll()`, clock advancing: 27.889 ns -> **1.505 ns**;
- alloc/free near capacity: 20.200 ns -> **0.918 ns**;
- 32 same-deadline expirations: 246.647 ns -> **97.749 ns**.

The size-oriented host proxy at capacity 32 changed from 2451 B to 2727 B `.text` with LTO (+276 B). Representative raw static state on a 32-bit MCU is estimated at about 4516 B before versus 4499 B after (-17 B), subject to target alignment.

The one-frame allocator case has a tiny host regression because an O(N) scan with N=1 is exceptionally cheap. This is documented rather than hidden. The free-list becomes beneficial at the practical 4+ capacities and strongly beneficial at 8/16/32 and above.

## Embedded validation

Local execution in this environment did not have Arduino CLI, ARM cross-compilers, PlatformIO, or a RISC-V embedded cross-compiler installed. Therefore local results are **host tested**, not hardware claims.

GitHub CI remains responsible for compiling all four examples with pinned Arduino-ESP32 3.3.11:

- `SingleDelay`
- `RepeatingDelay`
- `SequentialDelays`
- `NestedDelay`

That CI result is compile verification only, not ESP32 hardware execution. Other embedded targets remain expected until their real toolchain or hardware is used.

## Public API compatibility

No public API change was introduced. Existing code using these forms remains source-compatible:

```cpp
Async task();
co_await 500;
co_await child();
tinyawait::poll();
tinyawait::max_delay_ms;
```

No priority queue, STL dynamic container, executor, RTOS dependency, dynamic scheduler, or timing wheel was added.

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
- CPU regression at small capacities;
- RAM/Flash trade-offs;
- multi-translation-unit state;
- source API compatibility.

The full suite and sanitizers were rerun after the corrected deadline design replaced the rejected candidate.

## Remaining limitations

- Cooperative, single-threaded design; not ISR-safe.
- `poll()` is not reentrant.
- Fixed maximum live-frame count and fixed frame slot size.
- Timer-table scan still occurs when a deadline is due.
- All TinyAwait configuration macros must match across translation units.
- Production `TINYAWAIT_ON_ERROR()` should be fatal/non-returning.
- Target-specific MCU timing, final Flash, and alignment require target-toolchain measurement.
