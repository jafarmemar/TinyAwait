#include <cassert>
#include <cstdint>

static std::uint32_t fake_now = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_NOW_MS() fake_now
#include "TinyAwait.h"

Async child(int* state) {
    *state = 1;
    co_await 10;
    *state = 2;
}

Async parent(int* child_state, int* parent_state) {
    *parent_state = 1;
    co_await child(child_state);
    assert(*child_state == 2);
    *parent_state = 2;
    co_await 5;
    *parent_state = 3;
}


Async grandchild(int* value) {
    ++*value;
    co_await 3;
    ++*value;
}

Async middle(int* value) {
    co_await grandchild(value);
    ++*value;
    co_await 2;
    ++*value;
}

Async top(int* value) {
    co_await middle(value);
    ++*value;
}

Async immediate_child(int* value) {
    ++*value;
    co_return;
}

Async immediate_parent(int* value) {
    co_await immediate_child(value);
    ++*value;
}

int main() {
    int child_state = 0;
    int parent_state = 0;
    parent(&child_state, &parent_state);

    assert(child_state == 1);
    assert(parent_state == 1);
    assert(tinyawait::active_frames() == 2);
    assert(tinyawait::active_timers() == 1);

    fake_now = 9;
    tinyawait::poll();
    assert(child_state == 1 && parent_state == 1);

    fake_now = 10;
    tinyawait::poll();
    assert(child_state == 2);
    assert(parent_state == 2);
    assert(tinyawait::active_frames() == 1);
    assert(tinyawait::active_timers() == 1);

    fake_now = 15;
    tinyawait::poll();
    assert(parent_state == 3);
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);

    int deep = 0;
    top(&deep);
    assert(deep == 1);
    assert(tinyawait::active_frames() == 3);
    assert(tinyawait::active_timers() == 1);
    fake_now = 18;
    tinyawait::poll();
    assert(deep == 3);
    assert(tinyawait::active_frames() == 2);
    fake_now = 20;
    tinyawait::poll();
    assert(deep == 5);
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);

    int immediate = 0;
    immediate_parent(&immediate);
    assert(immediate == 2);
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);
    return 0;
}
