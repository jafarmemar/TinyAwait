# Changelog

## 1.1.1 - 2026-08-11

- Simplified frame-memory configuration around `TINYAWAIT_FRAME_POOL_BYTES`.
- TinyAwait no longer defines `TINYAWAIT_FRAME_SIZE` for new code.
- Existing projects that still define `TINYAWAIT_FRAME_SIZE` remain source-compatible through a small compatibility path.
- The default frame budget still scales with `TINYAWAIT_MAX_TASKS`, so reducing the task limit continues to reduce reserved RAM automatically.
- Renamed frame-allocation tests around the shared-pool model and kept a dedicated legacy-configuration regression test.
- Simplified README examples so normal use needs no memory macro.
- Scheduler, timing, coroutine syntax, allocator behavior, and no-heap guarantees are unchanged from 1.1.0.

## 1.1.0 - 2026-08-11

- Replaced the fixed per-coroutine frame slots with one fixed-memory arena that stores variable-size coroutine frames.
- Added `TINYAWAIT_FRAME_POOL_BYTES` for an explicit total frame-memory budget.
- Kept `TINYAWAIT_FRAME_SIZE` as a backward-compatible configuration input; it no longer acts as a hard size limit for each individual coroutine.
- Kept `TINYAWAIT_MAX_TASKS` as the maximum number of simultaneously live coroutine frames.
- Added reuse of released spans, adjacent-span coalescing, and lazy reclaim of free space at the arena tail without adding a heap fallback.
- Added large-frame parent/child and detached-task coverage, small legacy-configuration coverage, and expanded allocator stress tests.
- Expanded CI with allocator-focused GCC and Clang checks at `-O0`, `-O2`, and `-Os` in addition to the full host suites, sanitizers, size build, and Arduino-ESP32 example builds.
- Updated memory, performance, failure-mode, and configuration documentation for the new arena model.
- Preserved the existing `Async`, `co_await`, `tinyawait::poll()`, timing, and clock-wrap behavior.

## 1.0.1 - 2026-08-11

- Fixed a rare 32-bit clock-wrap bug when a coroutine resumed inside `poll()` crossed the wrap boundary and immediately registered another delay.
- Prevented the due-timer scan from combining a pre-wrap `now` value with a post-wrap clock epoch; the scan now restarts from the updated clock state only when that rare epoch transition occurs.
- Added a permanent regression case to `test_next_deadline` covering wrap-during-resume/re-arm behavior with another timer spanning the same wrap.
- Verified the fix with GCC 14.2 and Clang 17 at `-O0`, `-O2`, and `-Os`, plus ASan/UBSan and extended wrap stress.
- GitHub CI passed host GCC/Clang tests, sanitizers, the size-oriented build, and all Arduino-ESP32 examples before release preparation.
- Public API and configuration remain unchanged from 1.0.0.

## 1.0.0 - 2026-08-10

- Initial public release of TinyAwait.
- Added heap-free, fixed-capacity C++20 coroutine delays for embedded systems.
- Added nested `co_await child()` support.
- Added wrap-safe full-range `uint32_t` millisecond delays.
- Added an O(1) nearest-deadline `poll()` fast path while no timer is due.
- Added an intrusive O(1) coroutine-frame free-list allocator.
- Added host tests, sanitizer coverage, randomized timer verification, capacity and wraparound tests, no-heap verification, multi-TU coverage, and Arduino-ESP32 compile verification.
- Added reproducible CPU/RAM/Flash benchmark documentation and examples.
