#include <cassert>
#include <cstdint>

std::uint32_t shared_now = 0;
int shared_state = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_NOW_MS() shared_now
#include "TinyAwait.h"

void start_from_peer();
std::size_t peer_frames();
std::size_t peer_timers();

int main() {
    start_from_peer();
    assert(shared_state == 1);
    assert(peer_frames() == 1);
    assert(peer_timers() == 1);

    shared_now = 25;
    tinyawait::poll();
    assert(shared_state == 2);
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);
}
