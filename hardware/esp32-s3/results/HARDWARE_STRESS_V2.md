# TinyAwait ESP32-S3 Hardware Stress Validation V2

## Final Verdict

```text
TINYAWAIT_ESP32S3_STRESS_VALIDATION = PASS
```

TinyAwait 1.1.2 passed extended capacity, invalid-delay, wraparound, variable-frame, allocator, scheduler, heap-stability, and sustained-load validation on a physical ESP32-S3.

This result is based on live USB-to-UART output from the board, including a complete 180-second soak. The TinyAwait library source was not modified.

## Environment

- Date: 2026-09-11
- Host OS: Microsoft Windows 11 Enterprise 64-bit, build 10.0.26200
- PlatformIO Core: 6.1.19
- Port: `COM5`
- USB device: USB-Enhanced-SERIAL CH343
- VID/PID: `1A86:55D3`
- USB serial: `5B3D078818`
- USB location: `1-2`
- Board target: `esp32-s3-devkitc-1`
- Platform: Espressif 32 `55.3.311`
- Runtime chip: ESP32-S3 revision 2
- CPU: 240 MHz
- Flash detected by esptool and firmware: 8,388,608 bytes (8 MB)
- Firmware-reported PSRAM: 0 bytes; PlatformIO target is the no-PSRAM N8 variant.
- ESP-IDF: v5.5.5
- Arduino-ESP32: 3.3.11
- C++ `__cplusplus`: 202002
- Serial baud: 115200
- TinyAwait: 1.1.2, commit `868304d548074a07274665ebe4d3fb26854c4f14`
- TinyAwait frame pool: 4096 bytes
- TinyAwait maximum live tasks: 32

The esptool connection banner also describes the ESP32-S3 silicon as PSRAM-capable; the runtime API reported no usable PSRAM on this configured board target, so the firmware result above is the value used for validation.

## Firmware and build

- V1 source and default environment retained.
- V2 environment added as `esp32s3-stress-v2`.
- V2 source: `src_v2/main.cpp` in the original validation workspace; the repository copy lives under `hardware/esp32-s3/v2/src/main.cpp`.
- V2 library dependency pinned to the exact requested commit.
- Build: **PASS**.
- V2 build size: 297,293 bytes Flash and 26,824 bytes RAM.
- Upload target: **COM5**.
- Physical V2 firmware confirmed after upload by the new V2 boot banner and all subsequent runtime output.

PlatformIO emitted a Windows console encoding exception while echoing an upload progress line, but the command completed with success and the board subsequently booted the uploaded V2 image. This was a host-output issue, not an MCU or test failure.

## Test results

| # | Test | Result | Evidence |
|---:|---|---|---|
| 1 | Capacity boundary 32/33 | PASS | 32 frames and 32 timers accepted; task 33 produced exactly one expected error; final `0/0` |
| 2 | Invalid delay validation | PASS | negative and oversized delays produced exactly 2 expected errors; final `0/0` |
| 3 | `uint32_t` wrap + maximum delay | PASS | no early fire; fired after the full `UINT32_MAX` elapsed range; clean |
| 4 | Mixed real coroutine frames | PASS | 600 batches / 5,400 completions; deterministic checksum `4582832/4582832`; largest coroutine frame observed at this point `280` bytes |
| 5 | Direct allocator fuzz | PASS | 250,000 random alloc/free operations; pool fully recovered; search maximum `18`; final active `0/0` |
| 6 | Scheduler stress | PASS | 500,000 main operations plus 100,000 repeat operations; `600000/600000` completed |
| 7 | Heap stability | PASS | free heap `353888 -> 353888` (`delta=0`); largest block `294900 -> 294900` (`delta=0`) |
| 8 | Continuous soak | PASS | 180,000 ms; `87239` started and `87239` completed; final active `0/0` |
| 9 | Final scheduler cleanup | PASS | `active_frames=0`, `active_timers=0`, expected errors `3`, unexpected errors `0` |

## Capacity and failure paths

- Tasks 1 through 32 were accepted.
- Task 33 was rejected through the custom test-only error hook.
- Invalid signed negative delay and oversized 64-bit delay each used the expected error path.
- Total expected TinyAwait errors: `3`.
- Actual errors at final cleanup: `3`.
- Unexpected errors: `0`.
- No trap, panic, or reset occurred on any expected failure path.

## Wraparound and maximum delay

The harness replaced the clock only for this test with a deterministic `uint32_t` fake clock. It started at `UINT32_MAX - 500`, advanced across wraparound, verified the maximum-delay coroutine remained pending at each intermediate point, then verified completion exactly after the full `UINT32_MAX` elapsed range. The real Arduino `millis()` clock was restored immediately afterward.

## Allocator and frame stress

- Mixed frame workload: sizes 16, 32, 48, 64, 96, 128, 160, 192, and 224 bytes across 600 batches.
- Mixed-frame completions: `5400`.
- Mixed-frame checksum: deterministic and matched expected `4582832`.
- Direct allocator fuzz: `250000` operations using static storage only.
- Live allocator blocks were checked for pool bounds, alignment, overlap, canary integrity, active-frame accounting, and timer count.
- Whole-pool recovery allocation succeeded after the fuzz run.
- Allocator free-list search maximum: `18`.
- Final active frames/timers after allocator fuzz: `0/0`.

The final TinyAwait `max_frame_size=4096` includes the deliberate whole-pool allocator recovery request. The largest actual mixed coroutine frame before that direct allocator probe was `280` bytes.

## Scheduler stress

- Main scheduler load: `500000` real detached coroutine/timer completions.
- Repeat load: `100000` additional real detached coroutine/timer completions.
- Total scheduler completions: `600000`.
- Progress checkpoints reached 50,000 through 500,000 for the main pass and 600,000 total after the repeat pass.
- After every round, active frames and timers were required to return to zero.
- Maximum concurrent frame count observed: `32`, reached during the capacity boundary.
- `tinyawait::poll()` was serviced from the normal runtime loop, with `yield()` and the non-blocking Serial parser active in every wait loop.

## Heap stability

| Metric | Before stress | After scheduler | Final after soak |
|---|---:|---:|---:|
| Free heap | 353888 | 353888 | 353888 |
| Largest free block | 294900 | 294900 | 294900 |
| Historical minimum free heap | 348576 | 348544 | 348536 |
| 8-bit free heap | 353888 | 353888 | 353888 |
| 8-bit largest block | 294900 | 294900 | 294900 |

The minimum-free-heap value is historical and is not expected to return to its baseline. The leak comparison uses current free heap and largest block after the idle drain, both of which had zero delta.

## 180-second soak

The soak continuously generated mixed-size frames and short delays, including occasional nested parent/child coroutines, while keeping the live workload below the capacity boundary. Heartbeats were emitted every 10 seconds with elapsed time, started/completed counts, frames, timers, free heap, frame metric, allocator search, and error count.

- Duration reached: `180000 ms`.
- Soak tasks started: `87239`.
- Soak tasks completed: `87239`.
- Soak checksum: `993117862`.
- Error count remained `3` throughout.
- Observed live soak frames were typically `16..17`; timers `13..15`.
- Final drain returned frames and timers to zero.

## Serial responsiveness and stability

- `help`: PASS.
- `info`: PASS.
- `status`: PASS before, during, and after stress.
- `ping`: PASS repeatedly during mixed frames, scheduler stress, heap stability, and soak.
- `run`: V2 suite started automatically and completed normally.
- Unexpected reset: **NO**.
- Watchdog/WDT: **NOT OBSERVED**.
- Guru Meditation: **NOT OBSERVED**.
- Panic/abort/exception output from the MCU: **NOT OBSERVED**.
- Brownout: **NOT OBSERVED**.
- Reboot loop: **NOT OBSERVED**.
- Boot output: normal `SPI_FAST_FLASH_BOOT`; no subsequent reset banner during the capture.

## Runtime totals

- Suite runtime: `281216 ms` (4 minutes 41.216 seconds), below the 10-minute hard limit.
- Scheduler workload completions: `600000`.
- Mixed-frame completions: `5400`.
- Soak completions: `87239`.
- Additional smoke coroutine completions: `35` (capacity, invalid-delay, and maximum-delay checks).
- Counted coroutine completions including smoke paths: `692674`.
- Allocator fuzz operations: `250000`.
- Expected errors: `3`.
- Unexpected errors: `0`.
- Final active frames: `0`.
- Final active timers: `0`.

## Source/API review

The V2 harness uses the pinned TinyAwait public and testing interfaces:

```cpp
Async
co_await delay
co_await child()
tinyawait::poll()
tinyawait::frame_pool_bytes
tinyawait::active_frames()
tinyawait::active_timers()
tinyawait::max_frame_size()
tinyawait::frame_allocator_max_search()
```

The exact reference commit documents the same C++20 Arduino integration, fixed frame arena, no-heap fallback model, child/detached coroutines, and `poll()` service-loop model: <https://github.com/jafarmemar/TinyAwait/tree/868304d548074a07274665ebe4d3fb26854c4f14>.

## Artifacts

- Complete Serial capture: `v2-serial-log.txt`
- Repository harness: `../v2/src/main.cpp`
- Reproduction environment: `../v2/platformio.ini`
