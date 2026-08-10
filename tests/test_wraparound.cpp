#include <cassert>
#include <cstdint>
#include <limits>

static std::uint32_t fake_now = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_NOW_MS() fake_now
#include "TinyAwait.h"

static bool fired = false;
Async wrap_test() {
    co_await 20;
    fired = true;
}

int main() {
    fake_now = std::numeric_limits<std::uint32_t>::max() - 10U;
    wrap_test();
    fake_now = std::numeric_limits<std::uint32_t>::max() - 1U;
    tinyawait::poll();
    assert(!fired);
    fake_now = 8;
    tinyawait::poll();
    assert(!fired);
    fake_now = 9;
    tinyawait::poll();
    assert(fired);
    assert(tinyawait::active_frames() == 0);
    return 0;
}
