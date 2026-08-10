# TinyAwait

**TinyAwait — tiny, heap-free C++20 `co_await` delays for microcontrollers and embedded systems.**

[![CI](https://github.com/jafarmemar/TinyAwait/actions/workflows/ci.yml/badge.svg)](https://github.com/jafarmemar/TinyAwait/actions/workflows/ci.yml)

TinyAwait is a small header-only coroutine helper for readable, non-blocking delays in embedded C++:

```cpp
co_await 500;
```

The value is milliseconds. Only the current coroutine pauses; the rest of the application can keep running.

TinyAwait also supports simple parent/child sequencing:

```cpp
co_await child();
```

The core is platform-neutral. Arduino-compatible environments can use `millis()` automatically, while other targets provide a monotonic millisecond clock through one macro. This makes the same small API suitable for C++20-capable Arduino boards, ESP32, ARM/STM32, RP2040/RP2350, RISC-V, and other embedded toolchains.

There is no heap fallback, no RTOS task per delay, no dynamic timer container, and no external library dependency.

> **Current version:** 1.1.0. The host test suite is verified with GCC 14.2 and Clang 17. The Arduino examples are also compile-verified in CI using Arduino-ESP32 3.3.11 as one real embedded compile target. Other platforms are clearly marked as expected until tested with their own toolchains or hardware.

## Why TinyAwait?

Timed embedded code often starts simple and becomes harder to follow as timestamps, states, and branches accumulate. TinyAwait keeps short asynchronous sequences readable without blocking the main loop.

```cpp
Async delayedAction() {
    outputOn();
    co_await 500;
    outputOff();
}
```

The coroutine pauses at `co_await 500`; networking, sensors, GPIO handling, protocol work, or other application logic can continue elsewhere.

### Highlights

- `co_await 500;` for a non-blocking 500 ms delay
- `co_await child();` for nested async flow
- header-only C++20 implementation
- no external dependencies
- no TinyAwait heap allocation in the verified execution path
- fixed and predictable memory use
- 32 simultaneously-live coroutine frames by default
- timer and frame slots are reused automatically after completion
- maximum single delay: `4,294,967,295 ms`
- wrap-safe 32-bit millisecond timing
- no thread, RTOS task, lock, mutex, or executor per coroutine
- verified with `-fno-exceptions` and `-fno-rtti` on the host test compilers

## Quick Start

For a generic embedded target, provide a monotonic millisecond clock before including TinyAwait:

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

On an Arduino-compatible target, `millis()` is selected automatically:

```cpp
#include <TinyAwait.h>
```

Then call `tinyawait::poll()` regularly from `loop()` or from the cooperative execution context used by your application.

## Examples

The repository contains four small Arduino-style sketches. They are intentionally simple so each one demonstrates one idea:

| Example | Main function | What it demonstrates |
|---|---|---|
| [SingleDelay](examples/SingleDelay/SingleDelay.ino) | `singleDelay()` | One non-blocking delay, then continue. |
| [RepeatingDelay](examples/RepeatingDelay/RepeatingDelay.ino) | `repeatingDelay()` | Repeated timed work without blocking the main loop. |
| [SequentialDelays](examples/SequentialDelays/SequentialDelays.ino) | `sequentialDelays()` | Several delays written in a readable top-to-bottom sequence. |
| [NestedDelay](examples/NestedDelay/NestedDelay.ino) | `childDelay()` / `nestedDelay()` | Await a child coroutine, then continue in the parent. |

The sketches use Arduino GPIO only to make the timing visible. The TinyAwait API itself is not tied to Arduino or ESP32.

## Child await

An `Async` function can wait for another `Async` function:

```cpp
Async childDelay() {
    co_await 200;
}

Async nestedDelay() {
    co_await childDelay();
    co_await 800;
}
```

`nestedDelay()` pauses until `childDelay()` completes. The child frame is then returned to TinyAwait's fixed pool before the parent continues.

This is still single-threaded and cooperative. It does not create a new thread or RTOS task.

## API

The public API is intentionally small:

```cpp
Async someTask();
tinyawait::poll();
tinyawait::max_delay_ms;
```

Inside an `Async` function:

```cpp
co_await 0;          // immediate, no timer slot
co_await 1;          // 1 ms
co_await 500;        // 500 ms
co_await 1000;       // 1 second
co_await child();    // wait for another Async function
```

Integer delays are milliseconds.

## Maximum delay

```cpp
tinyawait::max_delay_ms
```

is:

```text
4,294,967,295 ms
= 49 days, 17 hours, 2 minutes, 47.295 seconds
```

So this is valid:

```cpp
co_await tinyawait::max_delay_ms;
```

TinyAwait tracks elapsed time in wrap-safe chunks, so the full `uint32_t` delay range works even when a 32-bit millisecond clock wraps.

`tinyawait::poll()` must still run regularly. More than one complete 32-bit clock cycle must not pass between calls to `poll()`.

## Default capacity: 32 tasks

The default is:

```cpp
#define TINYAWAIT_MAX_TASKS 32
```

This is the maximum number of TinyAwait coroutine frames that may be alive at the same time. It is **not** the total number of delays or `Async` functions you can use in a project.

A firmware can execute thousands of delays over time. Finished timer and frame slots are reused automatically.

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

Use the same configuration in every translation unit that includes TinyAwait.

### Fixed-memory behavior

The pool is static memory reserved at compile/link time. When a task finishes, its slot becomes immediately reusable by TinyAwait, but those bytes are not returned to the system heap.

That keeps the maximum RAM cost known in advance and avoids runtime allocation and fragmentation.

## Frame size

The default frame slot is:

```cpp
#define TINYAWAIT_FRAME_SIZE 128
```

Each compiler-generated coroutine frame must fit in one slot. Actual frame size depends on the compiler, target, optimization, parameters, and local variables.

If a frame is too large or all frame slots are occupied, TinyAwait does **not** fall back to the heap. The default behavior is fail-fast.

A project can provide its own failure hook:

```cpp
#define TINYAWAIT_ON_ERROR() myFatalHandler()
#include <TinyAwait.h>
```

## Clock integration

TinyAwait only needs a monotonic millisecond counter.

### Arduino-compatible environment

When `ARDUINO` is defined, TinyAwait uses `millis()` automatically:

```cpp
#include <TinyAwait.h>
```

This applies to Arduino-compatible environments whose C++ toolchain provides C++20 coroutine support.

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

### Generic embedded target

```cpp
#include <cstdint>

std::uint32_t boardMillis();
#define TINYAWAIT_NOW_MS() boardMillis()
#include "TinyAwait.h"
```

The target needs a monotonic millisecond counter and a toolchain with usable C++20 coroutine support.

## Compatibility

| Target / ecosystem | Status | Notes |
|---|---|---|
| Linux x86_64, GCC 14.2 | **Verified** | Full automated host suite passed. |
| Linux x86_64, Clang 17 | **Verified** | Full automated host suite passed. |
| Arduino examples via Arduino-ESP32 3.3.11 | **Compile-verified in CI** | All four supplied Arduino-style examples compile. |
| Other Arduino-compatible C++20 targets | **Expected with suitable toolchain** | `millis()` integration is automatic; coroutine support is still required. |
| ESP-IDF / ESP32 family | **Expected** | Use a current C++20-capable toolchain and `esp_timer_get_time()`. |
| STM32 / ARM Cortex-M | **Expected with suitable toolchain** | Map HAL or another monotonic tick. |
| Raspberry Pi RP2040 / RP2350 | **Expected with suitable toolchain** | Map the Pico SDK monotonic clock. |
| Embedded RISC-V | **Expected with suitable toolchain** | Provide a monotonic millisecond clock. |
| Classic AVR Uno/Nano/Mega toolchains | **Not advertised as supported** | Typical classic toolchains do not provide the required C++20 coroutine environment. |

`Expected` does not mean compile-tested or hardware-tested.

## Installation

### Arduino-compatible environments

TinyAwait is a normal source library; no precompiled firmware or architecture-specific binary is required.

Install the project ZIP with **Sketch → Include Library → Add .ZIP Library...**, or copy the `TinyAwait` folder into your Arduino libraries directory.

### PlatformIO

A `library.json` manifest is included for package metadata and discovery. You can use the repository directly as a library dependency. Your project must compile application code as C++20 because `co_await` appears in your own source code as well as in TinyAwait.

### Generic CMake / C++

Copy `src/TinyAwait.h` into your include path and enable C++20:

```cmake
target_compile_features(your_target PRIVATE cxx_std_20)
```

Define `TINYAWAIT_NOW_MS()` before including the header unless the environment already supplies Arduino `millis()` integration.

## Memory model

The fixed state is mainly:

```text
frame_pool = TINYAWAIT_MAX_TASKS × aligned TINYAWAIT_FRAME_SIZE
timers     = TINYAWAIT_MAX_TASKS × sizeof(TimerSlot)
frame_used = TINYAWAIT_MAX_TASKS × sizeof(bool)
```

With the default `32 × 128-byte` frame pool:

- frame payload storage: **4096 B**
- frame-use flags: typically **32 B**
- expected timer table on a typical 32-bit MCU: roughly **384 B**
- nominal fixed state: about **4512 B**, before target-specific padding/alignment

On the measured x86_64 host, `TimerSlot` is 16 B and `FrameSlot` is 128 B.

## Heap behavior

TinyAwait overrides coroutine frame allocation and serves frames from the fixed pool.

The host no-heap test runs 10,000 parent/child create/suspend/resume/complete sequences while global `operator new` is instrumented.

Measured TinyAwait-path result:

```text
0 additional global heap allocations
```

Code inside your coroutine can still allocate memory; TinyAwait only controls its own coroutine/timer path.

## Performance

These are x86_64 GCC 14.2 regression measurements, not MCU timing claims.

Median of five runs, 5,000,000 `poll()` calls per occupancy measurement:

| Case | Median |
|---|---:|
| `poll()` — 0 active timers, capacity 32 | **20.466 ns/call** |
| `poll()` — 1 active timer | **19.369 ns/call** |
| `poll()` — 32 active timers | **27.527 ns/call** |
| Coarse ready-coroutine resume estimate | **37.875 ns/resume** |

Measured coroutine frame sizes in the same host build:

- simple sleeper: **56 B**
- largest frame observed in the nested parent/child sample: **72 B**

More detail is in [BENCHMARKS.md](BENCHMARKS.md).

## Tests and CI

The automated suite covers:

- immediate coroutine start
- zero, 1 ms, and 500 ms awaits
- sequential awaits
- real `co_await child()` behavior
- parent continuation and child frame release
- 32 simultaneous default tasks
- deterministic capacity exhaustion
- timer/frame reuse
- maximum `uint32_t` delay
- 32-bit clock wraparound
- oversized-frame failure
- no TinyAwait heap allocation in 10,000 nested sequences
- repeated 32-way lifecycle stress
- GCC and Clang builds
- `-fno-exceptions`
- `-fno-rtti`
- AddressSanitizer
- UndefinedBehaviorSanitizer
- compilation of all four Arduino-style examples using a real C++20-capable Arduino toolchain target

Local host build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

See [VERIFICATION.md](VERIFICATION.md) for the recorded verification details.

## Design limits

TinyAwait stays small by leaving higher-level async features out of the core:

- C++20 and `<coroutine>` are required
- fixed maximum number of live frames
- fixed maximum frame size
- millisecond resolution
- `Async` is currently void-only
- no cancellation, futures, queues, mutexes, executors, or thread pool
- single-threaded and not ISR-safe
- `poll()` must not be called reentrantly

## Contributing

Keep the core focused. Features that add code size, RAM, or runtime cost should come with a clear use case and tests or measurements where appropriate.

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT. See [LICENSE](LICENSE).

## References

- Arduino library specification: https://docs.arduino.cc/arduino-cli/library-specification
- Espressif ESP-IDF C++ support: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/cplusplus.html
- Espressif timer API: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_timer.html
- PlatformIO library manifest: https://docs.platformio.org/en/latest/manifests/library-json/index.html
- Raspberry Pi Pico SDK time APIs: https://www.raspberrypi.com/documentation/pico-sdk/hardware.html

---

Maintained by [@jafarmemar](https://github.com/jafarmemar).
