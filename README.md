# TinyAwait

**TinyAwait — tiny, heap-free C++20 `co_await` delays for embedded systems.**

[![CI](https://github.com/jafarmemar/TinyAwait/actions/workflows/ci.yml/badge.svg)](https://github.com/jafarmemar/TinyAwait/actions/workflows/ci.yml)

TinyAwait is a small header-only coroutine helper for microcontrollers. It gives embedded C++ code a readable, non-blocking delay:

```cpp
co_await 500;
```

The number is milliseconds. While the coroutine is waiting, your main loop stays free for networking, sensors, GPIO, protocols, WebSockets, or other application work.

TinyAwait also supports simple parent/child sequencing:

```cpp
co_await child();
```

There is no heap fallback, no RTOS task per delay, no dynamic timer container, and no external library dependency.

> **Current version:** 1.1.0 — host-tested with GCC 14.2 and Clang 17, and compile-verified in GitHub Actions with Arduino-ESP32 3.3.11.

## Why TinyAwait?

A timed sequence is easy to understand when it reads from top to bottom. The usual embedded alternative is often a state machine spread across several branches and timestamps.

With TinyAwait:

```cpp
Async singleNonBlockingDelay() {
    ledOn();
    co_await 500;
    ledOff();
}
```

The delay is cooperative: only this coroutine pauses. The rest of the firmware keeps running.

### Highlights

- `co_await 500;` for a non-blocking 500 ms delay
- `co_await child();` for nested async flow
- header-only C++20 implementation
- no external dependencies
- no TinyAwait heap allocation in the verified execution path
- fixed and predictable memory use
- 32 simultaneously-live coroutine frames by default
- timer and frame slots are automatically reused after completion
- maximum single delay: `4,294,967,295 ms`
- wrap-safe 32-bit millisecond timing
- no thread, task, lock, mutex, or executor per coroutine
- verified with `-fno-exceptions` and `-fno-rtti` on the host test compilers

## Quick Start

```cpp
#include <TinyAwait.h>

// Change this to the LED/GPIO pin for your board.
constexpr int LED_PIN = 2;

Async repeatingNonBlockingDelay() {
    while (true) {
        digitalWrite(LED_PIN, HIGH);
        co_await 500;

        digitalWrite(LED_PIN, LOW);
        co_await 500;
    }
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    repeatingNonBlockingDelay();
}

void loop() {
    tinyawait::poll();

    // Wi-Fi
    // WebSocket
    // sensors
    // GPIO
    // application logic
}
```

`co_await 500` suspends only `repeatingNonBlockingDelay()`. It does **not** block `loop()`.

## Delay patterns in the examples

| Pattern | Example function | What it demonstrates |
|---|---|---|
| Single non-blocking delay | `singleNonBlockingDelay()` | Suspend one coroutine for a fixed delay, then continue. |
| Repeating non-blocking delay | `repeatingNonBlockingDelay()` | Repeat timed work without blocking the main loop. |
| Child delay step | `childDelayStep()` | Put a timed step inside an awaitable child coroutine. |
| Nested delay sequence | `nestedDelaySequence()` | Await a child coroutine, then continue with another delay. |

The repository includes three small sketches:

- [BlinkForever](examples/BlinkForever/BlinkForever.ino) — uses `repeatingNonBlockingDelay()` for a repeated ON/OFF sequence
- [OnFor500ms](examples/OnFor500ms/OnFor500ms.ino) — uses `singleNonBlockingDelay()` for one non-blocking timed action
- [NestedAwait](examples/NestedAwait/NestedAwait.ino) — uses `childDelayStep()` and `nestedDelaySequence()` to demonstrate parent/child awaiting

## Child await

An `Async` function can wait for another `Async` function:

```cpp
Async childDelayStep() {
    ledOn();
    co_await 200;
    ledOff();
}

Async nestedDelaySequence() {
    co_await childDelayStep();
    co_await 800;
}
```

`nestedDelaySequence()` pauses until `childDelayStep()` completes. The child frame is then returned to TinyAwait's fixed pool before the parent continues.

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

That is a design choice: fixed memory avoids runtime allocation and fragmentation and keeps the maximum RAM cost known in advance.

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

### Arduino-compatible environment

When `ARDUINO` is defined, TinyAwait uses `millis()` automatically:

```cpp
#include <TinyAwait.h>
```

### ESP-IDF

```cpp
#include "esp_timer.h"
#include <cstdint>

#define TINYAWAIT_NOW_MS() \
    static_cast<std::uint32_t>(esp_timer_get_time() / 1000ULL)
#include "TinyAwait.h"
```

Call `tinyawait::poll()` from your normal application loop/task.

### Raspberry Pi Pico SDK

```cpp
#include "pico/time.h"
#include <cstdint>

#define TINYAWAIT_NOW_MS() \
    static_cast<std::uint32_t>(time_us_64() / 1000ULL)
#include "TinyAwait.h"
```

### STM32 HAL

```cpp
#include "stm32xxxx_hal.h"
#define TINYAWAIT_NOW_MS() HAL_GetTick()
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
| Arduino-ESP32 3.3.11 | **Compile-verified in CI** | All supplied Arduino examples compile with the generic ESP32 target. |
| ESP-IDF / ESP32 family | **Expected** | Use a current C++20-capable toolchain and `esp_timer_get_time()`. |
| Raspberry Pi RP2040 / RP2350 | **Expected with suitable toolchain** | Map the Pico SDK monotonic clock. |
| STM32 / ARM Cortex-M | **Expected with suitable toolchain** | Map HAL or another monotonic tick. |
| Embedded RISC-V | **Expected with suitable toolchain** | Provide a monotonic millisecond clock. |
| Classic AVR Uno/Nano/Mega toolchains | **Not advertised as supported** | Typical classic toolchains do not provide the required C++20 coroutine environment. |

`Expected` does not mean hardware-tested.

## Installation

### Arduino / Arduino-ESP32

TinyAwait is a normal source library; no precompiled firmware or architecture-specific binary is required.

Install the project ZIP with **Sketch → Include Library → Add .ZIP Library...**, or copy the `TinyAwait` folder into your Arduino libraries directory.

### PlatformIO

A `library.json` manifest is included for package metadata and discovery. You can use the repository directly as a library dependency. Your project must compile application code as C++20 because `co_await` appears in your own source code as well as in TinyAwait.

### Generic CMake / C++

Copy `src/TinyAwait.h` into your include path and enable C++20:

```cmake
target_compile_features(your_target PRIVATE cxx_std_20)
```

Define `TINYAWAIT_NOW_MS()` before including the header unless the Arduino environment provides `millis()`.

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
- Arduino-ESP32 3.3.11 compilation for all examples

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

- Espressif ESP-IDF C++ support: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/cplusplus.html
- Espressif timer API: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_timer.html
- Arduino library specification: https://docs.arduino.cc/arduino-cli/library-specification
- PlatformIO library manifest: https://docs.platformio.org/en/latest/manifests/library-json/index.html
- Raspberry Pi Pico SDK time APIs: https://www.raspberrypi.com/documentation/pico-sdk/hardware.html

---

Maintained by [@jafarmemar](https://github.com/jafarmemar).