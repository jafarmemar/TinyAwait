#include <TinyAwait.h>

#ifndef LED_BUILTIN
constexpr int LED_PIN = 2;  // Change this for boards without a built-in LED definition.
#else
constexpr int LED_PIN = LED_BUILTIN;
#endif

Async childDelayStep() {
  digitalWrite(LED_PIN, HIGH);
  co_await 200;
  digitalWrite(LED_PIN, LOW);
}

Async nestedDelaySequence() {
  co_await childDelayStep();
  co_await 800;
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  nestedDelaySequence();
}

void loop() {
  tinyawait::poll();
  // Networking, sensors, GPIO, and application logic stay responsive here.
}
