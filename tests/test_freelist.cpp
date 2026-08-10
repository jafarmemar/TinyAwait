#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

static std::uint32_t fake_now = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_MAX_TASKS 64
#define TINYAWAIT_NOW_MS() fake_now
#include "TinyAwait.h"

static bool belongs_to_pool(void* p) {
    for (std::size_t i = 0; i < TINYAWAIT_MAX_TASKS; ++i) {
        if (p == static_cast<void*>(tinyawait::detail::frame_pool[i].bytes)) return true;
    }
    return false;
}

static std::size_t validate_free_list() {
    void* seen[TINYAWAIT_MAX_TASKS]{};
    std::size_t count = 0;
    void* node = tinyawait::detail::free_frame_head;
    while (node) {
        assert(belongs_to_pool(node));
        assert(count < TINYAWAIT_MAX_TASKS); // catches cycles
        for (std::size_t i = 0; i < count; ++i) assert(seen[i] != node);
        seen[count++] = node;
        void* next = nullptr;
        std::memcpy(&next, node, sizeof(next));
        node = next;
    }
    return count;
}

int main() {
    assert(tinyawait::active_frames() == 0);
    assert(tinyawait::detail::free_frame_head == nullptr);
    assert(tinyawait::detail::next_unused_frame == 0);

    void* ptrs[TINYAWAIT_MAX_TASKS]{};
    for (std::size_t i = 0; i < TINYAWAIT_MAX_TASKS; ++i) {
        ptrs[i] = tinyawait::detail::alloc_frame(1);
        assert(ptrs[i]);
        for (std::size_t j = 0; j < i; ++j) assert(ptrs[j] != ptrs[i]);
    }
    assert(tinyawait::active_frames() == TINYAWAIT_MAX_TASKS);
    assert(tinyawait::detail::alloc_frame(1) == nullptr);

    // Arbitrary releases become an intrusive LIFO free-list.
    void* first = ptrs[0];
    void* middle = ptrs[31];
    void* last = ptrs[63];
    tinyawait::detail::free_frame(first); ptrs[0] = nullptr;
    tinyawait::detail::free_frame(middle); ptrs[31] = nullptr;
    tinyawait::detail::free_frame(last); ptrs[63] = nullptr;
    assert(validate_free_list() == 3);

    void* r1 = tinyawait::detail::alloc_frame(1);
    void* r2 = tinyawait::detail::alloc_frame(1);
    void* r3 = tinyawait::detail::alloc_frame(1);
    assert(r1 == last && r2 == middle && r3 == first);
    ptrs[63] = r1; ptrs[31] = r2; ptrs[0] = r3;

    for (std::size_t i = 0; i < TINYAWAIT_MAX_TASKS; ++i) {
        tinyawait::detail::free_frame(ptrs[i]);
        ptrs[i] = nullptr;
    }
    assert(tinyawait::active_frames() == 0);
    assert(validate_free_list() == TINYAWAIT_MAX_TASKS);

    // Deterministic random allocation/release stress. Every live pointer must be unique.
    std::uint32_t rng = 0xC001D00DU;
    for (std::size_t op = 0; op < 300000; ++op) {
        rng = rng * 1664525U + 1013904223U;
        const auto index = static_cast<std::size_t>(rng % TINYAWAIT_MAX_TASKS);
        if (ptrs[index]) {
            tinyawait::detail::free_frame(ptrs[index]);
            ptrs[index] = nullptr;
        } else {
            void* p = tinyawait::detail::alloc_frame(1);
            assert(p);
            for (void* live : ptrs) if (live) assert(live != p);
            ptrs[index] = p;
        }
    }
    for (void*& p : ptrs) {
        if (p) {
            tinyawait::detail::free_frame(p);
            p = nullptr;
        }
    }
    assert(tinyawait::active_frames() == 0);
    assert(validate_free_list() == TINYAWAIT_MAX_TASKS);

    // Full capacity is still usable after stress.
    for (std::size_t i = 0; i < TINYAWAIT_MAX_TASKS; ++i) {
        ptrs[i] = tinyawait::detail::alloc_frame(1);
        assert(ptrs[i]);
        for (std::size_t j = 0; j < i; ++j) assert(ptrs[j] != ptrs[i]);
    }
    assert(tinyawait::detail::alloc_frame(1) == nullptr);
    for (void* p : ptrs) tinyawait::detail::free_frame(p);
    assert(tinyawait::active_frames() == 0);
    assert(validate_free_list() == TINYAWAIT_MAX_TASKS);
}
