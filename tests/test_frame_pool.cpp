#include <cassert>
#include <cstdint>

static std::uint32_t fake_now = 0;
static int errors = 0;

#define TINYAWAIT_TESTING
#define TINYAWAIT_MAX_TASKS 8
#define TINYAWAIT_FRAME_POOL_BYTES 512
#define TINYAWAIT_NOW_MS() fake_now
#define TINYAWAIT_ON_ERROR() (++errors)
#include "TinyAwait.h"

static bool completed = false;

Async larger_frame() {
    volatile std::uint8_t payload[160]{};
    payload[0] = 1;
    co_await 10;
    completed = payload[0] == 1;
}

Async larger_than_pool() {
    volatile std::uint8_t payload[700]{};
    payload[0] = 1;
    co_await 10;
    (void)payload[0];
}

int main() {
    static_assert(tinyawait::frame_pool_bytes == 512U);

    larger_frame();
    assert(errors == 0);
    assert(tinyawait::last_frame_size() > 64U);
    assert(tinyawait::last_frame_size() <= tinyawait::frame_pool_bytes);
    assert(tinyawait::active_frames() == 1);
    assert(tinyawait::active_timers() == 1);

    fake_now = 10;
    tinyawait::poll();
    assert(completed);
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);

    larger_than_pool();
    assert(errors == 1);
    assert(tinyawait::last_frame_size() > tinyawait::frame_pool_bytes);
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);
    return 0;
}
