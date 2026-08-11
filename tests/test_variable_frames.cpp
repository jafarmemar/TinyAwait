#include <cassert>
#include <cstdint>

static std::uint32_t fake_now = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_MAX_TASKS 6
#define TINYAWAIT_FRAME_POOL_BYTES 2048
#define TINYAWAIT_NOW_MS() fake_now
#include "TinyAwait.h"

static int state = 0;
static bool detached_done = false;

Async large_child() {
    volatile std::uint8_t payload[240]{};
    payload[0] = 7;
    co_await 5;
    assert(payload[0] == 7);
    state = 1;
}

Async large_parent() {
    volatile std::uint8_t payload[176]{};
    payload[0] = 9;
    co_await large_child();
    assert(payload[0] == 9);
    state = 2;
    co_await 7;
    state = 3;
}

Async large_detached() {
    volatile std::uint8_t payload[192]{};
    payload[0] = 11;
    co_await 3;
    assert(payload[0] == 11);
    detached_done = true;
}

int main() {
    large_parent();
    assert(tinyawait::max_frame_size() > 128U);
    assert(tinyawait::active_frames() == 2U);
    assert(tinyawait::active_timers() == 1U);

    fake_now = 4;
    tinyawait::poll();
    assert(state == 0);

    fake_now = 5;
    tinyawait::poll();
    assert(state == 2);
    assert(tinyawait::active_frames() == 1U);

    fake_now = 12;
    tinyawait::poll();
    assert(state == 3);
    assert(tinyawait::active_frames() == 0U);
    assert(tinyawait::active_timers() == 0U);

    large_detached();
    assert(tinyawait::active_frames() == 1U);
    fake_now = 15;
    tinyawait::poll();
    assert(detached_done);
    assert(tinyawait::active_frames() == 0U);
    assert(tinyawait::active_timers() == 0U);
    return 0;
}
