# TinyAwait

**TinyAwait — tiny, heap-free `co_await` delays for embedded C++.**

TinyAwait is a focused C++20 coroutine helper for microcontrollers and embedded software. It turns a non-blocking delay into one readable line:

```cpp
co_await 500;
```

The value is always milliseconds. No `std::chrono` literal, no RTOS task per delay, no dynamic timer container, and no external async framework.

TinyAwait also supports simple parent/child flow:

```cpp
co_await child();
```

The core stays header-only, fixed-memory, cooperative, and intentionally small.

> **Status:** v1.1 candidate. Host tests are verified with GCC 14.2 and Clang 17. Embedded targets are described conservatively unless actually compile-tested or hardware-tested.

## Why TinyAwait?

Embedded state machines are efficient but can become hard to read. RTOS tasks are useful but can be excessive for short asynchronous sequences. TinyAwait provides a small middle ground: stackless C++20 coroutines with predictable fixed memory and a tiny `poll()` scheduler.

```cpp
Async turnOnFor500ms() {
    ledOn();
    co_await 500;
    ledOff();
}
```

While that coroutine is waiting, your main loop can continue handling Wi-Fi, WebSocket traffic, sensors, GPIO, protocols, or other application work.

### Highlights

- **One-line non-blocking delays:** `co_await 500;`
- **Real child awaiting:** `co_await child();`
- **Heap-free TinyAwait execution path** in the tested configuration.
- **32 simultaneously-live `Async` frames by default.**
- **Automatic slot reuse** as timers and coroutines finish.
- **Maximum single delay:** `4,294,967,295 ms` — about **49 days, 17 hours, 2 minutes, 47.295 seconds**.
- **Header-only** and dependency-free.
- **No RTOS task or OS thread per coroutine.**
- **No dynamic timer containers.**
- **Wrap-safe 32-bit millisecond timing** when `poll()` continues to run normally.
- Builds with `-fno-exceptions` and `-fno-rtti` on the verified host compilers.

TinyAwait intentionally does one small job well: make embedded delays and simple async sequencing readable without turning the project into an async framework.

## 30-second quick start

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

## Simple child await

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

This is still cooperative and single-threaded. It does not create a new thread or RTOS task.

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

The timer internally consumes elapsed time in wrap-safe chunks. This allows the full `uint32_t` delay range while the underlying 32-bit millisecond clock wraps.

As with any 32-bit sampled clock, `tinyawait::poll()` must continue to run often enough that more than one complete 32-bit clock cycle does not pass between polls. In normal embedded event loops, `poll()` is called vastly more often than once every ~49.7 days.

## Default capacity: 32 tasks

By default:

```cpp
#define TINYAWAIT_MAX_TASKS 32
```

You normally do **not** need to define it yourself.

`TINYAWAIT_MAX_TASKS` is the maximum number of TinyAwait coroutine frames that may be alive at the same time. The timer table also has the same number of slots.

For example, 100 different functions may exist in your firmware and they may execute thousands of delays over time. The limit concerns only simultaneously-live TinyAwait coroutine frames.

### Automatic reuse

When a timer finishes, its timer slot is cleared **before** the coroutine resumes. When a coroutine completes, its frame slot is returned to the fixed pool. Both can immediately be reused by another task.

```text
Task A finishes  -> frame becomes free
Timer A fires    -> timer slot becomes free
Task B starts    -> may reuse those slots
```

Unused slots are not considered active and finished slots are reused automatically.

### Important fixed-memory detail

TinyAwait is deliberately heap-free, so the configured pool is **static memory reserved at compile/link time**. A free slot becomes reusable by TinyAwait, but its reserved bytes are not returned to the system heap. Returning memory dynamically would defeat the fixed-memory/no-fragmentation design.

If a smaller MCU needs less reserved RAM, reduce the capacity before including the header:

```cpp
#define TINYAWAIT_MAX_TASKS 8
#include <TinyAwait.h>
```

If you need more:

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

Each compiler-generated coroutine frame must fit within one slot. Simple tested coroutines are comfortably below this value on the measured x86_64 GCC build, but frame size depends on compiler, target, optimization, parameters, and local variables.

If a frame is too large or all frame slots are occupied, TinyAwait **does not fall back to the heap**. The default behavior is fail-fast. You can provide a project-specific fatal hook:

```cpp
#define TINYAWAIT_ON_ERROR() myFatalHandler()
#include <TinyAwait.h>
```

## Lifetime model

A normal call remains simple fire-and-forget:

```cpp
blinkForever();
```

The function starts immediately. If it suspends, its frame remains in TinyAwait's pool until completion.

When used as a child:

```cpp
co_await child();
```

TinyAwait stores the parent's continuation, resumes the parent when the child finishes, and then releases the child's frame. No heap allocation or task object registry is required.

Nested `Async` calls consume one frame per simultaneously-live coroutine. For example, a parent waiting on one child usually occupies two frame slots while the child is active, but only the child needs a timer slot while it is waiting on a delay.

## Clock integration

### Arduino-compatible environment

When `ARDUINO` is defined, TinyAwait maps directly to `millis()`:

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

The important requirement is a monotonic millisecond counter and a compiler/toolchain with usable C++20 coroutine support.

## Compatibility

| Target / ecosystem | Status in this package | Notes |
|---|---|---|
| Linux x86_64, GCC 14.2 | **Host-simulated / verified** | All automated tests passed. |
| Linux x86_64, Clang 17 | **Host-simulated / verified** | All automated tests passed. |
| ESP-IDF / ESP32 family | **Expected; not locally compile-tested** | Use a current C++20-capable toolchain and `esp_timer_get_time()`. |
| Arduino-ESP32 | **Expected; public CI configured** | Arduino clock maps to `millis()`. |
| Raspberry Pi RP2040 / RP2350 | **Expected with suitable C++20 toolchain** | Map Pico SDK monotonic time. |
| STM32 / ARM Cortex-M | **Expected with suitable C++20 toolchain** | Map HAL or another monotonic tick. |
| Embedded RISC-V | **Expected with suitable C++20 toolchain** | Provide a monotonic clock. |
| Classic AVR Uno/Nano/Mega toolchains | **Not advertised as supported** | Typical classic toolchains do not provide the required modern coroutine environment. |

"Expected" does not mean hardware-tested. TinyAwait does not fabricate compatibility results.

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
- nominal fixed state is therefore about **4512 B**, before target-specific padding/alignment

This is reserved static memory, not heap memory.

On the measured 64-bit host, `TimerSlot` is 16 B and `FrameSlot` is 128 B.

## Heap behavior

TinyAwait overrides coroutine frame allocation and serves frames from the fixed pool.

The host no-heap test performs 10,000 parent/child create/suspend/resume/complete sequences while global `operator new` is instrumented.

Measured TinyAwait-path result:

```text
0 additional global heap allocations
```

Your own code inside a coroutine can still allocate memory; TinyAwait cannot prevent allocations performed by application code or other libraries.

## Performance measurements

Measured on the supplied x86_64 build environment with GCC 14.2. These are host regression measurements, **not MCU timing claims**.

Median of five runs, 5,000,000 `poll()` calls per occupancy measurement:

| Case | Median |
|---|---:|
| `poll()` — 0 active timers, capacity 32 | **20.466 ns/call** |
| `poll()` — 1 active timer | **19.369 ns/call** |
| `poll()` — 32 active timers | **27.527 ns/call** |
| Coarse ready-coroutine resume estimate | **37.875 ns/resume** |

Measured GCC 14.2 / x86_64 coroutine frames in the benchmark build:

- simple sleeper: **56 B**
- largest frame observed after adding the nested parent/child example: **72 B**

Both fit inside the default 128-byte slot in this host build. Real MCU/compiler frame sizes can differ.

### Host code-size proxy (`-Os`)

GNU `size` on x86_64 ELF:

| Build | `.text` | `.data` | `.bss` |
|---|---:|---:|---:|
| Minimal baseline | 1205 B | 520 B | 8 B |
| TinyAwait nested sample, default 32 | 2532 B | 552 B | 4680 B |
| Increment | **+1327 B** | **+32 B** | **+4672 B** |

The `.bss` increase mostly reflects the intentionally reserved 32-slot fixed pool. These values are for regression tracking; target firmware map files are the real source for MCU Flash/RAM measurements.

See [BENCHMARKS.md](BENCHMARKS.md).

## Core size

The production implementation remains a single header. Using a simple logical-line count that excludes blank lines, comments, preprocessor-only lines, test instrumentation, namespace-only lines, and brace-only lines, the v1.1 core is about **104 logical implementation lines**.

The small increase versus the first candidate buys real parent/child `co_await` lifetime handling and robust full-range 32-bit delays without introducing a scheduler framework, dynamic container, or external dependency.

## Timer implementation

Each timer stores the last sampled 32-bit time and its remaining delay. On every `poll()`, TinyAwait subtracts the wrap-safe elapsed delta from the remaining time. This avoids the narrow-deadline problem that a single start/deadline comparison can have near the full 32-bit delay limit.

A timer slot is cleared before its coroutine is resumed, so the resumed coroutine can immediately schedule another delay and reuse available capacity.

## Concurrency / ISR rules

TinyAwait is intentionally cooperative and single-threaded:

- call `tinyawait::poll()` from one execution context
- start `Async` functions from that same context
- do not call TinyAwait from an ISR
- do not call `poll()` reentrantly from a TinyAwait coroutine
- do not access one TinyAwait instance concurrently from multiple RTOS tasks/threads

No locks, atomics, mutexes, or hidden critical sections are added.

## Limitations

- Requires C++20 coroutine support and `<coroutine>`.
- Fixed maximum number of simultaneously-live coroutine frames.
- Fixed maximum frame size.
- Millisecond resolution.
- `Async` is a void task; child await does not return a value.
- No cancellation, futures, queues, mutexes, executors, or thread pool.
- Single-threaded and not ISR-safe.
- Complex coroutine locals can increase compiler-generated frame size.

Those limits are intentional: they keep TinyAwait small and predictable.

## Installation

### Arduino ZIP

Use **Sketch → Include Library → Add .ZIP Library...** and select the release ZIP, or copy the `TinyAwait` directory into your Arduino libraries directory.

### Generic CMake / C++

Copy `src/TinyAwait.h` into your include path and compile with C++20:

```cmake
target_compile_features(your_target PRIVATE cxx_std_20)
```

Provide `TINYAWAIT_NOW_MS()` before including the header unless the Arduino environment supplies `millis()`.

## Examples

- `examples/BlinkForever`
- `examples/OnFor500ms`
- `examples/NestedAwait`

## Tests

The automated host suite verifies:

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

GitHub Actions is configured to:

- build/test with GCC and Clang
- run ASan + UBSan host tests
- build a size-optimized sample
- compile all three Arduino examples with Arduino-ESP32

## Name

`TinyAwait` still fits the project well: it directly describes the two defining ideas — a tiny implementation and readable `co_await`-based embedded flow. A web/GitHub search on 2026-08-10 did not surface an obvious serious exact-name conflict in the embedded C++ library space. This is not a trademark guarantee, so the name should be rechecked before formal registry/trademark use.

## About performance claims

TinyAwait deliberately avoids claims such as "zero overhead", "fastest", "works everywhere", or "the first complete await library". Embedded coroutine work and other coroutine libraries already exist, and performance depends on target hardware and compiler.

The project instead publishes things it can actually defend: measured host overhead, fixed memory, tested heap behavior, explicit limitations, and a very small API.

## Contributing

Keep TinyAwait focused. New features should justify their code size, RAM, and runtime cost with tests or measurements.

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT. See [LICENSE](LICENSE).

## Research references

Compatibility and design notes were checked against current primary/official material where possible:

- Espressif ESP-IDF C++ support: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/cplusplus.html
- Espressif timer API: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/esp_timer.html
- Arduino library specification: https://docs.arduino.cc/arduino-cli/library-specification
- Raspberry Pi Pico SDK time APIs: https://www.raspberrypi.com/documentation/pico-sdk/hardware.html
- GCC C++ status: https://gcc.gnu.org/projects/cxx-status.html
