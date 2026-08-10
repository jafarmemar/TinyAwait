# Changelog

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
