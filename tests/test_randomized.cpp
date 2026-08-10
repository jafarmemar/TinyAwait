#include <cassert>
#include <cstdint>
#include <limits>

static std::uint32_t fake_now = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_NOW_MS() fake_now
#include "TinyAwait.h"

struct Item {
    std::uint32_t start{};
    std::uint32_t delay{};
    bool fired{};
};

Async wait_item(Item* item) {
    co_await item->delay;
    item->fired = true;
}

static std::uint32_t rng = 0x9E3779B9U;
static std::uint32_t next_random() {
    rng = rng * 1664525U + 1013904223U;
    return rng;
}

int main() {
    for (int round = 0; round < 200; ++round) {
        fake_now = (round % 2 == 0)
            ? static_cast<std::uint32_t>(std::numeric_limits<std::uint32_t>::max() - 5000U)
            : next_random();

        Item items[32]{};
        std::uint32_t max_delay = 0;
        for (auto& item : items) {
            item.start = fake_now;
            item.delay = 1U + (next_random() % 5000U);
            if (item.delay > max_delay) max_delay = item.delay;
            wait_item(&item);
        }

        assert(tinyawait::active_frames() == 32);
        assert(tinyawait::active_timers() == 32);

        std::uint32_t elapsed_total = 0;
        while (elapsed_total < max_delay) {
            std::uint32_t step = 1U + (next_random() % 97U);
            if (step > max_delay - elapsed_total) step = max_delay - elapsed_total;
            elapsed_total += step;
            fake_now = static_cast<std::uint32_t>(fake_now + step);
            tinyawait::poll();

            for (const auto& item : items) {
                const auto elapsed = static_cast<std::uint32_t>(fake_now - item.start);
                assert(item.fired == (elapsed >= item.delay));
            }
        }

        for (const auto& item : items) assert(item.fired);
        assert(tinyawait::active_frames() == 0);
        assert(tinyawait::active_timers() == 0);
        assert(tinyawait::detail::active_timer_count == 0);
    }
}
