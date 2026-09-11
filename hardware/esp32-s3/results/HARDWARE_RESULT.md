# TinyAwait ESP32-S3 Hardware Validation

## Verdict

**PASS — TinyAwait 1.1.2 has been successfully validated on a physical ESP32-S3 over USB.**

The result is based on live USB-to-UART evidence from `COM5`, not on a simulated timer or a host-only test. The existing firmware was observed and tested without re-flashing it.

## Environment

- Date: 2026-09-11
- Tester: Codex hardware-validation session
- Host OS: Microsoft Windows 11 Enterprise 64-bit, build 10.0.26200
- PlatformIO Core: 6.1.19
- Board target: `esp32-s3-devkitc-1`
- Runtime chip: ESP32-S3 revision 2
- CPU: 240 MHz
- Flash: 8,388,608 bytes (8 MB)
- PSRAM: 0 bytes / not present
- USB/Serial: USB-to-UART, USB-Enhanced-SERIAL CH343
- Serial port: `COM5`
- USB VID/PID: `1A86:55D3`
- USB serial: `5B3D078818`
- USB location: `1-2`
- Baud: 115200
- Arduino-ESP32: 3.3.11
- ESP-IDF: v5.5.5
- C++ `__cplusplus`: 202002
- TinyAwait: 1.1.2, commit `868304d548074a07274665ebe4d3fb26854c4f14`
- Board/module marking: not read from the physical silkscreen; runtime identity and PlatformIO target are recorded above.

## Build

- Firmware already flashed before validation: **YES**
- Firmware re-flashed during validation: **NO**
- `pio project config`: **PASS**
- `pio run` build: **PASS**
- Platform: `Espressif 32 (55.3.311)`
- Build profile: release
- Build result: 289,885 bytes Flash and 26,232 bytes RAM used
- Build warning: PlatformIO reported Windows Long Path Support disabled; the build still completed successfully.

## Hardware and boot

- ESP32-S3 detected on physical USB serial: **PASS**
- Boot ROM output received: **PASS**
- Firmware banner received: **PASS**
- Serial communication at 115200: **PASS**
- Boot output showed `rst:0x1 (POWERON)` and normal `SPI_FAST_FLASH_BOOT`: **PASS**
- Guru Meditation, panic, illegal instruction, watchdog, brownout, and reboot loop: **NOT OBSERVED**

## TinyAwait tests

All three complete suite executions reported 7 passed, 0 failed, 7 total.

| Test | Run 1 | Run 2 | Run 3 | Observed evidence |
|---|---|---|---|---|
| Immediate `co_await 0` | PASS | PASS | PASS | actual `0 ms`; expected `0..2 ms` |
| Single non-blocking delay | PASS | PASS | PASS | actual `250 ms`; expected `250..500 ms` |
| Sequential delays | PASS | PASS | PASS | checkpoints `80, 200, 360 ms` |
| Nested coroutine | PASS | PASS | PASS | child finished at `150 ms`, stage `2`; total `270 ms` |
| Detached concurrent tasks | PASS | PASS | PASS | order `1,2,3`; completion `100,200,300 ms` |
| Repeating coroutine | PASS | PASS | PASS | `5` repetitions in `300 ms` |
| Scheduler cleanup | PASS | PASS | PASS | `active_frames=0`, `active_timers=0` |

Timing values were identical across the three runs within the firmware's stated tolerances:

- Single delay: `250 ms`, `250 ms`, `250 ms`
- Sequential total: `360 ms`, `360 ms`, `360 ms`
- Nested total: `270 ms`, `270 ms`, `270 ms`
- Concurrent completion order: `1 -> 2 -> 3` on every run
- Repeating total: `300 ms`, `300 ms`, `300 ms`

## Scheduler and non-blocking behavior

- `tinyawait::poll()` integration: **PASS**; the firmware calls it once from the ordinary Arduino `loop()`.
- Non-blocking loop behavior: **PASS**.
- During each single-delay test the loop advanced by approximately `36,491..36,546` iterations while the coroutine was suspended.
- The independent background coroutine advanced by `10` ticks during each single-delay test.
- Live `ping` while the suite was `RUNNING`: **PASS** on all three runs.
- Command interface `help`, `info`, `status`, `ping`, and `run`: **PASS**.

## Repeated runs

- Run 1: automatic suite after boot — **PASS**
- Run 2: manual `run` without reboot — **PASS**
- Run 3: manual `run` without reboot — **PASS**
- State after each run: `suite=IDLE`, `frames=0`, `timers=0`

## Memory and allocator metrics

- Frame pool: `4096 bytes`
- Maximum observed coroutine frame: `84 bytes`
- Maximum allocator free-list search: `1`
- Final active frames: `0` on all three runs
- Final active timers: `0` on all three runs
- Free heap at boot: `354480 bytes`
- Free heap after suite: not reported by the existing firmware; no source change was made to add this metric.

The observed zero final frame/timer counts provide direct lifecycle evidence for the finite tasks exercised by this harness. They do not replace the library's separate host stress, fragmentation, wraparound, sanitizer, and no-heap test suites.

## Stability observation

- Observation after the third run: approximately 15 seconds of live Serial observation
- Unexpected resets: **NO**
- Watchdog: **NOT OBSERVED**
- Guru Meditation: **NOT OBSERVED**
- Panic/exception output: **NOT OBSERVED**
- Serial responsiveness: **PASS**
- Additional idle `ping` after the runs: **PASS**

## Source/API review

The compiled local dependency resolved to TinyAwait commit `868304d548074a07274665ebe4d3fb26854c4f14` and SHA-256 `B8B415FE14731C85D2D1BBF2F9EA90C17F81BB5D7BD44BEFE6B340DD40670FC4`. The firmware uses the real public integration points:

- `Async`
- integer `co_await` delays, including `0`
- `co_await childCoroutine()` parent/child sequencing
- detached `Async` calls without `co_await`
- `tinyawait::poll()` from `loop()`
- `tinyawait::frame_pool_bytes`
- testing metrics `active_frames()`, `active_timers()`, `max_frame_size()`, and `frame_allocator_max_search()`

The checked reference commit documents the same C++20 Arduino model, fixed frame arena, no heap fallback, detached/child coroutines, and `poll()` service-loop integration. The hardware result verifies those selected paths on this ESP32-S3; it does not claim that every host-only library stress case was rerun on the MCU.

## Final result

```text
TINYAWAIT_ESP32S3_HARDWARE_VALIDATION = PASS
```

Raw unfiltered Serial capture: `serial-log.txt`
