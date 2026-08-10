#include <TinyAwait.h>

#ifndef LED_BUILTIN
constexpr int LED_PIN = 2;  // Change this for your board if needed.
#else
constexpr int LED_PIN = LED_BUILTIN;
#endif

Async singleDelay() {
  digitalWrite(LED_PIN, HIGH);
  co_await 500;
  digitalWrite(LED_PIN, LOW);
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  singleDelay();
}

void loop() {
  tinyawait::poll();
}
