#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

static std::size_t heap_allocs = 0;
void* operator new(std::size_t n) {
    ++heap_allocs;
    if (void* p = std::malloc(n)) return p;
    std::abort();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

static std::uint32_t fake_now = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_NOW_MS() fake_now
#include "TinyAwait.h"

static int done = 0;
Async leaf() {
    co_await 1;
    ++done;
}

Async task() {
    co_await leaf();
    co_await 1;
    ++done;
}

int main() {
    const auto before = heap_allocs;
    for (int i = 0; i < 10000; ++i) {
        task();
        ++fake_now;
        tinyawait::poll();
        ++fake_now;
        tinyawait::poll();
    }
    assert(done == 20000);
    assert(heap_allocs == before);
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::active_timers() == 0);
    return 0;
}
