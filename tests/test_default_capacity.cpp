#include <cassert>
#include <cstdint>

static std::uint32_t fake_now = 0;
static int errors = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_NOW_MS() fake_now
#define TINYAWAIT_ON_ERROR() (++errors)
#include "TinyAwait.h"

static_assert(TINYAWAIT_MAX_TASKS == 32, "default task capacity must be 32");

Async hold() { co_await 100; }

int main() {
    for (int i = 0; i < 32; ++i) hold();
    assert(tinyawait::active_frames() == 32);
    assert(tinyawait::active_timers() == 32);

    hold();
    assert(errors == 1);
    assert(tinyawait::active_frames() == 32);

    fake_now = 100;
    tinyawait::poll();
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);

    for (int i = 0; i < 32; ++i) hold();
    assert(tinyawait::active_frames() == 32);
    fake_now = 200;
    tinyawait::poll();
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);
    return 0;
}
