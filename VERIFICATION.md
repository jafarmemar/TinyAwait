# Verification Report — TinyAwait 1.1.0

This report describes the checks used for TinyAwait 1.1.0, which changes coroutine-frame storage from equal-size slots to a fixed-memory variable-size arena.

The timer scheduler and public coroutine syntax are unchanged.

## Configuration under review

- header-only C++20 implementation
- fixed total frame-memory arena
- variable-size coroutine frames
- fixed maximum live-frame count
- no heap fallback
- integer-millisecond `co_await`
- parent/child and detached `Async` tasks
- wrap-safe full-range `uint32_t` timing
- nearest-deadline `poll()` fast path

The public programming model remains:

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

## Frame arena design

TinyAwait receives the compiler-requested coroutine frame size and allocates an aligned span from one static arena.

The common paths are intentionally short:

- a fresh frame extends the bump frontier;
- a LIFO release retreats the bump frontier;
- non-LIFO releases become address-sorted free spans;
- adjacent free spans are merged;
- free space at the arena tail is reclaimed when needed.

Free-span metadata is stored inside memory that is already free. Metadata is read and written with byte copies, so the allocator does not depend on creating long-lived metadata objects inside the coroutine storage.

`TINYAWAIT_MAX_TASKS` still limits the number of live frames. `TINYAWAIT_FRAME_POOL_BYTES` can set the total byte budget directly. Existing `TINYAWAIT_FRAME_SIZE` configurations remain accepted and are used to derive the default total budget when the explicit pool setting is absent.

## Local engineering validation

The allocator and coroutine lifecycle changes were cross-checked with GCC and Clang at `-O0`, `-O2`, and `-Os`, using warnings-as-errors with exceptions and RTTI disabled.

Targeted validation covered:

- frames larger than the old 128-byte default slot;
- large nested parent/child coroutines;
- large detached coroutines;
- live-frame count exhaustion;
- byte-budget exhaustion;
- explicit and legacy pool configuration;
- very small legacy `TINYAWAIT_FRAME_SIZE` values;
- arena alignment and bounds;
- overlapping-live-frame detection;
- split-span reuse;
- adjacent-span coalescing;
- non-LIFO release order;
- complete arena recovery after fragmented use;
- one million deterministic variable-size allocator operations;
- repeated no-heap coroutine lifecycles.

Representative allocator and integration tests were also run with AddressSanitizer and UndefinedBehaviorSanitizer without reported errors.

## Repository CI release gate

The release commit is required to pass all configured GitHub Actions jobs before it is merged:

- full host suite with GCC;
- full host suite with Clang;
- allocator-focused tests with GCC and Clang at `-O0`, `-O2`, and `-Os`;
- ASan/UBSan build and test run;
- size-oriented build;
- all Arduino examples compiled with Arduino-ESP32 3.3.11.

Timing benchmarks are not used as pass/fail thresholds because small host timing differences are noisy.

## Host test suite

The CMake suite contains 16 tests:

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
- `test_variable_frames`
- `test_legacy_small_pool`

### Variable-frame coverage

`test_frame_size` verifies that a coroutine larger than the legacy per-frame setting can run when the shared arena has enough room, and that a frame larger than the entire arena fails through the configured error hook.

`test_variable_frames` exercises large parent/child and detached coroutine lifetimes through the real C++ coroutine allocation and destruction path.

`test_freelist` stress-tests the arena directly with variable-size requests and validates alignment, bounds, non-overlap, active-frame accounting, split reuse, coalescing, full recovery, and the live-frame limit.

`test_legacy_small_pool` protects source compatibility for very small legacy frame settings.

### Timer and wrap coverage

The existing scheduler tests continue to verify:

- zero and short delays;
- sequential and nested delays;
- same-deadline timers;
- insertion of an earlier deadline;
- deadline recomputation after resume;
- chained scheduling from a resumed coroutine;
- ordinary 32-bit clock wraparound;
- wrap occurring while a resumed coroutine re-arms;
- no premature firing of another timer spanning that wrap;
- full `UINT32_MAX` delay;
- sparse polling and full-wrap overshoot;
- deterministic randomized full-range schedules.

The 1.0.1 wrap-during-rearm regression remains part of the permanent suite.

## No-heap behavior

`Async::promise_type` supplies its own coroutine frame allocator. The implementation has no fallback to global `new`, `malloc`, or a dynamic container.

The no-heap regression test instruments global allocation while running repeated nested TinyAwait sequences and verifies that TinyAwait does not add global heap allocations.

## Performance and size review

The timer hot path is unchanged from 1.0.1.

In the same-host allocator comparison, isolated variable-size allocation is slower than the old equal-slot free-list. A complete create/suspend/resume/destroy coroutine cycle remained effectively unchanged within normal host measurement noise. The size-oriented host proxy adds roughly 0.55 KiB of linked code.

Those results are documented in [BENCHMARKS.md](BENCHMARKS.md). They are regression measurements, not MCU timing or Flash guarantees.

## Known limits reviewed for release

- TinyAwait is cooperative, single-threaded, and not ISR-safe.
- `poll()` is not reentrant.
- The frame arena has a fixed build-time size and never grows.
- Variable-size allocation can suffer external fragmentation; a frame needs one sufficiently large contiguous span.
- Live coroutine frames cannot be compacted or moved.
- The timer table is scanned when at least one deadline is due.
- All TinyAwait configuration macros must match across translation units.
- A production `TINYAWAIT_ON_ERROR()` hook should be fatal/non-returning.
- Over-aligned coroutine locals remain compiler- and ABI-dependent and should be verified on the target toolchain.
- Exact MCU timing, RAM placement, and Flash size require measurement with the target compiler and hardware.

## Final release checklist

Before publishing 1.1.0:

- all CI jobs must be green on the exact merge candidate;
- version metadata must agree across CMake, Arduino, and PlatformIO files;
- README, benchmarks, changelog, and this report must describe the variable-frame model;
- the release tag must point to the merged `main` commit;
- no temporary release helper should remain in the repository after the release is confirmed.
