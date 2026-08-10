# TinyAwait

**TinyAwait — tiny, heap-free `co_await` delays for embedded C++.**

[![CI](https://github.com/jafarmemar/TinyAwait/actions/workflows/ci.yml/badge.svg)](https://github.com/jafarmemar/TinyAwait/actions/workflows/ci.yml)

TinyAwait is a small C++20 coroutine helper for microcontrollers and embedded software. It turns a non-blocking delay into one readable line:

```cpp
co_await 500;
```

The value is always milliseconds. There are no `std::chrono` literals, no RTOS task per delay, no dynamic timer container, and no external async framework.

TinyAwait also supports parent/child flow:

```cpp
co_await child();
```

The library is header-only, fixed-memory, cooperative, and deliberately focused on one job: readable non-blocking delays and simple async sequencing.

> **Status:** v1.1.0. Host tests are verified with GCC 14.2 and Clang 17. Embedded support is listed separately as compile-tested, hardware-tested, or expected.

## Why TinyAwait?

Embedded state machines are efficient, but a sequence of timed steps can quickly become hard to read. RTOS tasks are useful when you need them, but they are often more machinery than a short asynchronous sequence requires.

TinyAwait keeps that code close to the way you would write blocking code, while leaving the main loop free:

```cpp
Async turnOnFor500ms() {
    ledOn();
    co_await 500;
    ledOff();
}
```

While this coroutine waits, your main loop can continue handling Wi-Fi, WebSockets, sensors, GPIO, protocols, or other application work.

### Highlights

- **One-line non-blocking delays:** `co_await 500;`
- **Child coroutines:** `co_await child();`
- **Heap-free TinyAwait execution path** in the tested configuration
- **32 simultaneously-live `Async` frames by default**
- **Automatic timer/frame slot reuse** after completion
- **Maximum single delay:** `4,294,967,295 ms` — about **49 days, 17 hours, 2 minutes, 47.295 seconds**
- **Header-only** and dependency-free
- **No RTOS task or OS thread per coroutine**
- **No dynamic timer containers**
- **Wrap-safe 32-bit millisecond timing** when `poll()` runs normally
- Builds with `-fno-exceptions` and `-fno-rtti` on the verified host compilers

## Quick Start

```cpp
#include <TinyAwait.h>

Async blinkForever() {
    while (true) {
        digitalWrite(LED_BUILTIN, HIGH);
        co_await 500;

        digitalWrite(LED_BUILTIN, LOW);
        co_await 500;
    }
}

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    blinkForever();
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

`co_await 500` suspends only `blinkForever()`. It does **not** block `loop()`.

## Examples

The repository includes three small examples:

- [BlinkForever](examples/BlinkForever/BlinkForever.ino) — repeat an ON/OFF sequence without blocking the main loop
- [OnFor500ms](examples/OnFor500ms/OnFor500ms.ino) — turn something on, wait 500 ms, then turn it off
- [NestedAwait](examples/NestedAwait/NestedAwait.ino) — wait for one `Async` function from another with `co_await child()`

## Child await

An `Async` function can wait for another `Async` function without blocking the main loop:

```cpp
Async flashOnce() {
    ledOn();
    co_await 200;
    ledOff();
}

Async flashThenWait() {
    co_await flashOnce();
    co_await 800;
}
```

`flashThenWait()` pauses until `flashOnce()` completes. The child frame is then released back to TinyAwait's fixed pool before the parent continues.

This remains cooperative and single-threaded. It does not create another thread or RTOS task.

## API

The public surface is intentionally small:

```cpp
Async someTask();
tinyawait::poll();
tinyawait::max_delay_ms;
```

Inside an `Async` function:

```cpp
co_await 0;          // immediate; no timer slot
co_await 1;          // 1 ms
co_await 500;        // 500 ms
co_await 1000;       // 1 second
co_await child();    // wait for another Async function
```

Integers passed directly to `co_await` are milliseconds.

## Maximum delay

TinyAwait exposes:

```cpp
tinyawait::max_delay_ms
```

Its value is:

```text
4,294,967,295 ms
= 49 days, 17 hours, 2 minutes, 47.295 seconds
```

So this is valid:

```cpp
co_await tinyawait::max_delay_ms;
```

The timer tracks elapsed time in wrap-safe chunks, so the full `uint32_t` delay range works even when the underlying 32-bit millisecond clock wraps.

`tinyawait::poll()` still needs to run regularly. More than one complete 32-bit clock cycle must not pass between calls to `poll()`.

## Default capacity: 32 tasks

By default:

```cpp
#define TINYAWAIT_MAX_TASKS 32
```

You normally do **not** need to define it yourself.

`TINYAWAIT_MAX_TASKS` is the maximum number of TinyAwait coroutine frames that may be alive at the same time. The timer table has the same number of slots.

You can have far more than 32 `Async` functions in a project and execute thousands of delays over time. The limit only applies to frames that are alive at the same moment.

### Automatic reuse

When a timer fires, its timer slot is cleared before the coroutine resumes. When a coroutine completes, its frame slot returns to the fixed pool. Both slots can be reused immediately.

```text
Task A finishes  -> frame slot becomes free
Timer A fires    -> timer slot becomes free
Task B starts    -> may reuse those slots
```

### Fixed-memory behavior

TinyAwait is heap-free by design, so the configured pool is reserved statically at compile/link time. A completed slot becomes free for another TinyAwait task, but those bytes are not returned to the system heap.

For a smaller MCU, reduce the capacity before including the header:

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

## Frame size

Default:

```cpp
#define TINYAWAIT_FRAME_SIZE 128
```

Each compiler-generated coroutine frame must fit in one slot. Frame size depends on the compiler, target, optimization level, function parameters, and local variables.

If a frame is too large or all frame slots are occupied, TinyAwait does **not** fall back to the heap. The default behavior is fail-fast. A project can provide its own fatal hook:

```cpp
#define TINYAWAIT_ON_ERROR() myFatalHandler()
#include <TinyAwait.h>
```

## Lifetime model

A normal call is fire-and-forget:

```cpp
blinkForever();
```

The function starts immediately. If it suspends, its frame remains in TinyAwait's pool until completion.

When used as a child:

```cpp
co_await child();
```

TinyAwait stores the parent's continuation, resumes the parent when the child finishes, and releases the child's frame. No heap allocation or task registry is required.

Nested `Async` calls use one frame per simultaneously-live coroutine. A parent waiting on one child normally occupies two frame slots while the child is active, but only the child needs a timer slot while it waits on a delay.

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

The core requirement is a monotonic millisecond counter and a toolchain with usable C++20 coroutine support.

## Compatibility

| Target / ecosystem | Status | Notes |
|---|---|---|
| Linux x86_64, GCC 14.2 | **Host-simulated / verified** | All automated tests passed. |
| Linux x86_64, Clang 17 | **Host-simulated / verified** | All automated tests passed. |
| Arduino-ESP32 | **CI compile target** | All supplied Arduino examples are compiled by GitHub Actions. |
| ESP-IDF / ESP32 family | **Expected** | Use a current C++20-capable toolchain and `esp_timer_get_time()`. |
| Raspberry Pi RP2040 / RP2350 | **Expected with suitable toolchain** | Map the Pico SDK monotonic clock. |
| STM32 / ARM Cortex-M | **Expected with suitable toolchain** | Map HAL or another monotonic tick. |
| Embedded RISC-V | **Expected with suitable toolchain** | Provide a monotonic millisecond clock. |
| Classic AVR Uno/Nano/Mega toolchains | **Not advertised as supported** | Typical classic toolchains do not provide the required coroutine environment. |

`Expected` means the integration is straightforward from the available toolchain/API, not that hardware testing has been completed.

## Installation and builds

### Arduino / Arduino-ESP32

TinyAwait is a normal source library. No precompiled firmware or architecture-specific binary is required.

Install it with **Sketch → Include Library → Add .ZIP Library...**, or copy the `TinyAwait` folder into your Arduino libraries directory. Arduino compiles the header as part of your sketch for the selected board.

The official Arduino library format supports precompiled `.a`/`.so` files, but they are optional. TinyAwait does not use them because the project is header-only and intended to remain portable across supported toolchains.

### ESP-IDF

TinyAwait does not need a prebuilt ESP32 binary. Add `src/TinyAwait.h` to your component/include path and use the ESP-IDF clock adapter shown above. The library is compiled as part of the firmware build.

### Generic CMake / C++

Copy `src/TinyAwait.h` into your include path and compile with C++20:

```cmake
target_compile_features(your_target PRIVATE cxx_std_20)
```

Provide `TINYAWAIT_NOW_MS()` before including the header unless the Arduino environment supplies `millis()`.

## Memory model

The fixed state is primarily:

```text
frame_pool = TINYAWAIT_MAX_TASKS × aligned TINYAWAIT_FRAME_SIZE
timers     = TINYAWAIT_MAX_TASKS × sizeof(TimerSlot)
frame_used = TINYAWAIT_MAX_TASKS × sizeof(bool)
```

With the default `32 × 128-byte` frame pool:

- frame payload storage: **4096 B**
- frame-use flags: typically **32 B**
- on a typical 32-bit MCU with a 4-byte coroutine handle, a timer slot is expected to be about **12 B**, or roughly **384 B** for 32 slots
- nominal fixed state: about **4512 B**, before target-specific padding/alignment

This is reserved static memory, not heap memory.

On the measured 64-bit host, `TimerSlot` is 16 B and `FrameSlot` is 128 B.

## Heap behavior

TinyAwait overrides coroutine frame allocation and serves frames from the fixed pool.

The host no-heap test performs 10,000 parent/child create/suspend/resume/complete sequences while global `operator new` is instrumented.

Measured result for the TinyAwait path:

```text
0 additional global heap allocations
```

Application code inside a coroutine can still allocate memory; TinyAwait only controls its own coroutine/timer path.

## Performance

These measurements come from the supplied x86_64 GCC 14.2 build environment. They are useful for regression tracking, not as MCU timing claims.

Median of five runs, 5,000,000 `poll()` calls per occupancy measurement:

| Case | Median |
|---|---:|
| `poll()` — 0 active timers, capacity 32 | **20.466 ns/call** |
| `poll()` — 1 active timer | **19.369 ns/call** |
| `poll()` — 32 active timers | **27.527 ns/call** |
| Coarse ready-coroutine resume estimate | **37.875 ns/resume** |

Measured GCC 14.2 / x86_64 coroutine frames:

- simple sleeper: **56 B**
- largest frame observed in the nested parent/child sample: **72 B**

Both fit inside the default 128-byte slot in this host build. MCU/compiler frame sizes can differ.

### Host code-size proxy (`-Os`)

GNU `size` on x86_64 ELF:

| Build | `.text` | `.data` | `.bss` |
|---|---:|---:|---:|
| Minimal baseline | 1205 B | 520 B | 8 B |
| TinyAwait nested sample, default 32 | 2532 B | 552 B | 4680 B |
| Increment | **+1327 B** | **+32 B** | **+4672 B** |

The `.bss` increase mostly comes from the intentionally reserved 32-slot fixed pool.

More details: [BENCHMARKS.md](BENCHMARKS.md).

## Core size

The production implementation is a single header. Under the repository's logical-line counting rule, v1.1 is about **104 logical implementation lines**.

The parent/child await support and full-range 32-bit delay handling are included without adding a dynamic scheduler, container, or external dependency.

## Timer implementation

Each timer stores the last sampled 32-bit time and its remaining delay. On each `poll()`, TinyAwait subtracts the wrap-safe elapsed delta from the remaining time.

A timer slot is cleared before its coroutine resumes, so that coroutine can immediately schedule another delay and reuse available capacity.

## Concurrency / ISR rules

TinyAwait is cooperative and single-threaded:

- call `tinyawait::poll()` from one execution context
- start `Async` functions from that same context
- do not call TinyAwait from an ISR
- do not call `poll()` reentrantly from a TinyAwait coroutine
- do not access one TinyAwait instance concurrently from multiple RTOS tasks/threads

There are no locks, atomics, mutexes, or hidden critical sections.

## Limitations

- Requires C++20 coroutine support and `<coroutine>`
- Fixed maximum number of simultaneously-live coroutine frames
- Fixed maximum frame size
- Millisecond resolution
- `Async` is a void task; child await does not return a value
- No cancellation, futures, queues, mutexes, executors, or thread pool
- Single-threaded and not ISR-safe
- Complex coroutine locals can increase compiler-generated frame size

These tradeoffs keep the implementation small and predictable.

## Tests

The host suite covers:

- immediate coroutine start
- `co_await 0`, `1`, and `500`
- sequential awaits
- real `co_await child()` behavior
- immediate child completion
- parent continuation and child frame destruction
- 32 default simultaneous tasks
- deterministic capacity exhaustion
- timer/frame reuse after completion
- maximum `uint32_t` delay
- clock wraparound
- no premature resume
- oversized-frame failure
- no TinyAwait heap allocation in 10,000 nested sequences
- repeated 32-way lifecycle stress
- no residual frames/timers after each stress round
- GCC and Clang
- `-fno-exceptions`
- `-fno-rtti`
- AddressSanitizer
- UndefinedBehaviorSanitizer

Build locally:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## CI

GitHub Actions builds/tests the host suite with GCC and Clang, runs ASan/UBSan, builds the size-optimized sample, and compiles all three Arduino examples for Arduino-ESP32.

Use the CI badge at the top of this README, or open the repository's **Actions** tab, to see the current result for the latest commit.

## Project name

`TinyAwait` still describes the project well: a small embedded helper centered on `co_await`. A naming check performed before v1.1 did not find an obvious exact-name conflict in the embedded C++ library space.

## Performance claims

The README sticks to measured behavior and explicit limitations. Timing and memory numbers are labeled with the platform they came from, and embedded targets are not marked as verified until they have actually been built or tested there.

## Contributing

Keep TinyAwait focused. Features that add code, RAM, or runtime cost should come with a clear reason and tests or measurements where appropriate.

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT. See [LICENSE](LICENSE).

## References

Compatibility and integration notes were checked against current primary/official material where possible:

- Espressif ESP-IDF C++ support: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/cplusplus.html
- Espressif timer API: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_timer.html
- Arduino library specification: https://docs.arduino.cc/arduino-cli/library-specification
- Raspberry Pi Pico SDK time APIs: https://www.raspberrypi.com/documentation/pico-sdk/hardware.html
- GCC C++ status: https://gcc.gnu.org/projects/cxx-status.html
