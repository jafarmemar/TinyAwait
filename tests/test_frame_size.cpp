#include <cassert>
#include <cstdint>

static std::uint32_t fake_now = 0;
static int errors = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_MAX_TASKS 2
#define TINYAWAIT_FRAME_SIZE 64
#define TINYAWAIT_NOW_MS() fake_now
#define TINYAWAIT_ON_ERROR() (++errors)
#include "TinyAwait.h"

Async too_large() {
    volatile std::uint8_t payload[160]{};
    payload[0] = 1;
    co_await 10;
    (void)payload[0];
}

int main() {
    too_large();
    assert(errors == 1);
    assert(tinyawait::last_frame_size() > TINYAWAIT_FRAME_SIZE);
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);
    return 0;
}
