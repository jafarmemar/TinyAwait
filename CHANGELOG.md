# Changelog

## 1.2.0 - 2026-08-10

- Added a wrap-safe nearest-deadline fast path so repeated `poll()` calls normally avoid scanning the fixed timer table until the nearest timer can be due.
- Replaced the O(N) coroutine-frame allocation/free search with an O(1) bump-plus-intrusive-free-list allocator; free-list links live inside free frame storage and there is still no heap fallback.
- Preserved the full `uint32_t` delay range, including wraparound and sparse-poll overshoot cases that invalidate simpler signed-deadline/countdown approaches.
- Added dedicated nearest-deadline and free-list stress tests, including same-deadline timers, earlier insertion, full-range delays, wraparound, allocator exhaustion/reuse, cycle detection, and deterministic randomized reuse.
- Expanded host performance measurements across capacities 1, 4, 8, 16, 32, 64, 128, 256, and 1000, with independent A/B/C/D measurements for baseline, each optimization alone, and both combined.
- Updated CI size builds to include LTO while keeping performance timing out of pass/fail thresholds.

## 1.1.1 - 2026-08-10

- Added an idle `poll()` fast path with no timer-table scan when nothing is scheduled.
- Added range checking for integral delay values; negative values and values above `uint32_t` range use the configured error path instead of silently wrapping.
- Expanded host coverage with deterministic randomized scheduling and multi-translation-unit tests.
- Made CMake consumer-friendly: namespaced interface target, no global C++ standard mutation, and tests/benchmarks disabled by default when used as a subproject.
- Simplified the public examples to `SingleDelay`, `RepeatingDelay`, `SequentialDelays`, and `NestedDelay`.
- Made Arduino/PlatformIO metadata and documentation platform-neutral while keeping Arduino-ESP32 as a pinned CI compile target.

## 1.1.0 - 2026-08-10

- Added real parent/child `co_await` support.
- Set the default simultaneous task capacity to 32.
- Added full-range 32-bit delay handling and wraparound tests.
