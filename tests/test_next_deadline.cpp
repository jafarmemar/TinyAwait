#include <cassert>
#include <cstdint>
#include <limits>

static std::uint32_t fake_now = 0;
static std::uint64_t clock_reads = 0;
static std::uint32_t read_now() noexcept { ++clock_reads; return fake_now; }
#define TINYAWAIT_TESTING
#define TINYAWAIT_MAX_TASKS 8
#define TINYAWAIT_NOW_MS() read_now()
#include "TinyAwait.h"

Async mark_after(std::uint32_t delay, int* value, int mark) {
    co_await delay;
    *value = mark;
}

Async increment_after(std::uint32_t delay, int* count) {
    co_await delay;
    ++*count;
}

Async chained(int* state) {
    *state = 1;
    co_await 10;
    *state = 2;
    co_await 20;
    *state = 3;
}

struct ReferenceItem {
    std::uint32_t delay{};
    bool fired{};
};

Async reference_wait(ReferenceItem* item) {
    co_await item->delay;
    item->fired = true;
}

static std::uint32_t rng_state = 0xA341316CU;
static std::uint32_t next_random() noexcept {
    rng_state = rng_state * 1664525U + 1013904223U;
    return rng_state;
}

static int wrap_rearm_state = 0;
static bool wrap_later_fired = false;

Async rearm_during_poll_wrap() {
    wrap_rearm_state = 1;
    co_await 1;
    wrap_rearm_state = 2;
    fake_now = 0;
    co_await 10;
    wrap_rearm_state = 3;
}

Async wait_across_poll_wrap() {
    co_await 100;
    wrap_later_fired = true;
}

int main() {
    // Empty poll remains a zero-clock-read fast path.
    const auto reads_before_empty = clock_reads;
    for (int i = 0; i < 100; ++i) tinyawait::poll();
    assert(clock_reads == reads_before_empty);

    int a = 0, b = 0, c = 0;
    fake_now = 1000;
    mark_after(100, &a, 1);
    mark_after(300, &b, 1);
    assert(tinyawait::active_timers() == 2);

    // Repeated future polls must not resume anything.
    for (std::uint32_t t = 1001; t < 1050; ++t) {
        fake_now = t;
        tinyawait::poll();
        assert(a == 0 && b == 0);
    }

    // Insert a new timer earlier than the currently tracked nearest timer.
    fake_now = 1050;
    mark_after(20, &c, 1);
    fake_now = 1069;
    tinyawait::poll();
    assert(c == 0 && a == 0 && b == 0);
    fake_now = 1070;
    tinyawait::poll();
    assert(c == 1 && a == 0 && b == 0);

    // Expiring the nearest timer must recompute the next one correctly.
    fake_now = 1099;
    tinyawait::poll();
    assert(a == 0 && b == 0);
    fake_now = 1100;
    tinyawait::poll();
    assert(a == 1 && b == 0);
    fake_now = 1299;
    tinyawait::poll();
    assert(b == 0);
    fake_now = 1300;
    tinyawait::poll();
    assert(b == 1);
    assert(tinyawait::active_timers() == 0);

    // Same-deadline timers all fire in one scan.
    int same = 0;
    fake_now = 2000;
    increment_after(50, &same);
    increment_after(50, &same);
    increment_after(50, &same);
    fake_now = 2049;
    tinyawait::poll();
    assert(same == 0);
    fake_now = 2050;
    tinyawait::poll();
    assert(same == 3);

    // A resumed coroutine may schedule its next delay into a slot already visited.
    int chain_state = 0;
    fake_now = 3000;
    chained(&chain_state);
    fake_now = 3010;
    tinyawait::poll();
    assert(chain_state == 2);
    fake_now = 3029;
    tinyawait::poll();
    assert(chain_state == 2);
    fake_now = 3030;
    tinyawait::poll();
    assert(chain_state == 3);

    // Do not accidentally adopt the common signed-half-range deadline limitation.
    int huge_gap = 0;
    fake_now = 4000;
    mark_after(100, &huge_gap, 1);
    fake_now = static_cast<std::uint32_t>(fake_now + 3000000000U);
    tinyawait::poll();
    assert(huge_gap == 1);

    // Wrap boundary remains correct.
    int wrapped = 0;
    fake_now = std::numeric_limits<std::uint32_t>::max() - 10U;
    mark_after(20, &wrapped, 1);
    fake_now = std::numeric_limits<std::uint32_t>::max() - 1U;
    tinyawait::poll();
    assert(wrapped == 0);
    fake_now = 8;
    tinyawait::poll();
    assert(wrapped == 0);
    fake_now = 9;
    tinyawait::poll();
    assert(wrapped == 1);

    // A coroutine resumed by poll() can cross the 32-bit clock wrap before
    // registering its next delay. The rest of the same scan must not combine
    // the pre-wrap `now` value with the new clock epoch.
    wrap_rearm_state = 0;
    wrap_later_fired = false;
    fake_now = std::numeric_limits<std::uint32_t>::max() - 2U;
    rearm_during_poll_wrap();
    wait_across_poll_wrap();
    fake_now = std::numeric_limits<std::uint32_t>::max() - 1U;
    tinyawait::poll();
    assert(wrap_rearm_state == 2);
    assert(!wrap_later_fired);
    fake_now = 9U;
    tinyawait::poll();
    assert(wrap_rearm_state == 2);
    assert(!wrap_later_fired);
    fake_now = 10U;
    tinyawait::poll();
    assert(wrap_rearm_state == 3);
    assert(!wrap_later_fired);
    fake_now = 96U;
    tinyawait::poll();
    assert(!wrap_later_fired);
    fake_now = 97U;
    tinyawait::poll();
    assert(wrap_later_fired);
    assert(tinyawait::active_timers() == 0);

    // Full uint32 delay still works while poll() advances the global countdown in chunks.
    int max_fired = 0;
    fake_now = std::numeric_limits<std::uint32_t>::max() - 500U;
    mark_after(tinyawait::max_delay_ms, &max_fired, 1);
    auto advance = [&](std::uint32_t delta) {
        fake_now = static_cast<std::uint32_t>(fake_now + delta);
        tinyawait::poll();
    };
    advance(1000U); assert(max_fired == 0);
    advance(2000000000U); assert(max_fired == 0);
    advance(2000000000U); assert(max_fired == 0);
    advance(294966294U); assert(max_fired == 0);
    advance(1U); assert(max_fired == 1);

    // The final poll may overshoot the max-delay deadline after a full clock wrap.
    // This is the regression case that rejects a naive global countdown which leaves
    // per-timer elapsed state stale during the fast path.
    int max_overshoot = 0;
    fake_now = 1000U;
    mark_after(tinyawait::max_delay_ms, &max_overshoot, 1);
    fake_now = static_cast<std::uint32_t>(fake_now + 3000000000U);
    tinyawait::poll();
    assert(max_overshoot == 0);
    fake_now = static_cast<std::uint32_t>(fake_now + 2000000000U);
    tinyawait::poll();
    assert(max_overshoot == 1);

    // Deterministic full-range reference testing. The reference uses a 64-bit
    // logical elapsed count only in the test; production remains 32-bit hot-path.
    for (int round = 0; round < 500; ++round) {
        fake_now = next_random();
        ReferenceItem items[TINYAWAIT_MAX_TASKS]{};
        std::uint32_t max_delay = 0;
        for (auto& item : items) {
            item.delay = next_random();
            if (item.delay == 0) item.delay = 1;
            if (item.delay > max_delay) max_delay = item.delay;
            reference_wait(&item);
        }

        std::uint64_t elapsed = 0;
        while (elapsed < max_delay) {
            auto delta = next_random();
            if (delta == 0) delta = 1;
            elapsed += delta;
            fake_now = static_cast<std::uint32_t>(fake_now + delta);
            tinyawait::poll();
            for (const auto& item : items) {
                assert(item.fired == (elapsed >= item.delay));
            }
        }
        for (const auto& item : items) assert(item.fired);
        assert(tinyawait::active_frames() == 0);
        assert(tinyawait::active_timers() == 0);
    }

    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);
}
