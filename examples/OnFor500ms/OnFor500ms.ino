#include <TinyAwait.h>

#ifndef LED_BUILTIN
constexpr int LED_PIN = 2;  // Change this for boards without a built-in LED definition.
#else
constexpr int LED_PIN = LED_BUILTIN;
#endif

Async turnOnFor500ms() {
  digitalWrite(LED_PIN, HIGH);
  co_await 500;
  digitalWrite(LED_PIN, LOW);
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  turnOnFor500ms();
}

void loop() {
  tinyawait::poll();
}
