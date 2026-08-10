#include <TinyAwait.h>

Async flashOnce() {
    digitalWrite(LED_BUILTIN, HIGH);
    co_await 200;
    digitalWrite(LED_BUILTIN, LOW);
}

Async flashThenWait() {
    co_await flashOnce();  // wait for the child coroutine to finish
    co_await 800;
}

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    flashThenWait();
}

void loop() {
    tinyawait::poll();
    // networking, sensors, GPIO, and application logic stay responsive here
}
