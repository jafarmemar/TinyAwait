#include <Arduino.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

#include "esp_heap_caps.h"

// These hooks belong only to this validation firmware. TinyAwait itself is not
// modified; the production/default V1 environment remains unchanged.
static uint32_t g_tinyawaitErrors = 0;
static bool g_useFakeClock = false;
static uint32_t g_fakeNow = 0;

static uint32_t tinyawaitTestClock() {
  return g_useFakeClock ? g_fakeNow : static_cast<uint32_t>(millis());
}

static void tinyawaitTestError() noexcept {
  ++g_tinyawaitErrors;
}

#define TINYAWAIT_TESTING 1
#define TINYAWAIT_NOW_MS() tinyawaitTestClock()
#define TINYAWAIT_ON_ERROR() tinyawaitTestError()
#include <TinyAwait.h>

namespace {

constexpr unsigned long SERIAL_BAUD = 115200;
constexpr uint32_t AUTO_RUN_DELAY_MS = 2000;
constexpr uint32_t CAPACITY_HOLD_MS = 250;
constexpr uint32_t MIXED_BATCHES = 600;
constexpr uint32_t FUZZ_OPERATIONS = 250000;
constexpr uint32_t SCHEDULER_OPERATIONS = 500000;
constexpr uint32_t REPEAT_OPERATIONS = 100000;
constexpr uint32_t SOAK_DURATION_MS = 180000;
constexpr uint32_t SOAK_HEARTBEAT_MS = 10000;
constexpr uint32_t MAX_STRESS_RUNTIME_MS = 9UL * 60UL * 1000UL;

bool suiteRunning = false;
bool autoRunPending = true;
uint32_t autoRunAtMs = 0;
uint32_t suiteStartedAtMs = 0;
const char* currentPhase = "IDLE";

uint32_t passedTests = 0;
uint32_t failedTests = 0;
uint32_t testNumber = 0;
uint32_t expectedErrorCount = 0;

uint32_t maximumConcurrentFrames = 0;
uint32_t mixedDone = 0;
uint32_t mixedChecksum = 0;
uint32_t soakStarted = 0;
uint32_t soakCompleted = 0;
uint32_t soakChecksum = 0;
uint32_t schedulerCompleted = 0;
uint32_t schedulerStarted = 0;

uint32_t heapBefore = 0;
uint32_t heapAfter = 0;
uint32_t heapMinBefore = 0;
uint32_t heapMinAfter = 0;
uint32_t largestBefore = 0;
uint32_t largestAfter = 0;
uint32_t capsFreeBefore = 0;
uint32_t capsFreeAfter = 0;
uint32_t capsLargestBefore = 0;
uint32_t capsLargestAfter = 0;
uint32_t capsMinBefore = 0;
uint32_t capsMinAfter = 0;

char commandBuffer[48] = {};
size_t commandLength = 0;

bool failureSeen = false;

void updateFrameWatermark() {
  const uint32_t active = static_cast<uint32_t>(tinyawait::active_frames());
  if (active > maximumConcurrentFrames) maximumConcurrentFrames = active;
}

bool elapsedAtMost(uint32_t start, uint32_t limit) {
  return static_cast<uint32_t>(millis() - start) <= limit;
}

void printDivider() {
  Serial.println("================================================================");
}

void recordTest(const char* name, bool pass, const char* detail) {
  ++testNumber;
  if (pass) ++passedTests;
  else {
    ++failedTests;
    failureSeen = true;
  }

  Serial.printf("[V2 TEST %02lu] %-34s %s | %s\n",
                static_cast<unsigned long>(testNumber), name,
                pass ? "PASS" : "FAIL", detail);
}

void recordProgress(const char* label, uint32_t completed, uint32_t total) {
  Serial.printf("[STRESS] %s %lu / %lu | frames=%lu | timers=%lu | errors=%lu\n",
                label,
                static_cast<unsigned long>(completed),
                static_cast<unsigned long>(total),
                static_cast<unsigned long>(tinyawait::active_frames()),
                static_cast<unsigned long>(tinyawait::active_timers()),
                static_cast<unsigned long>(g_tinyawaitErrors));
}

void printHeapSnapshot(const char* label,
                       uint32_t freeHeap,
                       uint32_t minFreeHeap,
                       uint32_t largestAlloc,
                       uint32_t capsFree,
                       uint32_t capsLargest,
                       uint32_t capsMin) {
  Serial.printf(
      "[HEAP] %s free=%lu min=%lu largest=%lu caps_free=%lu "
      "caps_largest=%lu caps_min=%lu\n",
      label,
      static_cast<unsigned long>(freeHeap),
      static_cast<unsigned long>(minFreeHeap),
      static_cast<unsigned long>(largestAlloc),
      static_cast<unsigned long>(capsFree),
      static_cast<unsigned long>(capsLargest),
      static_cast<unsigned long>(capsMin));
}

void printSystemInfo() {
  printDivider();
  Serial.println("TinyAwait ESP32-S3 Hardware Stress Validation V2");
  printDivider();
  Serial.printf("Chip                : %s rev %d\n",
                ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("CPU frequency       : %u MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("Flash size          : %lu bytes\n",
                static_cast<unsigned long>(ESP.getFlashChipSize()));
  Serial.printf("PSRAM size          : %lu bytes\n",
                static_cast<unsigned long>(ESP.getPsramSize()));
  Serial.printf("Free heap at boot   : %lu bytes\n",
                static_cast<unsigned long>(ESP.getFreeHeap()));
  Serial.printf("ESP-IDF             : %s\n", ESP.getSdkVersion());
  Serial.printf("C++ __cplusplus     : %ld\n", static_cast<long>(__cplusplus));
  Serial.println("Arduino-ESP32       : 3.3.11 (PlatformIO pinned)");
  Serial.println("TinyAwait version   : 1.1.2");
  Serial.println("TinyAwait commit    : 868304d548074a07274665ebe4d3fb26854c4f14");
  Serial.printf("TinyAwait frame pool: %lu bytes\n",
                static_cast<unsigned long>(tinyawait::frame_pool_bytes));
  Serial.printf("TinyAwait max tasks : %u\n",
                static_cast<unsigned>(TINYAWAIT_MAX_TASKS));
  Serial.println("Clock mode          : Arduino millis() (fake clock only in wrap test)");
  printDivider();
}

void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  run     - run the complete V2 stress validation");
  Serial.println("  ping    - prove Serial remains responsive during stress");
  Serial.println("  status  - show V2 phase, scheduler, heap/error counters");
  Serial.println("  info    - print board/build information");
  Serial.println("  help    - show this help");
  Serial.println();
}

void printStatus() {
  Serial.printf(
      "[V2 STATUS] suite=%s | phase=%s | uptime=%lu ms | elapsed=%lu ms "
      "| frames=%lu | timers=%lu | max_frames=%lu | errors=%lu/%lu "
      "| scheduler=%lu/%lu | soak=%lu/%lu\n",
      suiteRunning ? "RUNNING" : "IDLE", currentPhase,
      static_cast<unsigned long>(millis()),
      static_cast<unsigned long>(suiteRunning ? millis() - suiteStartedAtMs : 0),
      static_cast<unsigned long>(tinyawait::active_frames()),
      static_cast<unsigned long>(tinyawait::active_timers()),
      static_cast<unsigned long>(maximumConcurrentFrames),
      static_cast<unsigned long>(g_tinyawaitErrors),
      static_cast<unsigned long>(expectedErrorCount),
      static_cast<unsigned long>(schedulerCompleted),
      static_cast<unsigned long>(schedulerStarted),
      static_cast<unsigned long>(soakCompleted),
      static_cast<unsigned long>(soakStarted));
}

void serviceRuntime();

void handleSerial() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') continue;

    if (ch == '\n') {
      commandBuffer[commandLength] = '\0';
      if (strcmp(commandBuffer, "run") == 0) {
        autoRunPending = false;
        if (suiteRunning) Serial.println("[V2 SUITE] Already running.");
        else suiteRunning = true;
      } else if (strcmp(commandBuffer, "ping") == 0) {
        Serial.printf("[PONG] uptime=%lu ms | suite=%s | phase=%s | frames=%lu | timers=%lu\n",
                      static_cast<unsigned long>(millis()),
                      suiteRunning ? "RUNNING" : "IDLE", currentPhase,
                      static_cast<unsigned long>(tinyawait::active_frames()),
                      static_cast<unsigned long>(tinyawait::active_timers()));
      } else if (strcmp(commandBuffer, "status") == 0) {
        printStatus();
      } else if (strcmp(commandBuffer, "info") == 0) {
        printSystemInfo();
      } else if (strcmp(commandBuffer, "help") == 0 ||
                 strcmp(commandBuffer, "?") == 0) {
        printHelp();
      } else if (commandBuffer[0] != '\0') {
        Serial.printf("[COMMAND] Unknown: '%s' (type 'help')\n", commandBuffer);
      }
      commandLength = 0;
      continue;
    }

    if (commandLength + 1 < sizeof(commandBuffer)) {
      commandBuffer[commandLength++] = ch;
    }
  }
}

void serviceRuntime() {
  handleSerial();
  tinyawait::poll();
  updateFrameWatermark();
  yield();
}

// -----------------------------------------------------------------------------
// Real coroutine workloads
// -----------------------------------------------------------------------------

Async holdTask(uint32_t delayMs) {
  co_await delayMs;
}

Async invalidNegativeDelay() {
  co_await -1;
}

Async invalidTooLargeDelay() {
  co_await std::numeric_limits<uint64_t>::max();
}

volatile bool maxDelayFired = false;

Async maximumDelayTask() {
  co_await tinyawait::max_delay_ms;
  maxDelayFired = true;
}

template <size_t N>
Async mixedFrameTask(uint32_t delayMs, uint32_t token) {
  std::array<uint8_t, N> payload{};
  for (size_t i = 0; i < N; i += 16) {
    payload[i] = static_cast<uint8_t>((token + i) & 0xFFU);
  }

  co_await delayMs;

  uint32_t local = 0;
  for (size_t i = 0; i < N; i += 16) local += payload[i];
  mixedChecksum += local;
  ++mixedDone;
}

Async shortTimerTask(uint32_t delayMs) {
  co_await delayMs;
  ++schedulerCompleted;
}

template <size_t N>
Async soakFrameTask(uint32_t delayMs, uint32_t token) {
  std::array<uint8_t, N> payload{};
  for (size_t i = 0; i < N; i += 16) {
    payload[i] = static_cast<uint8_t>((token + i * 3U) & 0xFFU);
  }
  co_await delayMs;
  uint32_t local = 0;
  for (size_t i = 0; i < N; i += 16) local += payload[i];
  soakChecksum += local;
  ++soakCompleted;
}

Async soakChild(uint32_t delayMs) {
  co_await delayMs;
}

Async soakParent(uint32_t first, uint32_t second, uint32_t token) {
  co_await soakChild(first);
  co_await second;
  soakChecksum += token;
  ++soakCompleted;
}

// -----------------------------------------------------------------------------
// V2 tests
// -----------------------------------------------------------------------------

bool testCapacityBoundary() {
  currentPhase = "CAPACITY";
  if (tinyawait::active_frames() != 0 || tinyawait::active_timers() != 0) {
    recordTest("capacity 32/33", false, "not clean at start");
    return false;
  }

  const uint32_t errorsBefore = g_tinyawaitErrors;
  for (uint8_t i = 0; i < TINYAWAIT_MAX_TASKS; ++i) holdTask(CAPACITY_HOLD_MS);
  updateFrameWatermark();

  const bool accepted =
      tinyawait::active_frames() == TINYAWAIT_MAX_TASKS &&
      tinyawait::active_timers() == TINYAWAIT_MAX_TASKS;

  // The 33rd allocation is deliberately expected to invoke the custom hook.
  holdTask(CAPACITY_HOLD_MS);
  const bool rejected = g_tinyawaitErrors == errorsBefore + 1U;
  if (rejected) ++expectedErrorCount;

  const uint32_t drainStart = millis();
  while (static_cast<uint32_t>(millis() - drainStart) < 450U) serviceRuntime();

  const bool clean = tinyawait::active_frames() == 0 &&
                     tinyawait::active_timers() == 0;
  const bool pass = accepted && rejected && clean;
  char detail[160];
  snprintf(detail, sizeof(detail),
           "accepted=%lu frames/%lu timers | rejected=%s | after=%lu/%lu",
           static_cast<unsigned long>(accepted ? TINYAWAIT_MAX_TASKS : 0),
           static_cast<unsigned long>(accepted ? TINYAWAIT_MAX_TASKS : 0),
           rejected ? "exactly one error" : "wrong error count",
           static_cast<unsigned long>(tinyawait::active_frames()),
           static_cast<unsigned long>(tinyawait::active_timers()));
  recordTest("capacity 32/33", pass, detail);
  return pass;
}

bool testInvalidDelays() {
  currentPhase = "INVALID_DELAY";
  const uint32_t errorsBefore = g_tinyawaitErrors;
  invalidNegativeDelay();
  invalidTooLargeDelay();
  serviceRuntime();

  const uint32_t errors = g_tinyawaitErrors - errorsBefore;
  const bool pass = errors == 2U &&
                    tinyawait::active_frames() == 0 &&
                    tinyawait::active_timers() == 0;
  if (errors == 2U) expectedErrorCount += 2U;
  char detail[160];
  snprintf(detail, sizeof(detail), "expected_errors=2 actual_errors=%lu | after=%lu/%lu",
           static_cast<unsigned long>(errors),
           static_cast<unsigned long>(tinyawait::active_frames()),
           static_cast<unsigned long>(tinyawait::active_timers()));
  recordTest("invalid delay validation", pass, detail);
  return pass;
}

void advanceFakeClock(uint32_t delta) {
  g_fakeNow = static_cast<uint32_t>(g_fakeNow + delta);
  tinyawait::poll();
  updateFrameWatermark();
  handleSerial();
}

bool testWraparoundAndMaximumDelay() {
  currentPhase = "WRAPAROUND";
  if (tinyawait::active_frames() != 0 || tinyawait::active_timers() != 0) {
    recordTest("uint32 wrap + max delay", false, "not clean at start");
    return false;
  }

  maxDelayFired = false;
  g_useFakeClock = true;
  g_fakeNow = (std::numeric_limits<uint32_t>::max)() - 500U;
  maximumDelayTask();

  bool stayedPending = !maxDelayFired;
  advanceFakeClock(1000U);
  stayedPending = stayedPending && !maxDelayFired;
  advanceFakeClock(2000000000U);
  stayedPending = stayedPending && !maxDelayFired;
  advanceFakeClock(2000000000U);
  stayedPending = stayedPending && !maxDelayFired;
  advanceFakeClock(294966294U);
  stayedPending = stayedPending && !maxDelayFired;
  advanceFakeClock(1U);

  const bool firedAtEnd = maxDelayFired;
  const bool clean = tinyawait::active_frames() == 0 &&
                     tinyawait::active_timers() == 0;
  g_useFakeClock = false;
  const bool pass = stayedPending && firedAtEnd && clean;
  recordTest("uint32 wrap + max delay", pass,
             pass ? "no early fire; fired after UINT32_MAX elapsed ms; clean"
                  : "early/missing fire or cleanup failure");
  return pass;
}

uint32_t mixedFrameContribution(size_t size, uint32_t token) {
  uint32_t result = 0;
  for (size_t i = 0; i < size; i += 16) result += (token + i) & 0xFFU;
  return result;
}

bool waitForFramesToDrain(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (elapsedAtMost(start, timeoutMs)) {
    serviceRuntime();
    if (tinyawait::active_frames() == 0 && tinyawait::active_timers() == 0) return true;
  }
  return tinyawait::active_frames() == 0 && tinyawait::active_timers() == 0;
}

bool testMixedFrames() {
  currentPhase = "MIXED_FRAMES";
  mixedDone = 0;
  mixedChecksum = 0;
  uint32_t expectedChecksum = 0;
  const uint32_t start = millis();

  for (uint32_t batch = 0; batch < MIXED_BATCHES; ++batch) {
    const uint32_t token = batch * 9U;
    mixedFrameTask<16>(7, token + 1U);
    mixedFrameTask<32>(3, token + 2U);
    mixedFrameTask<48>(9, token + 3U);
    mixedFrameTask<64>(4, token + 4U);
    mixedFrameTask<96>(11, token + 5U);
    mixedFrameTask<128>(5, token + 6U);
    mixedFrameTask<160>(8, token + 7U);
    mixedFrameTask<192>(6, token + 8U);
    mixedFrameTask<224>(10, token + 9U);

    expectedChecksum += mixedFrameContribution(16, token + 1U);
    expectedChecksum += mixedFrameContribution(32, token + 2U);
    expectedChecksum += mixedFrameContribution(48, token + 3U);
    expectedChecksum += mixedFrameContribution(64, token + 4U);
    expectedChecksum += mixedFrameContribution(96, token + 5U);
    expectedChecksum += mixedFrameContribution(128, token + 6U);
    expectedChecksum += mixedFrameContribution(160, token + 7U);
    expectedChecksum += mixedFrameContribution(192, token + 8U);
    expectedChecksum += mixedFrameContribution(224, token + 9U);

    const uint32_t batchStart = millis();
    while (mixedDone < (batch + 1U) * 9U) {
      serviceRuntime();
      if (!elapsedAtMost(batchStart, 1000U)) break;
    }
    if (mixedDone < (batch + 1U) * 9U) break;
    if ((batch + 1U) % 100U == 0U) recordProgress("mixed-batches", batch + 1U, MIXED_BATCHES);
    if (!elapsedAtMost(start, 20000U)) break;
  }

  const bool complete = mixedDone == MIXED_BATCHES * 9U;
  const bool clean = waitForFramesToDrain(1000U);
  const bool pass = complete && clean && mixedChecksum == expectedChecksum &&
                    g_tinyawaitErrors == expectedErrorCount;
  char detail[200];
  snprintf(detail, sizeof(detail),
           "batches=%lu done=%lu checksum=%lu/%lu max_frame=%lu search=%lu",
           static_cast<unsigned long>(MIXED_BATCHES),
           static_cast<unsigned long>(mixedDone),
           static_cast<unsigned long>(mixedChecksum),
           static_cast<unsigned long>(expectedChecksum),
           static_cast<unsigned long>(tinyawait::max_frame_size()),
           static_cast<unsigned long>(tinyawait::frame_allocator_max_search()));
  recordTest("mixed real coroutine frames", pass, detail);
  return pass;
}

struct LiveFrame {
  void* pointer = nullptr;
  size_t requested = 0;
  size_t span = 0;
  uint8_t pattern = 0;
};

LiveFrame liveFrames[TINYAWAIT_MAX_TASKS] = {};
uint32_t liveFrameCount = 0;
uint32_t rngState = 0xC001D00DU;

uint32_t nextRandom() {
  rngState = rngState * 1664525U + 1013904223U;
  return rngState;
}

bool validateLiveFrames() {
  const uintptr_t poolBegin = reinterpret_cast<uintptr_t>(tinyawait::detail::frame_pool);
  const uintptr_t poolEnd = poolBegin + tinyawait::detail::frame_pool_usable_bytes;

  for (uint32_t i = 0; i < liveFrameCount; ++i) {
    auto& frame = liveFrames[i];
    const uintptr_t begin = reinterpret_cast<uintptr_t>(frame.pointer);
    const uintptr_t end = begin + frame.span;
    if (!frame.pointer || begin < poolBegin || end > poolEnd ||
        (begin % tinyawait::detail::frame_alignment) != 0U) return false;
    const auto* bytes = static_cast<const uint8_t*>(frame.pointer);
    for (size_t j = 0; j < frame.span; ++j) {
      if (bytes[j] != frame.pattern) return false;
    }

    for (uint32_t j = i + 1U; j < liveFrameCount; ++j) {
      const uintptr_t otherBegin = reinterpret_cast<uintptr_t>(liveFrames[j].pointer);
      const uintptr_t otherEnd = otherBegin + liveFrames[j].span;
      if (begin < otherEnd && otherBegin < end) return false;
    }
  }
  return tinyawait::active_frames() == liveFrameCount &&
         tinyawait::active_timers() == 0;
}

bool testAllocatorFuzz() {
  currentPhase = "ALLOCATOR_FUZZ";
  std::memset(liveFrames, 0, sizeof(liveFrames));
  liveFrameCount = 0;
  rngState = 0xC001D00DU;
  const uint32_t searchBefore = tinyawait::frame_allocator_max_search();

  bool valid = true;
  for (uint32_t operation = 1; operation <= FUZZ_OPERATIONS; ++operation) {
    const bool shouldAllocate =
        liveFrameCount == 0 ||
        (liveFrameCount < TINYAWAIT_MAX_TASKS && (nextRandom() % 100U) < 62U);

    if (shouldAllocate) {
      const size_t requested = 1U + (nextRandom() % 400U);
      void* pointer = tinyawait::detail::alloc_frame(requested);
      if (pointer && liveFrameCount < TINYAWAIT_MAX_TASKS) {
        auto& frame = liveFrames[liveFrameCount++];
        frame.pointer = pointer;
        frame.requested = requested;
        frame.span = tinyawait::detail::normalize_frame_size(requested);
        frame.pattern = static_cast<uint8_t>(0xA5U ^ (operation & 0xFFU));
        std::memset(frame.pointer, frame.pattern, frame.span);
      } else if (pointer) {
        tinyawait::detail::free_frame(pointer, requested);
        valid = false;
      }
    } else if (liveFrameCount > 0) {
      const uint32_t index = nextRandom() % liveFrameCount;
      auto& frame = liveFrames[index];
      const auto* bytes = static_cast<const uint8_t*>(frame.pointer);
      for (size_t j = 0; j < frame.span; ++j) {
        if (bytes[j] != frame.pattern) valid = false;
      }
      tinyawait::detail::free_frame(frame.pointer, frame.requested);
      liveFrames[index] = liveFrames[liveFrameCount - 1U];
      liveFrames[liveFrameCount - 1U] = {};
      --liveFrameCount;
    }

    if ((operation & 0xFFU) == 0U) {
      if (!validateLiveFrames()) valid = false;
      serviceRuntime();
    }
    if ((operation % 10000U) == 0U) recordProgress("allocator-ops", operation, FUZZ_OPERATIONS);
  }

  if (!validateLiveFrames()) valid = false;
  while (liveFrameCount > 0) {
    auto& frame = liveFrames[liveFrameCount - 1U];
    tinyawait::detail::free_frame(frame.pointer, frame.requested);
    frame = {};
    --liveFrameCount;
  }

  void* whole = tinyawait::detail::alloc_frame(tinyawait::detail::frame_pool_usable_bytes);
  const bool recovered = whole != nullptr;
  if (whole) tinyawait::detail::free_frame(whole, tinyawait::detail::frame_pool_usable_bytes);

  const bool clean = tinyawait::active_frames() == 0 && tinyawait::active_timers() == 0;
  const bool pass = valid && recovered && clean &&
                    g_tinyawaitErrors == expectedErrorCount;
  char detail[220];
  snprintf(detail, sizeof(detail),
           "ops=%lu recovered=%s max_frame=%lu search=%lu->%lu active=%lu/%lu",
           static_cast<unsigned long>(FUZZ_OPERATIONS), recovered ? "YES" : "NO",
           static_cast<unsigned long>(tinyawait::max_frame_size()),
           static_cast<unsigned long>(searchBefore),
           static_cast<unsigned long>(tinyawait::frame_allocator_max_search()),
           static_cast<unsigned long>(tinyawait::active_frames()),
           static_cast<unsigned long>(tinyawait::active_timers()));
  recordTest("allocator fuzz 250000 ops", pass, detail);
  return pass;
}

bool runSchedulerRounds(uint32_t totalOperations, bool reportProgress) {
  const uint32_t rounds = totalOperations / 20U;
  for (uint32_t round = 0; round < rounds; ++round) {
    const uint32_t roundStartCompleted = schedulerCompleted;
    for (uint8_t i = 0; i < 20U; ++i) {
      shortTimerTask(1U + (i % 3U));
      ++schedulerStarted;
    }
    updateFrameWatermark();

    const uint32_t waitStart = millis();
    while (schedulerCompleted - roundStartCompleted < 20U) {
      serviceRuntime();
      if (!elapsedAtMost(waitStart, 1000U)) return false;
    }
    if (tinyawait::active_frames() != 0 || tinyawait::active_timers() != 0) return false;

    if (reportProgress && (round + 1U) % 2500U == 0U) {
      recordProgress("scheduler-completions", schedulerCompleted, totalOperations);
    }
  }
  return schedulerCompleted == schedulerStarted &&
         schedulerCompleted >= totalOperations &&
         tinyawait::active_frames() == 0 && tinyawait::active_timers() == 0;
}

bool testSchedulerStress() {
  currentPhase = "SCHEDULER_STRESS";
  schedulerCompleted = 0;
  schedulerStarted = 0;
  const bool mainPass = runSchedulerRounds(SCHEDULER_OPERATIONS, true);
  const uint32_t mainCompleted = schedulerCompleted;
  const bool repeatPass = runSchedulerRounds(REPEAT_OPERATIONS, true);
  const uint32_t total = schedulerCompleted;
  const bool pass = mainPass && repeatPass && total == SCHEDULER_OPERATIONS + REPEAT_OPERATIONS &&
                    g_tinyawaitErrors == expectedErrorCount;
  char detail[220];
  snprintf(detail, sizeof(detail),
           "main=%lu repeat=%lu total=%lu started=%lu active=%lu/%lu",
           static_cast<unsigned long>(mainCompleted),
           static_cast<unsigned long>(total - mainCompleted),
           static_cast<unsigned long>(total),
           static_cast<unsigned long>(schedulerStarted),
           static_cast<unsigned long>(tinyawait::active_frames()),
           static_cast<unsigned long>(tinyawait::active_timers()));
  recordTest("600000 timer/coroutine ops", pass, detail);
  return pass;
}

void captureHeap(const char* label,
                 uint32_t& freeHeap,
                 uint32_t& minFreeHeap,
                 uint32_t& largestAlloc,
                 uint32_t& capsFree,
                 uint32_t& capsLargest,
                 uint32_t& capsMin) {
  freeHeap = ESP.getFreeHeap();
  minFreeHeap = ESP.getMinFreeHeap();
  largestAlloc = ESP.getMaxAllocHeap();
  capsFree = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  capsLargest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  capsMin = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  printHeapSnapshot(label, freeHeap, minFreeHeap, largestAlloc,
                    capsFree, capsLargest, capsMin);
}

bool testHeapStability() {
  currentPhase = "HEAP_STABILITY";
  const uint32_t idleStart = millis();
  while (static_cast<uint32_t>(millis() - idleStart) < 2000U) serviceRuntime();
  captureHeap("after-scheduler", heapAfter, heapMinAfter, largestAfter,
              capsFreeAfter, capsLargestAfter, capsMinAfter);

  const int32_t freeDelta = static_cast<int32_t>(heapAfter) - static_cast<int32_t>(heapBefore);
  const int32_t largestDelta = static_cast<int32_t>(largestAfter) - static_cast<int32_t>(largestBefore);
  const bool pass = freeDelta >= -512 && largestDelta >= -512 &&
                    g_tinyawaitErrors == expectedErrorCount;
  char detail[220];
  snprintf(detail, sizeof(detail),
           "free=%lu->%lu delta=%ld | largest=%lu->%lu delta=%ld",
           static_cast<unsigned long>(heapBefore), static_cast<unsigned long>(heapAfter),
           static_cast<long>(freeDelta), static_cast<unsigned long>(largestBefore),
           static_cast<unsigned long>(largestAfter), static_cast<long>(largestDelta));
  recordTest("heap stability", pass, detail);
  return pass;
}

void spawnSoakTask(uint32_t token) {
  const uint32_t delayMs = 5U + (token % 46U);
  const uint32_t beforeErrors = g_tinyawaitErrors;
  switch (token % 4U) {
    case 0: soakFrameTask<16>(delayMs, token); break;
    case 1: soakFrameTask<64>(delayMs, token); break;
    case 2: soakFrameTask<160>(delayMs, token); break;
    default: soakParent(5U + (token % 20U), 5U + (token % 30U), token); break;
  }
  if (g_tinyawaitErrors == beforeErrors) ++soakStarted;
}

bool testSoak() {
  currentPhase = "180S_SOAK";
  soakStarted = 0;
  soakCompleted = 0;
  soakChecksum = 0;
  const uint32_t start = millis();
  uint32_t nextSpawn = start;
  uint32_t nextHeartbeat = start + SOAK_HEARTBEAT_MS;
  uint32_t token = 1;

  while (static_cast<uint32_t>(millis() - start) < SOAK_DURATION_MS) {
    serviceRuntime();
    const uint32_t now = millis();

    if (static_cast<int32_t>(now - nextSpawn) >= 0 && tinyawait::active_frames() < 16U) {
      const uint32_t slots = 16U - static_cast<uint32_t>(tinyawait::active_frames());
      const uint32_t count = slots > 3U ? 3U : slots;
      for (uint32_t i = 0; i < count; ++i) spawnSoakTask(token++);
      nextSpawn = now + 1U;
    }

    if (static_cast<int32_t>(now - nextHeartbeat) >= 0) {
      Serial.printf("[SOAK] elapsed=%lu started=%lu completed=%lu frames=%lu timers=%lu "
                    "free_heap=%lu max_frame=%lu allocator_search=%lu errors=%lu\n",
                    static_cast<unsigned long>(now - start),
                    static_cast<unsigned long>(soakStarted),
                    static_cast<unsigned long>(soakCompleted),
                    static_cast<unsigned long>(tinyawait::active_frames()),
                    static_cast<unsigned long>(tinyawait::active_timers()),
                    static_cast<unsigned long>(ESP.getFreeHeap()),
                    static_cast<unsigned long>(tinyawait::max_frame_size()),
                    static_cast<unsigned long>(tinyawait::frame_allocator_max_search()),
                    static_cast<unsigned long>(g_tinyawaitErrors));
      nextHeartbeat += SOAK_HEARTBEAT_MS;
    }
  }

  const uint32_t drainStart = millis();
  while (tinyawait::active_frames() != 0 || tinyawait::active_timers() != 0) {
    serviceRuntime();
    if (!elapsedAtMost(drainStart, 3000U)) break;
  }

  const bool balanced = soakStarted == soakCompleted;
  const bool clean = tinyawait::active_frames() == 0 && tinyawait::active_timers() == 0;
  const bool pass = balanced && clean && g_tinyawaitErrors == expectedErrorCount;
  char detail[220];
  snprintf(detail, sizeof(detail),
           "started=%lu completed=%lu checksum=%lu frames=%lu timers=%lu",
           static_cast<unsigned long>(soakStarted),
           static_cast<unsigned long>(soakCompleted),
           static_cast<unsigned long>(soakChecksum),
           static_cast<unsigned long>(tinyawait::active_frames()),
           static_cast<unsigned long>(tinyawait::active_timers()));
  recordTest("180-second continuous soak", pass, detail);
  return pass;
}

bool testFinalCleanup() {
  currentPhase = "FINAL_CLEANUP";
  const bool countersClean = tinyawait::active_frames() == 0 &&
                             tinyawait::active_timers() == 0;
  captureHeap("final", heapAfter, heapMinAfter, largestAfter,
              capsFreeAfter, capsLargestAfter, capsMinAfter);
  const bool errorsClean = g_tinyawaitErrors == expectedErrorCount;
  const bool pass = countersClean && errorsClean && !failureSeen;
  char detail[180];
  snprintf(detail, sizeof(detail),
           "frames=%lu timers=%lu expected_errors=%lu actual_errors=%lu",
           static_cast<unsigned long>(tinyawait::active_frames()),
           static_cast<unsigned long>(tinyawait::active_timers()),
           static_cast<unsigned long>(expectedErrorCount),
           static_cast<unsigned long>(g_tinyawaitErrors));
  recordTest("final scheduler cleanup", pass, detail);
  return pass;
}

void runV2Suite() {
  suiteStartedAtMs = millis();
  currentPhase = "STARTING";
  passedTests = 0;
  failedTests = 0;
  testNumber = 0;
  expectedErrorCount = 0;
  failureSeen = false;
  maximumConcurrentFrames = 0;
  schedulerCompleted = 0;
  schedulerStarted = 0;

  printDivider();
  Serial.println("[V2 SUITE] START");
  Serial.println("[V2 SUITE] Real ESP32-S3 stress validation; no simulated scheduler");
  printDivider();
  captureHeap("before-stress", heapBefore, heapMinBefore, largestBefore,
              capsFreeBefore, capsLargestBefore, capsMinBefore);

  const uint32_t start = millis();
  testCapacityBoundary();
  testInvalidDelays();
  testWraparoundAndMaximumDelay();
  testMixedFrames();
  testAllocatorFuzz();
  testSchedulerStress();
  testHeapStability();
  testSoak();
  testFinalCleanup();

  const uint32_t runtime = millis() - start;
  const bool withinTime = runtime <= MAX_STRESS_RUNTIME_MS;
  const bool allTestsPass = passedTests == 9U && failedTests == 0U;
  const bool finalCountersClean = tinyawait::active_frames() == 0 &&
                                  tinyawait::active_timers() == 0;
  const bool finalErrorsClean = g_tinyawaitErrors == expectedErrorCount;
  const bool finalPass = withinTime && allTestsPass && finalCountersClean &&
                         finalErrorsClean;

  printDivider();
  Serial.printf("[V2 METRICS] runtime_ms=%lu scheduler_started=%lu scheduler_completed=%lu\n",
                static_cast<unsigned long>(runtime),
                static_cast<unsigned long>(schedulerStarted),
                static_cast<unsigned long>(schedulerCompleted));
  Serial.printf("[V2 METRICS] frame_pool_bytes=%lu max_frame_size=%lu allocator_max_search=%lu\n",
                static_cast<unsigned long>(tinyawait::frame_pool_bytes),
                static_cast<unsigned long>(tinyawait::max_frame_size()),
                static_cast<unsigned long>(tinyawait::frame_allocator_max_search()));
  Serial.printf("[V2 METRICS] max_concurrent_frames=%lu mixed_done=%lu mixed_checksum=%lu\n",
                static_cast<unsigned long>(maximumConcurrentFrames),
                static_cast<unsigned long>(mixedDone),
                static_cast<unsigned long>(mixedChecksum));
  Serial.printf("[V2 METRICS] soak_started=%lu soak_completed=%lu soak_checksum=%lu\n",
                static_cast<unsigned long>(soakStarted),
                static_cast<unsigned long>(soakCompleted),
                static_cast<unsigned long>(soakChecksum));
  Serial.printf("[V2 METRICS] heap_before=%lu heap_after=%lu heap_delta=%ld largest_before=%lu largest_after=%lu largest_delta=%ld\n",
                static_cast<unsigned long>(heapBefore),
                static_cast<unsigned long>(heapAfter),
                static_cast<long>(static_cast<int32_t>(heapAfter) - static_cast<int32_t>(heapBefore)),
                static_cast<unsigned long>(largestBefore),
                static_cast<unsigned long>(largestAfter),
                static_cast<long>(static_cast<int32_t>(largestAfter) - static_cast<int32_t>(largestBefore)));
  Serial.printf("[V2 METRICS] final_active_frames=%lu final_active_timers=%lu expected_errors=%lu unexpected_errors=%lu\n",
                static_cast<unsigned long>(tinyawait::active_frames()),
                static_cast<unsigned long>(tinyawait::active_timers()),
                static_cast<unsigned long>(expectedErrorCount),
                static_cast<unsigned long>(g_tinyawaitErrors - expectedErrorCount));
  Serial.printf("[V2 SUMMARY] passed=%lu failed=%lu total=%lu\n",
                static_cast<unsigned long>(passedTests),
                static_cast<unsigned long>(failedTests),
                static_cast<unsigned long>(passedTests + failedTests));
  Serial.printf("[V2 RESULT] TINYAWAIT_ESP32S3_STRESS_VALIDATION=%s\n",
                finalPass ? "PASS" : "FAIL");
  printDivider();
  Serial.println("Type 'run' to repeat V2, or 'help' for commands.");

  currentPhase = "IDLE";
  suiteRunning = false;
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);
  printSystemInfo();
  printHelp();
  autoRunAtMs = millis() + AUTO_RUN_DELAY_MS;
  Serial.printf("[AUTO] V2 stress validation starts automatically in %lu ms.\n",
                static_cast<unsigned long>(AUTO_RUN_DELAY_MS));
}

void loop() {
  handleSerial();

  if (autoRunPending && !suiteRunning &&
      static_cast<int32_t>(millis() - autoRunAtMs) >= 0) {
    autoRunPending = false;
    suiteRunning = true;
  }

  if (suiteRunning) runV2Suite();

  // Keep the normal Arduino loop serviced even while idle.
  tinyawait::poll();
  updateFrameWatermark();
}
