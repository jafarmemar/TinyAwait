#include <cassert>
#include <cstdint>
#include <cstdlib>

static std::uint32_t fake_now = 0;
static int errors = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_MAX_TASKS 2
#define TINYAWAIT_NOW_MS() fake_now
#define TINYAWAIT_ON_ERROR() (++errors)
#include "TinyAwait.h"

Async hold() { co_await 1000; }

int main() {
    hold();
    hold();
    assert(tinyawait::active_frames() == 2);
    assert(tinyawait::active_timers() == 2);
    hold();
    assert(errors == 1);
    assert(tinyawait::active_frames() == 2);
    fake_now = 1000;
    tinyawait::poll();
    assert(tinyawait::active_frames() == 0);
    hold();
    assert(tinyawait::active_frames() == 1);
    return 0;
}
