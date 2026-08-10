#include <cassert>
#include <cstdint>

static std::uint32_t fake_now = 0;
static int errors = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_NOW_MS() fake_now
#define TINYAWAIT_ON_ERROR() (++errors)
#include "TinyAwait.h"

Async signed_delay(int delay, int* state) {
    *state = 1;
    co_await delay;
    *state = 2;
}

Async wide_delay(std::uint64_t delay, int* state) {
    *state = 1;
    co_await delay;
    *state = 2;
}

int main() {
    int state = 0;
    signed_delay(-1, &state);
    assert(errors == 1);
    assert(state == 2);
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);

    state = 0;
    wide_delay(static_cast<std::uint64_t>(tinyawait::max_delay_ms) + 1ULL, &state);
    assert(errors == 2);
    assert(state == 2);
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);

    state = 0;
    signed_delay(10, &state);
    assert(state == 1);
    fake_now = 10;
    tinyawait::poll();
    assert(state == 2);
}
