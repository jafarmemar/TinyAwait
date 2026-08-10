#include <TinyAwait.h>

Async blinkForever() {
  while (true) {
    digitalWrite(LED_BUILTIN, HIGH);
    co_await 500;
    digitalWrite(LED_BUILTIN, LOW);
    co_await 500;
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  blinkForever();
}

void loop() {
  tinyawait::poll();
  // Wi-Fi, WebSocket, sensors, GPIO, and application logic remain free to run.
}
