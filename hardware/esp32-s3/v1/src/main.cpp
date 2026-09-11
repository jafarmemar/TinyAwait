#include <Arduino.h>
#include <TinyAwait.h>

// -----------------------------------------------------------------------------
// TinyAwait ESP32-S3 real-hardware validation
//
// Hardware required: ESP32-S3 + one USB cable.
// No LED, sensor, Wi-Fi, PSRAM, or other peripheral is required.
//
// TinyAwait usage patterns demonstrated below:
//   1) co_await <milliseconds>
//   2) sequential waits
//   3) co_await childCoroutine()
//   4) detached/concurrent coroutines
//   5) repeating waits
//   6) tinyawait::poll() in loop()
// -----------------------------------------------------------------------------

namespace {

constexpr unsigned long SERIAL_BAUD = 115200;
constexpr uint32_t AUTO_RUN_DELAY_MS = 2000;
constexpr const char* TINYAWAIT_TESTED_COMMIT =
    "868304d548074a07274665ebe4d3fb26854c4f14";

bool suiteRunning = false;
bool finalizePending = false;
bool autoRunPending = true;
uint32_t autoRunAtMs = 0;

uint32_t passedTests = 0;
uint32_t failedTests = 0;
uint32_t testNumber = 0;
uint64_t loopCount = 0;

// Background task used to prove that one coroutine can keep progressing while
// another coroutine is waiting.
bool backgroundStopRequested = false;
uint32_t backgroundTicks = 0;

// State used by the nested-coroutine test.
uint8_t nestedChildStage = 0;

// State used by the detached/concurrent-coroutine test.
uint32_t concurrentStartMs = 0;
uint8_t completionCount = 0;
uint8_t completionOrder[3] = {0, 0, 0};
uint32_t completionAtMs[3] = {0, 0, 0};

// State used by the repeating-coroutine test.
uint8_t repeatCount = 0;

// Non-blocking serial command parser.
char commandBuffer[32] = {};
size_t commandLength = 0;

bool elapsedInRange(uint32_t elapsed, uint32_t minimum, uint32_t maximum) {
  return elapsed >= minimum && elapsed <= maximum;
}

void beginTestResult(const char* name, bool pass) {
  ++testNumber;
  if (pass) {
    ++passedTests;
  } else {
    ++failedTests;
  }

  Serial.printf("[TEST %02lu] %-30s %s",
                static_cast<unsigned long>(testNumber),
                name,
                pass ? "PASS" : "FAIL");
}

void recordTiming(const char* name,
                  bool pass,
                  uint32_t actualMs,
                  uint32_t expectedMinMs,
                  uint32_t expectedMaxMs) {
  beginTestResult(name, pass);
  Serial.printf(" | actual=%lu ms | expected=%lu..%lu ms\n",
                static_cast<unsigned long>(actualMs),
                static_cast<unsigned long>(expectedMinMs),
                static_cast<unsigned long>(expectedMaxMs));
}

void recordBoolean(const char* name, bool pass, const char* detail) {
  beginTestResult(name, pass);
  Serial.printf(" | %s\n", detail);
}

void printDivider() {
  Serial.println("----------------------------------------------------------------");
}

void printSystemInfo() {
  printDivider();
  Serial.println("TinyAwait ESP32-S3 Real Hardware Validation");
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
  Serial.printf("TinyAwait commit    : %s\n", TINYAWAIT_TESTED_COMMIT);
  Serial.printf("TinyAwait frame pool: %lu bytes\n",
                static_cast<unsigned long>(tinyawait::frame_pool_bytes));
  Serial.printf("TinyAwait max tasks : %u\n", static_cast<unsigned>(TINYAWAIT_MAX_TASKS));
  printDivider();
}

void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  run     - run the complete TinyAwait hardware test again");
  Serial.println("  ping    - prove Serial + Arduino loop stay responsive");
  Serial.println("  status  - show scheduler/test status");
  Serial.println("  info    - print board/build information");
  Serial.println("  help    - show this help");
  Serial.println();
}

void printStatus() {
  Serial.printf("[STATUS] suite=%s | uptime=%lu ms | loop=%llu",
                suiteRunning ? "RUNNING" : "IDLE",
                static_cast<unsigned long>(millis()),
                static_cast<unsigned long long>(loopCount));
#ifdef TINYAWAIT_TESTING
  Serial.printf(" | frames=%lu | timers=%lu | max_frame=%lu | alloc_search_max=%lu",
                static_cast<unsigned long>(tinyawait::active_frames()),
                static_cast<unsigned long>(tinyawait::active_timers()),
                static_cast<unsigned long>(tinyawait::max_frame_size()),
                static_cast<unsigned long>(tinyawait::frame_allocator_max_search()));
#endif
  Serial.println();
}

Async backgroundTask() {
  while (!backgroundStopRequested) {
    ++backgroundTicks;
    co_await 25;
  }
}

Async testImmediateAwait() {
  const uint64_t loopBefore = loopCount;
  const uint32_t startMs = millis();

  co_await 0;

  const uint32_t elapsed = millis() - startMs;
  const bool pass = (loopCount == loopBefore) && (elapsed <= 2);
  recordTiming("co_await 0 (immediate)", pass, elapsed, 0, 2);
}

Async testSingleDelay() {
  const uint64_t loopBefore = loopCount;
  const uint32_t ticksBefore = backgroundTicks;
  const uint32_t startMs = millis();

  co_await 250;

  const uint32_t elapsed = millis() - startMs;
  const bool timingOk = elapsedInRange(elapsed, 250, 500);
  const bool loopProgressed = loopCount > loopBefore;
  const bool backgroundProgressed = backgroundTicks > ticksBefore;
  const bool pass = timingOk && loopProgressed && backgroundProgressed;

  recordTiming("single non-blocking delay", pass, elapsed, 250, 500);
  Serial.printf("          loop delta=%llu | background tick delta=%lu\n",
                static_cast<unsigned long long>(loopCount - loopBefore),
                static_cast<unsigned long>(backgroundTicks - ticksBefore));
}

Async testSequentialDelays() {
  const uint32_t startMs = millis();

  co_await 80;
  const uint32_t afterFirst = millis() - startMs;

  co_await 120;
  const uint32_t afterSecond = millis() - startMs;

  co_await 160;
  const uint32_t afterThird = millis() - startMs;

  const bool pass =
      elapsedInRange(afterFirst, 80, 230) &&
      elapsedInRange(afterSecond, 200, 400) &&
      elapsedInRange(afterThird, 360, 660);

  recordTiming("sequential delays", pass, afterThird, 360, 660);
  Serial.printf("          checkpoints=%lu, %lu, %lu ms\n",
                static_cast<unsigned long>(afterFirst),
                static_cast<unsigned long>(afterSecond),
                static_cast<unsigned long>(afterThird));
}

Async nestedChild() {
  nestedChildStage = 1;
  co_await 150;
  nestedChildStage = 2;
}

Async testNestedCoroutine() {
  nestedChildStage = 0;
  const uint32_t startMs = millis();

  co_await nestedChild();
  const uint32_t childFinishedAt = millis() - startMs;
  const bool childCompleted = (nestedChildStage == 2);

  co_await 120;
  const uint32_t totalElapsed = millis() - startMs;

  const bool pass =
      childCompleted &&
      elapsedInRange(childFinishedAt, 150, 350) &&
      elapsedInRange(totalElapsed, 270, 570);

  recordTiming("nested child coroutine", pass, totalElapsed, 270, 570);
  Serial.printf("          child finished at=%lu ms | child stage=%u\n",
                static_cast<unsigned long>(childFinishedAt),
                static_cast<unsigned>(nestedChildStage));
}

Async concurrentWorker(uint8_t id, uint32_t delayMs) {
  co_await delayMs;

  if (completionCount < 3) {
    const uint8_t index = completionCount++;
    completionOrder[index] = id;
    completionAtMs[index] = millis() - concurrentStartMs;
  }
}

Async testConcurrentDetachedTasks() {
  completionCount = 0;
  completionOrder[0] = completionOrder[1] = completionOrder[2] = 0;
  completionAtMs[0] = completionAtMs[1] = completionAtMs[2] = 0;
  concurrentStartMs = millis();

  concurrentWorker(1, 100);
  concurrentWorker(2, 200);
  concurrentWorker(3, 300);

  co_await 420;

  const bool countOk = completionCount == 3;
  const bool orderOk =
      countOk &&
      completionOrder[0] == 1 &&
      completionOrder[1] == 2 &&
      completionOrder[2] == 3;
  const bool timingOk =
      countOk &&
      elapsedInRange(completionAtMs[0], 100, 250) &&
      elapsedInRange(completionAtMs[1], 200, 350) &&
      elapsedInRange(completionAtMs[2], 300, 450);
  const bool pass = orderOk && timingOk;

  recordBoolean("detached concurrent tasks", pass,
                "3 tasks should finish in order 1 -> 2 -> 3");
  Serial.printf("          order=%u,%u,%u | times=%lu,%lu,%lu ms\n",
                static_cast<unsigned>(completionOrder[0]),
                static_cast<unsigned>(completionOrder[1]),
                static_cast<unsigned>(completionOrder[2]),
                static_cast<unsigned long>(completionAtMs[0]),
                static_cast<unsigned long>(completionAtMs[1]),
                static_cast<unsigned long>(completionAtMs[2]));
}

Async repeatWorker() {
  repeatCount = 0;
  for (uint8_t i = 0; i < 5; ++i) {
    co_await 60;
    ++repeatCount;
  }
}

Async testRepeatingAwait() {
  const uint32_t startMs = millis();

  co_await repeatWorker();

  const uint32_t elapsed = millis() - startMs;
  const bool pass = (repeatCount == 5) && elapsedInRange(elapsed, 300, 600);

  recordTiming("repeating await loop", pass, elapsed, 300, 600);
  Serial.printf("          repetitions=%u\n", static_cast<unsigned>(repeatCount));
}

Async runAllTests() {
  printDivider();
  Serial.println("[SUITE] START");
  printDivider();

  backgroundStopRequested = false;
  backgroundTicks = 0;
  backgroundTask();

  co_await testImmediateAwait();
  co_await testSingleDelay();
  co_await testSequentialDelays();
  co_await testNestedCoroutine();
  co_await testConcurrentDetachedTasks();
  co_await testRepeatingAwait();

  backgroundStopRequested = true;
  co_await 50;

  suiteRunning = false;
  finalizePending = true;
}

void startSuite() {
  if (suiteRunning) {
    Serial.println("[SUITE] Already running.");
    return;
  }

  passedTests = 0;
  failedTests = 0;
  testNumber = 0;
  finalizePending = false;
  suiteRunning = true;

  runAllTests();
}

void finalizeSuite() {
  finalizePending = false;

#ifdef TINYAWAIT_TESTING
  const size_t activeFrames = tinyawait::active_frames();
  const size_t activeTimers = tinyawait::active_timers();
  const bool cleanupOk = (activeFrames == 0) && (activeTimers == 0);

  beginTestResult("scheduler cleanup", cleanupOk);
  Serial.printf(" | active_frames=%lu | active_timers=%lu\n",
                static_cast<unsigned long>(activeFrames),
                static_cast<unsigned long>(activeTimers));

  Serial.printf("[METRICS] max_frame=%lu bytes | frame_pool=%lu bytes | allocator_max_search=%lu\n",
                static_cast<unsigned long>(tinyawait::max_frame_size()),
                static_cast<unsigned long>(tinyawait::frame_pool_bytes),
                static_cast<unsigned long>(tinyawait::frame_allocator_max_search()));
#endif

  printDivider();
  Serial.printf("[SUMMARY] passed=%lu | failed=%lu | total=%lu\n",
                static_cast<unsigned long>(passedTests),
                static_cast<unsigned long>(failedTests),
                static_cast<unsigned long>(passedTests + failedTests));
  Serial.printf("[RESULT] TINYAWAIT_ESP32S3_HARDWARE_TEST=%s\n",
                failedTests == 0 ? "PASS" : "FAIL");
  printDivider();
  Serial.println("Type 'run' to repeat the suite, or 'help' for commands.");
}

void processCommand(const char* command) {
  if (strcmp(command, "run") == 0) {
    autoRunPending = false;
    startSuite();
  } else if (strcmp(command, "ping") == 0) {
    Serial.printf("[PONG] uptime=%lu ms | loop=%llu | suite=%s\n",
                  static_cast<unsigned long>(millis()),
                  static_cast<unsigned long long>(loopCount),
                  suiteRunning ? "RUNNING" : "IDLE");
  } else if (strcmp(command, "status") == 0) {
    printStatus();
  } else if (strcmp(command, "info") == 0) {
    printSystemInfo();
  } else if (strcmp(command, "help") == 0 || strcmp(command, "?") == 0) {
    printHelp();
  } else if (command[0] != '\0') {
    Serial.printf("[COMMAND] Unknown: '%s' (type 'help')\n", command);
  }
}

void handleSerial() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());

    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      commandBuffer[commandLength] = '\0';
      processCommand(commandBuffer);
      commandLength = 0;
      continue;
    }

    if (commandLength + 1 < sizeof(commandBuffer)) {
      commandBuffer[commandLength++] = ch;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);

  printSystemInfo();
  printHelp();

  autoRunAtMs = millis() + AUTO_RUN_DELAY_MS;
  Serial.printf("[AUTO] Complete test starts automatically in %lu ms.\n",
                static_cast<unsigned long>(AUTO_RUN_DELAY_MS));
}

void loop() {
  ++loopCount;
  handleSerial();

  if (autoRunPending && !suiteRunning &&
      static_cast<int32_t>(millis() - autoRunAtMs) >= 0) {
    autoRunPending = false;
    startSuite();
  }

  tinyawait::poll();

  if (finalizePending) {
    finalizeSuite();
  }
}
