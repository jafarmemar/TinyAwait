# Verification Report — TinyAwait 1.0.1

This report records verification of TinyAwait 1.0.1, including the 32-bit clock-wrap re-arm correctness fix introduced after the initial 1.0.0 release.

## Configuration under review

- header-only C++20 implementation
- fixed frame pool; default 32 × 128 B
- no heap fallback
- integer-millisecond `co_await`
- parent/child `co_await child()`
- wrap-safe full-range `uint32_t` timing
- nearest-deadline `poll()` fast path
- intrusive O(1) coroutine-frame free-list

The public programming model is unchanged:

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

## 1.0.1 correctness fix

The 1.0.0 scheduler could enter a rare inconsistent clock state if all of the following happened in the same due scan:

1. `poll()` captured a pre-wrap `now` value;
2. a due coroutine was resumed;
3. the 32-bit clock wrapped before that coroutine registered its next `co_await` delay;
4. `add_timer()` observed the wrapped clock and advanced the global epoch;
5. the outer due scan continued examining other timers using its older pre-wrap `now` together with the newer epoch.

That mismatch could make another timer appear due prematurely.

Version 1.0.1 moves the due scan into `detail::poll_due()` and checks the epoch after each resumed coroutine. If a re-arm changed the clock epoch, the scan restarts from `clock_last` before another timer is examined. The common future-deadline fast path remains unchanged in structure.

A permanent regression case in `test_next_deadline` reproduces the original wrap-during-resume/re-arm sequence and verifies that a second timer spanning the same wrap is not fired early.

## Host toolchains

Verification environment used for the targeted fix and local cross-checks:

- x86_64 Linux
- GCC 14.2.0
- Clang 17.0.0
- `-O0`, `-O2`, and `-Os` targeted regression builds
- release-style suite with warnings-as-errors, no exceptions, and no RTTI

### GCC

- Release-style full test suite: passed.
- 14/14 CTest tests: passed.
- targeted wrap-during-rearm regression at `-O0`, `-O2`, and `-Os`: passed.
- `-Wall -Wextra -Wpedantic -Werror`: passed.
- `-fno-exceptions -fno-rtti`: passed.

### Clang

- Release-style full test suite: passed.
- 14/14 CTest tests: passed.
- targeted wrap-during-rearm regression at `-O0`, `-O2`, and `-Os`: passed.
- `-Wall -Wextra -Wpedantic -Werror`: passed.
- `-fno-exceptions -fno-rtti`: passed.

## Sanitizers

All 14 tests were built and run with AddressSanitizer + UndefinedBehaviorSanitizer and leak detection enabled in GitHub CI for the fix.

Result: **passed with no reported sanitizer error**.

The targeted wrap regression and extended local wrap stress were also exercised under sanitizers.

## GitHub CI validation

The fix PR completed all configured CI jobs successfully before merge:

- host GCC tests: passed;
- host Clang tests: passed;
- ASan/UBSan: passed;
- size-oriented build: passed;
- Arduino-ESP32 3.3.11 example compilation: passed.

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

### Nearest-deadline / wrap coverage

`test_next_deadline` verifies:

- zero active timers and zero clock reads on empty `poll()`;
- one and multiple future timers;
- insertion of a timer earlier than the currently nearest timer;
- nearest expiration and recomputation;
- same-deadline expiration;
- a resumed coroutine scheduling its next delay;
- clock movement larger than `INT32_MAX` without signed half-range semantics;
- ordinary 32-bit wrap boundary;
- wrap occurring inside a due scan while a resumed coroutine re-arms;
- no premature firing of another timer spanning that same wrap;
- exact full-range `max_delay_ms` progress in chunks;
- max-delay overshoot after a complete clock wrap;
- 500 deterministic full-range reference rounds using a 64-bit logical elapsed counter only in the test oracle.

### Extended wrap stress

The 1.0.1 fix was additionally exercised locally with **10,000 wrap-during-`poll()` cycles** involving simultaneous due timers, a timer spanning the wrap, and a resumed coroutine immediately registering its next delay.

Result: **passed without premature firing or leaked timer/frame state**.

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

Version 1.0.1 additionally guarantees that a due scan does not continue pairing a stale pre-wrap `now` with a newer epoch after a resumed coroutine observes the wrap and re-arms.

Verified cases include ordinary wrap, wrap-during-resume/re-arm, `UINT32_MAX` single delay, large poll gaps, and full-wrap overshoot.

## Performance / RAM / Flash

See `BENCHMARKS.md` for methodology and capacity data. The published nanosecond table is retained as the reproducible **1.0.0 baseline** and is not relabeled as a 1.0.1 measurement.

Same-environment local comparison of the 1.0.1 fix showed:

- no meaningful regression in the common no-deadline-due `poll()` fast path;
- approximately **+42 B `.text`** in the size-oriented host proxy;
- **no `.bss` increase**;
- a small due/expire-path cost from one predictable epoch check after a resumed timer;
- the scan restart itself is only taken on the rare epoch change during that scan.

GitHub CI also passed the size-oriented build. Absolute linker totals from runner images/toolchain revisions are not compared directly against the historical 1.0.0 local numbers.

These remain host regression measurements and structure-level estimates, not MCU timing or exact linker guarantees.

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

There is **no public API change in 1.0.1**.

No priority queue, STL dynamic container, executor, RTOS dependency, dynamic scheduler, or timing wheel is used.

## Final review checklist

Reviewed specifically for:

- stale nearest-deadline state;
- missed/late timers;
- same-deadline behavior;
- ordinary wraparound and full-range delay behavior;
- wrap occurring during a resumed coroutine's re-arm;
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
