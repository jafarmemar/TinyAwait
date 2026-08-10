# Changelog

## 1.0.0 - 2026-08-10

- Initial public release of TinyAwait.
- Added heap-free, fixed-capacity C++20 coroutine delays for embedded systems.
- Added nested `co_await child()` support.
- Added wrap-safe full-range `uint32_t` millisecond delays.
- Added an O(1) nearest-deadline `poll()` fast path while no timer is due.
- Added an intrusive O(1) coroutine-frame free-list allocator.
- Added host tests, sanitizer coverage, randomized timer verification, capacity and wraparound tests, no-heap verification, multi-TU coverage, and Arduino-ESP32 compile verification.
- Added reproducible CPU/RAM/Flash benchmark documentation and examples.
