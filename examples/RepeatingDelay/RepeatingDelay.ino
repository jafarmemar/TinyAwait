#include <TinyAwait.h>

#ifndef LED_BUILTIN
constexpr int LED_PIN = 2;  // Change this for your board if needed.
#else
constexpr int LED_PIN = LED_BUILTIN;
#endif

Async repeatingDelay() {
  while (true) {
    digitalWrite(LED_PIN, HIGH);
    co_await 500;
    digitalWrite(LED_PIN, LOW);
    co_await 500;
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  repeatingDelay();
}

void loop() {
  tinyawait::poll();
}
