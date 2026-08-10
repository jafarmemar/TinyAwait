#include <TinyAwait.h>

#ifndef LED_BUILTIN
constexpr int LED_PIN = 2;  // Change this for boards without a built-in LED definition.
#else
constexpr int LED_PIN = LED_BUILTIN;
#endif

Async repeatingNonBlockingDelay() {
  while (true) {
    digitalWrite(LED_PIN, HIGH);
    co_await 500;
    digitalWrite(LED_PIN, LOW);
    co_await 500;
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  repeatingNonBlockingDelay();
}

void loop() {
  tinyawait::poll();
  // Wi-Fi, WebSocket, sensors, GPIO, and application logic remain free to run.
}
