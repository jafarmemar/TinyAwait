#include <TinyAwait.h>

#ifndef LED_BUILTIN
constexpr int LED_PIN = 2;  // Change this for your board if needed.
#else
constexpr int LED_PIN = LED_BUILTIN;
#endif

Async childDelay() {
  digitalWrite(LED_PIN, HIGH);
  co_await 200;
  digitalWrite(LED_PIN, LOW);
}

Async nestedDelay() {
  co_await childDelay();
  co_await 800;
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  nestedDelay();
}

void loop() {
  tinyawait::poll();
}
