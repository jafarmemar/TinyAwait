# Verification Report — TinyAwait 1.1.2

TinyAwait 1.1.2 keeps the scheduler, coroutine syntax, variable-size frame arena, memory model, and public API from 1.1.1. This release replaces the hard language-version gate with direct coroutine capability checks and synchronizes public documentation and package metadata with that requirement while keeping C++20 as the current source/build policy.

## Configuration under review

- header-only C++20 source/build mode
- direct compiler and standard-library coroutine capability checks
- fixed total frame-memory arena
- variable-size coroutine frames
- fixed maximum live-frame count
- no heap fallback
- integer-millisecond `co_await`
- parent/child and detached `Async` tasks
- wrap-safe full-range `uint32_t` timing
- nearest-deadline `poll()` fast path

The normal programming model remains:

```cpp
Async task() {
    co_await 500;
}

void loop() {
    tinyawait::poll();
}
```

## Toolchain capability checks

TinyAwait checks the coroutine capabilities it actually uses rather than rejecting a toolchain only from its `__cplusplus` value.

At include time it verifies:

- compiler coroutine support through `__cpp_impl_coroutine`;
- availability of the standard `<coroutine>` header when `__has_include` is available;
- standard-library coroutine support through `__cpp_lib_coroutine`.

If a required capability is missing, compilation stops with a TinyAwait-specific error. The current implementation still contains C++20 language constructs and the CMake interface target still requests `cxx_std_20`; this capability check does not claim C++17 compatibility.

## Memory configuration

Current code has two memory-related settings:

```cpp
TINYAWAIT_MAX_TASKS
TINYAWAIT_FRAME_POOL_BYTES
```

No memory macro is required for ordinary use. The default pool budget scales with `TINYAWAIT_MAX_TASKS`, preserving the previous RAM behavior for small configurations.

`TINYAWAIT_FRAME_SIZE` is deprecated and is no longer defined by TinyAwait or shown as a current configuration option. A compatibility path still accepts it when an older project defines it and no explicit pool budget is provided.

The compatibility path is covered by a dedicated regression test so deprecation does not silently break existing firmware builds.

## Frame arena design

TinyAwait receives the compiler-requested coroutine frame size and allocates an aligned span from one static arena.

The common paths are intentionally short:

- fresh frames extend the bump frontier;
- LIFO releases retreat the bump frontier;
- non-LIFO releases become address-sorted free spans;
- adjacent free spans are merged;
- free space at the arena tail is reclaimed when needed.

Free-span metadata lives inside memory that is already free and is read and written with byte copies.

## Host validation

The current code is checked with GCC and Clang. Allocator-focused tests run at `-O0`, `-O2`, and `-Os`, with warnings treated as errors and exceptions/RTTI disabled.

Coverage includes:

- default configuration;
- explicit `TINYAWAIT_FRAME_POOL_BYTES` budgets;
- deprecated configuration compatibility;
- frames larger than the old fixed-slot size;
- large nested parent/child coroutines;
- detached coroutines;
- live-frame count exhaustion;
- byte-budget exhaustion;
- arena alignment and bounds;
- overlapping-live-frame detection;
- split-span reuse;
- adjacent-span coalescing;
- non-LIFO release order;
- complete arena recovery after fragmented use;
- one million deterministic variable-size allocator operations;
- repeated no-heap coroutine lifecycles.

ASan/UBSan is also part of the repository CI gate.

## Repository CI gate

Changes intended for `main` should pass:

- full host suite with GCC;
- full host suite with Clang;
- allocator-focused tests with GCC and Clang at `-O0`, `-O2`, and `-Os`;
- ASan/UBSan build and test run;
- size-oriented build;
- all Arduino examples compiled with Arduino-ESP32 3.3.11.

Timing benchmarks are not used as pass/fail thresholds because very small host timing differences are noisy.

## Host test suite

The CMake suite contains 16 tests:

- `test_basic`
- `test_wraparound`
- `test_capacity`
- `test_no_heap`
- `test_stress`
- `test_frame_pool`
- `test_nested`
- `test_max_delay`
- `test_default_capacity`
- `test_delay_validation`
- `test_randomized`
- `test_multitu`
- `test_next_deadline`
- `test_freelist`
- `test_variable_frames`
- `test_legacy_config`

`test_frame_pool` verifies explicit shared-pool sizing, successful use of a frame larger than the old slot model, and failure when a frame is larger than the whole pool.

`test_variable_frames` exercises large parent/child and detached coroutine lifetimes through the real C++ coroutine allocation and destruction path.

`test_freelist` validates alignment, bounds, non-overlap, active-frame accounting, split reuse, coalescing, full recovery, and live-frame limits under variable-size stress.

`test_legacy_config` protects source compatibility for projects that still define the deprecated frame-size setting.

## Timer and wrap coverage

The scheduler tests continue to verify:

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

`Async::promise_type` supplies its own coroutine frame allocator. There is no fallback to global `new`, `malloc`, or a dynamic container.

The no-heap regression test instruments global allocation while running repeated nested TinyAwait sequences.

## ESP32-S3 hardware validation

TinyAwait 1.1.2 commit `868304d548074a07274665ebe4d3fb26854c4f14` is **hardware validated** on a physical ESP32-S3 revision 2 using PlatformIO Core 6.1.19, Arduino-ESP32 3.3.11, ESP-IDF 5.5.5, C++20, and USB-to-UART Serial at 115200 baud. The tested target was `esp32-s3-devkitc-1`, 240 MHz, 8 MB flash, with no active PSRAM reported by the firmware.

The repository preserves the validation firmware, reports, and raw Serial captures under [`hardware/esp32-s3/`](hardware/esp32-s3/). The hardware claim is tied to the exact TinyAwait revision and recorded toolchain; it should not be generalized to untested ESP32 variants or newer TinyAwait commits without rerunning the harness.

### V1 integration validation

The usage-oriented V1 suite was run three times consecutively on the physical board without rebooting. All three runs reported 7/7 PASS and covered:

- immediate `co_await 0`;
- a normal non-blocking delay while Arduino `loop()` and an independent background coroutine continued progressing;
- sequential delays;
- nested parent/child coroutine sequencing;
- detached concurrent tasks with deadline-ordered completion;
- repeated awaits;
- scheduler cleanup after each run;
- live Serial `ping` responsiveness while the suite was active.

Each run ended with `active_frames=0` and `active_timers=0`. No watchdog, panic, Guru Meditation, brownout, illegal instruction, or unexpected reset was observed.

### V2 stress validation

The extended V2 suite completed **9/9 PASS** in `281216 ms` (4 min 41.216 s). It added target-side coverage for edge cases and sustained workloads that complement the host suite:

- capacity boundary: all 32 configured live tasks were accepted and task 33 took the expected error path;
- invalid delay validation: negative and oversized delays produced exactly two additional expected errors;
- `uint32_t` wraparound and `tinyawait::max_delay_ms`: no premature completion, with completion after the full `UINT32_MAX` elapsed range using a deterministic test clock on the MCU;
- 600 mixed-frame batches with 5,400 real coroutine completions and a deterministic checksum;
- 250,000 direct variable-size allocator operations with bounds, alignment, overlap, canary, active-frame, and timer checks;
- successful whole-pool allocation after allocator fuzz, demonstrating complete arena recovery;
- allocator free-list maximum search depth of 18 in the recorded workload;
- 600,000/600,000 real detached timer/coroutine completions across the main and repeat scheduler passes;
- current free heap `353888 -> 353888` and largest free block `294900 -> 294900`, both with zero delta after drain;
- a 180-second sustained mixed workload with occasional nested parent/child coroutines and `87239/87239` tasks completed;
- repeated live Serial responsiveness during mixed-frame, scheduler, heap, and soak phases;
- final `active_frames=0`, `active_timers=0`;
- 3 expected TinyAwait errors and 0 unexpected errors.

No unexpected reset, watchdog/WDT, Guru Meditation, panic/abort/exception output, brownout, or reboot loop was observed during the V2 capture.

The raw V2 log contains two cosmetic progress lines in the repeat scheduler pass where the cumulative completed counter is shown against the repeat-pass total (`550000 / 100000` and `600000 / 100000`). This is a reporting-only artifact in the validation harness: the final counters and pass criteria independently record `scheduler_started=600000` and `scheduler_completed=600000`, and the scheduler test itself passed with zero active frames/timers. The preserved harness and raw log intentionally retain the tested behavior rather than rewriting evidence after the run.

## Performance and size review

Version 1.1.2 does not change the allocator or scheduler runtime paths. The 1.1.0 performance comparison therefore remains applicable.

The later coroutine-capability detection change is compile-time-only and does not alter the scheduler, allocator, coroutine frame layout, or runtime timing paths.

The timer fast path is unchanged, and the variable-size arena keeps its bump, LIFO, free-span reuse, and coalescing behavior.

See [BENCHMARKS.md](BENCHMARKS.md) for measurements and limitations.

## Known limits

- TinyAwait is cooperative, single-threaded, and not ISR-safe.
- `poll()` is not reentrant.
- The current source/build policy is C++20 even though coroutine support is feature-detected directly.
- The frame arena has a fixed build-time size and never grows.
- Variable-size allocation can suffer external fragmentation; a frame needs one sufficiently large contiguous span.
- Live coroutine frames cannot be compacted or moved.
- The timer table is scanned when at least one deadline is due.
- All TinyAwait configuration macros must match across translation units.
- A production `TINYAWAIT_ON_ERROR()` hook should be fatal/non-returning.
- Over-aligned coroutine locals remain compiler- and ABI-dependent.
- Hardware validation currently covers the recorded ESP32-S3/Arduino-ESP32 configuration; exact MCU timing, RAM placement, Flash size, and ABI behavior on other targets still require target-specific measurement.

## Release checklist

Before a future release:

- all CI jobs must be green on the exact merge candidate;
- version metadata must agree across CMake, Arduino, and PlatformIO files;
- README, benchmarks, changelog, verification notes, package metadata, and Arduino keywords must agree with the public API and toolchain requirements;
- if a release changes scheduler, allocator, coroutine frame handling, error paths, or ESP32 toolchain requirements, rerun the relevant ESP32-S3 hardware harness and record the exact tested commit;
- the release tag must point to the intended merged `main` commit;
- no temporary release helper should remain after the release is confirmed.
