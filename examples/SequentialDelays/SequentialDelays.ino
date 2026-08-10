#include <TinyAwait.h>

#ifndef LED_BUILTIN
constexpr int LED_PIN = 2;  // Change this for your board if needed.
#else
constexpr int LED_PIN = LED_BUILTIN;
#endif

Async sequentialDelays() {
  digitalWrite(LED_PIN, HIGH);
  co_await 200;

  digitalWrite(LED_PIN, LOW);
  co_await 800;

  digitalWrite(LED_PIN, HIGH);
  co_await 200;

  digitalWrite(LED_PIN, LOW);
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  sequentialDelays();
}

void loop() {
  tinyawait::poll();
}
