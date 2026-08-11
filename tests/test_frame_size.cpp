#include <cassert>
#include <cstdint>

static std::uint32_t fake_now = 0;
static int errors = 0;

#define TINYAWAIT_TESTING
#define TINYAWAIT_MAX_TASKS 8
// Legacy setting kept deliberately: it now contributes to the total pool
// budget (8 * 64 = 512 B) rather than imposing a 64 B per-frame ceiling.
#define TINYAWAIT_FRAME_SIZE 64
#define TINYAWAIT_NOW_MS() fake_now
#define TINYAWAIT_ON_ERROR() (++errors)
#include "TinyAwait.h"

static bool completed = false;

Async larger_than_legacy_slot() {
    volatile std::uint8_t payload[160]{};
    payload[0] = 1;
    co_await 10;
    completed = payload[0] == 1;
}

Async larger_than_entire_pool() {
    volatile std::uint8_t payload[700]{};
    payload[0] = 1;
    co_await 10;
    (void)payload[0];
}

int main() {
    static_assert(tinyawait::frame_pool_bytes <= 512U);

    larger_than_legacy_slot();
    assert(errors == 0);
    assert(tinyawait::last_frame_size() > TINYAWAIT_FRAME_SIZE);
    assert(tinyawait::last_frame_size() <= tinyawait::frame_pool_bytes);
    assert(tinyawait::active_frames() == 1);
    assert(tinyawait::active_timers() == 1);

    fake_now = 10;
    tinyawait::poll();
    assert(completed);
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);

    larger_than_entire_pool();
    assert(errors == 1);
    assert(tinyawait::last_frame_size() > tinyawait::frame_pool_bytes);
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);
    return 0;
}
