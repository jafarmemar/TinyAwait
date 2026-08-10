#include <cassert>
#include <cstdint>

static std::uint32_t fake_now = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_NOW_MS() fake_now
#include "TinyAwait.h"

Async repeat(int* count) {
    for (int i = 0; i < 100; ++i) {
        co_await 1;
        ++*count;
    }
}

int main() {
    for (int round = 0; round < 250; ++round) {
        int counts[32]{};
        for (auto& count : counts) repeat(&count);
        assert(tinyawait::active_frames() == 32);
        assert(tinyawait::active_timers() == 32);
        for (int i = 0; i < 100; ++i) {
            ++fake_now;
            tinyawait::poll();
        }
        for (int count : counts) assert(count == 100);
        assert(tinyawait::active_frames() == 0);
        assert(tinyawait::active_timers() == 0);
    }
    return 0;
}
