# Changelog

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
