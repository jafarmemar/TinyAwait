#include <TinyAwait.h>

Async turnOnFor500ms() {
  digitalWrite(LED_BUILTIN, HIGH);
  co_await 500;
  digitalWrite(LED_BUILTIN, LOW);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  turnOnFor500ms();
}

void loop() {
  tinyawait::poll();
}
