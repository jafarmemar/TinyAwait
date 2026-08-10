#include <TinyAwait.h>

#ifndef LED_BUILTIN
constexpr int LED_PIN = 2;  // Change this for boards without a built-in LED definition.
#else
constexpr int LED_PIN = LED_BUILTIN;
#endif

Async flashOnce() {
  digitalWrite(LED_PIN, HIGH);
  co_await 200;
  digitalWrite(LED_PIN, LOW);
}

Async flashThenWait() {
  co_await flashOnce();
  co_await 800;
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  flashThenWait();
}

void loop() {
  tinyawait::poll();
  // Networking, sensors, GPIO, and application logic stay responsive here.
}
