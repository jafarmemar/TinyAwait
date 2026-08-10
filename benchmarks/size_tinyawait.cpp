#include <cstdint>
static std::uint32_t now_ms = 0;
#define TINYAWAIT_NOW_MS() now_ms
#include "TinyAwait.h"
Async child() { co_await 100; }
Async sample() { co_await child(); co_await 500; }
int main() { sample(); now_ms = 100; tinyawait::poll(); now_ms = 600; tinyawait::poll(); }
