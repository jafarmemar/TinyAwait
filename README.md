# TinyAwait

**Tiny, heap-free C++20 `co_await` delays for microcontrollers and embedded systems.**

[![CI](https://github.com/jafarmemar/TinyAwait/actions/workflows/ci.yml/badge.svg)](https://github.com/jafarmemar/TinyAwait/actions/workflows/ci.yml)

TinyAwait lets embedded code wait without blocking the rest of the application:

```cpp
Async delayedAction() {
    outputOn();
    co_await 500;
    outputOff();
}
```

The delay is in milliseconds. Only the current coroutine pauses; the main loop and other TinyAwait tasks keep running.

TinyAwait is header-only, uses fixed total memory, does not fall back to the heap, and does not create threads or RTOS tasks.

> **Current version: 1.1.0.**

## Highlights

- `co_await 500;` for readable non-blocking delays
- `co_await child();` for simple parent/child sequencing
- C++20, header-only
- fixed total coroutine-frame memory
- variable-size coroutine frames inside that fixed budget
- no heap fallback or dynamic STL container
- 32 simultaneously live coroutine frames by default
- full `uint32_t` single-delay range
- wrap-safe 32-bit millisecond timing
- O(1) nearest-deadline `poll()` fast path while no timer is due
- no thread, mutex, lock, executor, or RTOS dependency

## Quick start

### Generic embedded target

```cpp
#include <cstdint>

std::uint32_t boardMillis();
#define TINYAWAIT_NOW_MS() boardMillis()
#include <TinyAwait.h>

Async delayedWork() {
    co_await 500;
}

void startApplication() {
    delayedWork();
}

void serviceLoop() {
    tinyawait::poll();
}
```

### Arduino-compatible target

When `ARDUINO` is defined, TinyAwait uses `millis()` automatically:

```cpp
#include <TinyAwait.h>

Async delayedWork() {
    co_await 500;
}

void setup() {
    delayedWork();
}

void loop() {
    tinyawait::poll();
}
```

The board toolchain still needs C++20 coroutine support.

## Child coroutines

```cpp
Async childDelay() {
    co_await 200;
}

Async parentDelay() {
    co_await childDelay();
    co_await 800;
}
```

The parent stays suspended until the child finishes. The child's frame is then released before the parent continues.

Calling an `Async` function without `co_await` intentionally starts it as a detached task:

```cpp
childDelay();
```

That is useful when the caller should continue immediately.

## Main API

```cpp
Async someTask();
tinyawait::poll();
tinyawait::max_delay_ms;
tinyawait::frame_pool_bytes;
```

Inside an `Async` function:

```cpp
co_await 0;
co_await 1;
co_await 500;
co_await 1000;
co_await child();
```

Delay operands must be integral and in the range `0..4,294,967,295`. Invalid values use `TINYAWAIT_ON_ERROR()` instead of wrapping silently.

## Maximum delay

```text
4,294,967,295 ms
= 49 days, 17 hours, 2 minutes, 47.295 seconds
```

```cpp
co_await tinyawait::max_delay_ms;
```

The scheduler preserves the full 32-bit range across clock wraparound. While timers are active, `poll()` must run often enough that a complete 32-bit millisecond clock cycle does not pass between clock observations.

## Memory and capacity

TinyAwait 1.1.0 uses one fixed memory arena for coroutine frames. Each coroutine receives the amount of space requested by the compiler instead of occupying an equally sized slot.

The default configuration is:

```cpp
#define TINYAWAIT_MAX_TASKS 32
#define TINYAWAIT_FRAME_SIZE 128
```

For compatibility with earlier releases, those defaults produce roughly a 4 KiB frame arena. `TINYAWAIT_FRAME_SIZE` is kept as a legacy configuration input; **it is no longer a per-coroutine size limit**.

For new projects, the clearest configuration is an explicit total frame-memory budget:

```cpp
#define TINYAWAIT_MAX_TASKS 16
#define TINYAWAIT_FRAME_POOL_BYTES 2048
#include <TinyAwait.h>
```

`TINYAWAIT_MAX_TASKS` limits the number of live coroutine frames. `TINYAWAIT_FRAME_POOL_BYTES` limits their combined memory. `tinyawait::frame_pool_bytes` exposes the usable aligned pool size at compile time.

A larger coroutine can therefore coexist with smaller ones without forcing every task to reserve the same large slot. For example, a 300-byte frame can use about 300 bytes of the pool while simple tasks continue to use much less.

The arena never grows and never falls back to `malloc` or the global heap.

### Fragmentation

Variable-size allocation can leave free gaps when tasks finish in a different order from the one in which they were created. TinyAwait keeps free spans sorted, merges adjacent spans, and reclaims free space at the end of the arena. Even so, a new frame still needs one contiguous span large enough to hold it. In a heavily fragmented workload, allocation can fail even when the sum of all free bytes is larger than the requested frame.

For most small embedded workloads with short-lived or naturally nested tasks, the common bump and LIFO paths remain simple and fast. Applications with unusual long-lived mixed-size workloads should leave reasonable headroom in the frame pool and test their real task pattern.

## Failure handling

TinyAwait uses `TINYAWAIT_ON_ERROR()` when:

- the live-task limit is reached;
- the frame arena cannot provide a large enough contiguous span;
- a delay value is invalid;
- an unhandled coroutine exception reaches TinyAwait.

The default behavior is fail-fast. A product can provide its own fatal handler:

```cpp
#define TINYAWAIT_ON_ERROR() myFatalHandler()
#include <TinyAwait.h>
```

A production error hook should not return.

## Clock integration

### ESP-IDF

```cpp
#include "esp_timer.h"
#include <cstdint>

#define TINYAWAIT_NOW_MS() \
    static_cast<std::uint32_t>(esp_timer_get_time() / 1000ULL)
#include "TinyAwait.h"
```

### STM32 HAL / ARM Cortex-M

```cpp
#include "stm32xxxx_hal.h"
#define TINYAWAIT_NOW_MS() HAL_GetTick()
#include "TinyAwait.h"
```

### Raspberry Pi Pico SDK

```cpp
#include "pico/time.h"
#include <cstdint>

#define TINYAWAIT_NOW_MS() \
    static_cast<std::uint32_t>(time_us_64() / 1000ULL)
#include "TinyAwait.h"
```

Any other target can provide a monotonic `uint32_t` millisecond source.

## Compatibility

| Target / ecosystem | Status |
|---|---|
| Linux x86_64 / GCC | Host test suite in CI |
| Linux x86_64 / Clang | Host test suite in CI |
| Arduino examples / Arduino-ESP32 3.3.11 | Compile-checked in CI |
| Other C++20-capable Arduino cores | Expected; toolchain support required |
| ESP-IDF / ESP32 | Expected with a C++20-capable toolchain |
| STM32 / ARM Cortex-M | Expected with a C++20-capable toolchain |
| RP2040 / RP2350 | Expected with a C++20-capable toolchain |
| Embedded RISC-V | Expected with a C++20-capable toolchain |
| Classic AVR Uno/Nano/Mega toolchains | Not advertised as supported |

`Expected` means the design is portable to the target, not that the target has been hardware-tested.

## Multiple translation units

TinyAwait uses inline state so one configured instance is shared across translation units. All TinyAwait configuration macros must have the same values in every translation unit.

For larger projects, put the configuration and `#include <TinyAwait.h>` in one project header and include that everywhere.

## Performance

The timer scheduler in 1.1.0 keeps the nearest-deadline fast path from 1.0.x. Repeated `poll()` calls still return in O(1) while every active timer is in the future.

The new frame allocator favors the common embedded lifecycle:

- new frames normally extend a compact bump frontier;
- LIFO releases normally retreat that frontier directly;
- released holes are reused and adjacent holes are merged;
- searching free holes is only needed after non-LIFO releases.

A same-host regression comparison found no meaningful change in end-to-end create/suspend/resume/destroy time. Isolated allocator microbenchmarks are somewhat slower than the old equal-slot free-list, which is the expected cost of supporting variable frame sizes. The size-oriented host proxy also adds roughly 0.55 KiB of code. These measurements are regression signals, not MCU timing guarantees.

Full methodology and the historical scheduler baseline are in [BENCHMARKS.md](BENCHMARKS.md).

## Installation

### Arduino

Install the repository ZIP with **Sketch → Include Library → Add .ZIP Library...**, or copy the project into the Arduino libraries directory.

### PlatformIO

`library.json` is included. The consuming project must enable C++20 because `co_await` appears in application code as well as the library.

### CMake / generic C++

Copy `src/TinyAwait.h` into your include path, or use the provided interface target:

```cmake
target_link_libraries(your_target PRIVATE TinyAwait::TinyAwait)
```

When TinyAwait is included as a subproject, tests and benchmarks are disabled by default.

## Design limits

TinyAwait deliberately stays smaller than a general async framework:

- fixed total frame-memory budget;
- fixed maximum live-task count;
- millisecond resolution;
- `Async` is void-only;
- no cancellation, futures, queues, mutexes, executors, or thread pool;
- single-threaded and not ISR-safe;
- `poll()` is not reentrant;
- variable-size frame storage can fragment under non-LIFO lifetimes;
- over-aligned coroutine locals remain toolchain-dependent and should be verified on the target compiler.

## Tests

The host suite contains 16 tests covering ordinary and nested delays, detached tasks, capacity and frame-arena exhaustion, variable-size frames, legacy small-memory configuration, no-heap behavior, randomized allocator reuse, timer stress, full-range delay handling, 32-bit clock wraparound, nearest-deadline behavior, and shared state across multiple translation units.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CI runs the full suite with GCC and Clang, allocator-focused tests at `-O0`, `-O2`, and `-Os`, ASan/UBSan, a size-oriented build, and all four Arduino examples.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for coding style, test expectations, and benchmark guidelines.

## License

MIT. See [LICENSE](LICENSE).

---

Maintained by [@jafarmemar](https://github.com/jafarmemar).
