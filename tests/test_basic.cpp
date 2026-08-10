#include <cassert>
#include <cstdint>

static std::uint32_t fake_now = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_MAX_TASKS 4
#define TINYAWAIT_NOW_MS() fake_now
#include "TinyAwait.h"

static int step = 0;

Async sequential() {
    step = 1;
    co_await 0;
    assert(step == 1);
    step = 2;
    co_await 1;
    step = 3;
    co_await 500;
    step = 4;
}

Async forever_counter(int* count) {
    for (;;) {
        ++*count;
        co_await 10;
    }
}

Async one_shot(int* value) {
    *value = 1;
    co_await 500;
    *value = 2;
}

int main() {
    assert(tinyawait::active_frames() == 0);
    sequential();
    assert(step == 2);
    assert(tinyawait::active_frames() == 1);
    assert(tinyawait::active_timers() == 1);

    tinyawait::poll();
    assert(step == 2);
    fake_now = 1;
    tinyawait::poll();
    assert(step == 3);
    fake_now = 500;
    tinyawait::poll();
    assert(step == 3);
    fake_now = 501;
    tinyawait::poll();
    assert(step == 4);
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);

    int a = 0, b = 0;
    one_shot(&a);
    one_shot(&b);
    assert(a == 1 && b == 1);
    assert(tinyawait::active_frames() == 2);
    fake_now += 499;
    tinyawait::poll();
    assert(a == 1 && b == 1);
    fake_now += 1;
    tinyawait::poll();
    assert(a == 2 && b == 2);
    assert(tinyawait::active_frames() == 0);

    int loops = 0;
    forever_counter(&loops);
    assert(loops == 1);
    fake_now += 10;
    tinyawait::poll();
    assert(loops == 2);
    assert(tinyawait::active_frames() == 1);
    assert(tinyawait::active_timers() == 1);
    return 0;
}
