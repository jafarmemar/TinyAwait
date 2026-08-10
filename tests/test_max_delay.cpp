#include <cassert>
#include <cstdint>
#include <limits>

static std::uint32_t fake_now = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_NOW_MS() fake_now
#include "TinyAwait.h"

static bool fired = false;
Async max_delay_task() {
    co_await tinyawait::max_delay_ms;
    fired = true;
}

static void advance(std::uint32_t delta) {
    fake_now = static_cast<std::uint32_t>(fake_now + delta);
    tinyawait::poll();
}

int main() {
    assert(tinyawait::max_delay_ms == std::numeric_limits<std::uint32_t>::max());

    fake_now = std::numeric_limits<std::uint32_t>::max() - 500U;
    max_delay_task();
    assert(!fired);

    advance(1000U);          // crosses the 32-bit clock wrap
    assert(!fired);
    advance(2000000000U);
    assert(!fired);
    advance(2000000000U);
    assert(!fired);
    advance(294966294U);     // total elapsed = max_delay_ms - 1
    assert(!fired);
    advance(1U);
    assert(fired);
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);
    return 0;
}
