# TinyAwait

**Tiny, heap-free C++20 `co_await` delays for microcontrollers and embedded systems.**

[![CI](https://github.com/jafarmemar/TinyAwait/actions/workflows/ci.yml/badge.svg)](https://github.com/jafarmemar/TinyAwait/actions/workflows/ci.yml)

TinyAwait is a small header-only helper for readable, non-blocking delays:

```cpp
co_await 500;
```

The value is milliseconds. Only the current coroutine pauses; the rest of the application keeps running.

```cpp
Async delayedAction() {
    outputOn();
    co_await 500;
    outputOff();
}
```

It also supports simple parent/child sequencing:

```cpp
co_await child();
```

The core is platform-neutral. Arduino-compatible environments can use `millis()` automatically; other targets provide a monotonic millisecond clock through one macro. There is no heap fallback, no RTOS task per delay, no dynamic timer container, and no external library dependency.

> **Current version: 1.1.1.** Host tests are verified with GCC 14.2 and Clang 17. Arduino examples are compile-checked in CI using Arduino-ESP32 3.3.11 as one real C++20-capable Arduino target.

## Highlights

- `co_await 500;` for a non-blocking 500 ms delay
- `co_await child();` for nested async flow
- header-only C++20
- fixed, predictable memory
- heap-free TinyAwait execution path
- 32 simultaneously-live coroutine frames by default
- automatic frame/timer slot reuse
- full `uint32_t` single-delay range
- wrap-safe 32-bit millisecond timing
- O(1) idle `poll()` fast path when no timer is active
- no thread, RTOS task, lock, mutex, executor, or dynamic container

## Quick Start

### Generic embedded target

```cpp
#include <cstdint>

std::uint32_t boardMillis();
#define TINYAWAIT_NOW_MS() boardMillis()
#include <TinyAwait.h>

Async delayedWork() {
    co_await 500;
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

void loop() {
    tinyawait::poll();
}
```

The board/toolchain still needs C++20 coroutine support.

## Examples

| Example | Function | Purpose |
|---|---|---|
| [SingleDelay](examples/SingleDelay/SingleDelay.ino) | `singleDelay()` | One non-blocking delay, then continue. |
| [RepeatingDelay](examples/RepeatingDelay/RepeatingDelay.ino) | `repeatingDelay()` | Repeated timed work without blocking the main loop. |
| [SequentialDelays](examples/SequentialDelays/SequentialDelays.ino) | `sequentialDelays()` | Several delays written as a simple top-to-bottom sequence. |
| [NestedDelay](examples/NestedDelay/NestedDelay.ino) | `childDelay()` / `nestedDelay()` | Await a child coroutine, then continue in the parent. |

The sketches use GPIO only to make timing visible. TinyAwait itself is not tied to Arduino or ESP32.

## Child await

```cpp
Async childDelay() {
    co_await 200;
}

Async nestedDelay() {
    co_await childDelay();
    co_await 800;
}
```

While the child is running, the parent remains suspended. When the child finishes, its frame is released back to the fixed pool before the parent continues.

## API

```cpp
Async someTask();
tinyawait::poll();
tinyawait::max_delay_ms;
```

Inside an `Async` function:

```cpp
co_await 0;
co_await 1;
co_await 500;
co_await 1000;
co_await child();
```

Delay operands must be integral and in the range `0..4,294,967,295`. Negative values and wider values outside that range enter `TINYAWAIT_ON_ERROR()` instead of silently wrapping.

## Maximum delay

```text
4,294,967,295 ms
= 49 days, 17 hours, 2 minutes, 47.295 seconds
```

```cpp
co_await tinyawait::max_delay_ms;
```

The timer keeps remaining time in wrap-safe chunks. `poll()` must still run often enough that more than one complete 32-bit millisecond clock cycle does not pass between polls while timers are active.

## Capacity and memory

Defaults:

```cpp
#define TINYAWAIT_MAX_TASKS 32
#define TINYAWAIT_FRAME_SIZE 128
```

`TINYAWAIT_MAX_TASKS` is the maximum number of coroutine frames alive at the same time, not the number of delays you can execute over the lifetime of the firmware. Completed slots are reused immediately.

For a smaller target:

```cpp
#define TINYAWAIT_MAX_TASKS 8
#include <TinyAwait.h>
```

For a larger application:

```cpp
#define TINYAWAIT_MAX_TASKS 64
#include <TinyAwait.h>
```

The default fixed state is roughly **4.5 KiB** on a typical 32-bit target. See [BENCHMARKS.md](BENCHMARKS.md) for the breakdown.

Each compiler-generated coroutine frame must fit in `TINYAWAIT_FRAME_SIZE`. Complex parameters or locals can increase frame size.

## Failure hook

TinyAwait does not fall back to the heap when the frame pool is full or a frame is too large. Invalid delay values also use the same error path.

Default behavior is fail-fast. A product can provide its own fatal handler:

```cpp
#define TINYAWAIT_ON_ERROR() myFatalHandler()
#include <TinyAwait.h>
```

The production error hook should **not return**.

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

Any other target can provide its own monotonic `uint32_t` millisecond source.

## Compatibility

| Target / ecosystem | Status |
|---|---|
| Linux x86_64 / GCC 14.2 | **Verified — full host suite** |
| Linux x86_64 / Clang 17 | **Verified — full host suite** |
| Arduino examples / Arduino-ESP32 3.3.11 | **Compile-verified in CI** |
| Other C++20-capable Arduino cores | Expected; toolchain support required |
| ESP-IDF / ESP32 | Expected with a C++20-capable toolchain |
| STM32 / ARM Cortex-M | Expected with a C++20-capable toolchain |
| RP2040 / RP2350 | Expected with a C++20-capable toolchain |
| Embedded RISC-V | Expected with a C++20-capable toolchain |
| Classic AVR Uno/Nano/Mega toolchains | Not advertised as supported |

`Expected` does not mean compile-tested or hardware-tested.

## Multiple translation units

TinyAwait uses C++17/20 inline state so one configured instance is shared across translation units. A dedicated multi-TU test is part of the host suite.

All TinyAwait configuration macros must be identical in every translation unit. In larger projects, put the configuration and `#include <TinyAwait.h>` in one project header and include that everywhere.

## Performance

Host regression medians after the v1.1.1 idle fast path:

| Case | Median |
|---|---:|
| `poll()` with 0 active timers | **0.574 ns/call** |
| `poll()` with 1 active timer | **11.989 ns/call** |
| `poll()` with 32 active timers | **27.880 ns/call** |

These are x86_64 GCC 14.2 measurements, not MCU timing claims. The idle fast path adds only a tiny amount of linked code while avoiding the fixed-table scan when nothing is scheduled.

The no-heap test recorded **0 additional global heap allocations** over 10,000 nested TinyAwait sequences.

See [BENCHMARKS.md](BENCHMARKS.md) and [VERIFICATION.md](VERIFICATION.md).

## Installation

### Arduino

Install the repository ZIP with **Sketch → Include Library → Add .ZIP Library...**, or copy the project into your Arduino libraries directory.

### PlatformIO

`library.json` is included. The project using TinyAwait must enable C++20 because `co_await` appears in application code as well as the library.

### CMake / generic C++

Copy `src/TinyAwait.h` into your include path or add this repository as an interface dependency. Enable C++20:

```cmake
target_compile_features(your_target PRIVATE cxx_std_20)
```

## Design limits

TinyAwait stays small by intentionally not becoming a general async framework:

- fixed maximum live-frame count and fixed frame slot size
- millisecond resolution
- `Async` is void-only
- no cancellation, futures, queues, mutexes, executors, or thread pool
- single-threaded; not ISR-safe
- `poll()` is not reentrant
- over-aligned coroutine locals are toolchain-dependent; keep locals at normal ABI alignment unless verified on the target compiler

## Tests

The 12-test host suite covers basic/sequential delays, 32-bit wraparound, capacity failure, no-heap behavior, stress, frame-size failure, nested awaiting, maximum delay, default capacity, invalid delay values, deterministic randomized schedules, and multi-translation-unit state sharing.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CI runs GCC, Clang, ASan/UBSan, a size-optimized build, and all four Arduino examples.

## Contributing

Keep the core focused. Changes that add code, RAM, or runtime work should have a clear use case and tests or measurements where appropriate.

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT. See [LICENSE](LICENSE).

## References

- Arduino library specification: https://docs.arduino.cc/arduino-cli/library-specification
- PlatformIO library manifest: https://docs.platformio.org/en/latest/manifests/library-json/index.html
- Espressif ESP-IDF C++ support: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/cplusplus.html
- Espressif timer API: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_timer.html
- Raspberry Pi Pico SDK time APIs: https://www.raspberrypi.com/documentation/pico-sdk/hardware.html

---

Maintained by [@jafarmemar](https://github.com/jafarmemar).
