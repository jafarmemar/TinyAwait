#include <cstddef>
#include <cstdint>

extern std::uint32_t shared_now;
extern int shared_state;
#define TINYAWAIT_TESTING
#define TINYAWAIT_NOW_MS() shared_now
#include "TinyAwait.h"

Async peer_task() {
    shared_state = 1;
    co_await 25;
    shared_state = 2;
}

void start_from_peer() { peer_task(); }
std::size_t peer_frames() { return tinyawait::active_frames(); }
std::size_t peer_timers() { return tinyawait::active_timers(); }
