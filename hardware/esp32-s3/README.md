# ESP32-S3 hardware validation

TinyAwait 1.1.2 is **hardware validated** on a physical ESP32-S3 using Arduino-ESP32 3.3.11, ESP-IDF 5.5.5, C++20, and a USB-to-UART connection.

The validation was performed against TinyAwait commit `868304d548074a07274665ebe4d3fb26854c4f14` on an ESP32-S3 revision 2 at 240 MHz with 8 MB flash and no active PSRAM. The board target was `esp32-s3-devkitc-1` under PlatformIO Core 6.1.19.

## What is included

- `v1/` — small usage-oriented hardware validation covering normal API behavior, nested and detached coroutines, concurrent delays, repeated waits, non-blocking `loop()`, Serial responsiveness, and cleanup.
- `v2/` — extended stress harness covering capacity/error paths, `uint32_t` wraparound, maximum delay, variable-size coroutine frames, allocator fragmentation/recovery, scheduler load, heap stability, and a sustained soak.
- `results/HARDWARE_RESULT.md` — reviewed V1 hardware report.
- `results/HARDWARE_STRESS_V2.md` — reviewed V2 stress report.
- `results/serial-log.txt` and `results/v2-serial-log.txt` — raw Serial evidence captured from the physical board.

The harnesses are validation firmware, not production examples. `TINYAWAIT_TESTING`, custom clock control, and a returning error hook are used only where the tests need observability or deliberate failure injection. Production applications should normally include `<TinyAwait.h>` directly and use the default fail-fast error behavior or a non-returning application-specific error hook.

## V1: usage and integration validation

V1 ran the complete suite three times on the physical MCU without rebooting between runs. Every run reported 7/7 PASS. It verified:

- `co_await 0`;
- a normal non-blocking delay;
- sequential delays;
- nested `co_await child()` sequencing;
- detached concurrent tasks;
- repeated awaits;
- continued Arduino `loop()` execution while a coroutine was suspended;
- live Serial responsiveness while the suite was running;
- final scheduler cleanup with zero active frames and timers.

## V2: allocator and scheduler stress validation

V2 completed 9/9 PASS in 281,216 ms (4 min 41.216 s). The recorded workload included:

| Check | Hardware result |
|---|---:|
| Capacity boundary | 32 tasks accepted; task 33 rejected through the expected error path |
| Invalid delay paths | 2/2 expected errors, no unexpected errors |
| `uint32_t` wrap + `max_delay_ms` | PASS; no early completion |
| Mixed real coroutine frames | 5,400 completions; deterministic checksum |
| Direct allocator fuzz | 250,000 operations; whole pool recovered |
| Scheduler stress | 600,000 / 600,000 completions |
| Heap stability | `353888 -> 353888` bytes; delta 0 |
| Continuous soak | 180 s; 87,239 / 87,239 tasks completed |
| Final cleanup | `active_frames=0`, `active_timers=0` |

No unexpected reset, watchdog, Guru Meditation, panic, abort, brownout, or reboot loop was observed. Serial remained responsive during mixed-frame, scheduler, heap, and soak phases.

The V2 source in this repository contains one documentation-only cleanup compared with the captured run: the repeat scheduler progress denominator is displayed cumulatively (`600000`) instead of producing cosmetic lines such as `550000 / 100000`. The scheduler logic, workload, pass/fail criteria, and preserved raw validation log are unchanged.

## Reproduce

Connect a compatible ESP32-S3 over USB, then run either project from its own directory:

```bash
cd hardware/esp32-s3/v1
pio run -t upload
pio device monitor -b 115200
```

or:

```bash
cd hardware/esp32-s3/v2
pio run -t upload
pio device monitor -b 115200
```

Both PlatformIO projects pin TinyAwait to the exact revision used for the recorded validation. If validating a newer TinyAwait commit, intentionally change the dependency revision and record the new commit in the resulting report rather than treating the existing evidence as validation of untested code.

## Scope of the claim

**Hardware validated** means the recorded TinyAwait 1.1.2 revision was exercised successfully on this physical ESP32-S3/toolchain combination. It does not imply that every ESP32 variant, Arduino core, compiler, board package, or third-party C++20 microcontroller has been tested. The host CI suite remains the broader deterministic regression gate, including sanitizer, randomized allocator, wraparound, no-heap, and multi-translation-unit tests.
