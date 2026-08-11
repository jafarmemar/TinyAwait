#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

static std::uint32_t fake_now = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_MAX_TASKS 32
#define TINYAWAIT_FRAME_POOL_BYTES 4096
#define TINYAWAIT_NOW_MS() fake_now
#include "TinyAwait.h"

struct LiveFrame {
    void* pointer{};
    std::size_t requested{};
    std::size_t span{};
};

static std::uint32_t rng_state = 0xC001D00DU;
static std::uint32_t next_random() noexcept {
    rng_state = rng_state * 1664525U + 1013904223U;
    return rng_state;
}

static void validate_live_frames(const std::array<LiveFrame, TINYAWAIT_MAX_TASKS>& live) {
    const auto base = reinterpret_cast<std::uintptr_t>(tinyawait::detail::frame_pool);
    const auto end = base + tinyawait::detail::frame_pool_usable_bytes;
    std::size_t count = 0;

    for (std::size_t i = 0; i < live.size(); ++i) {
        if (!live[i].pointer) continue;
        ++count;

        const auto begin_i = reinterpret_cast<std::uintptr_t>(live[i].pointer);
        const auto end_i = begin_i + live[i].span;
        assert(begin_i % alignof(std::max_align_t) == 0);
        assert(begin_i >= base && end_i <= end);

        for (std::size_t j = i + 1; j < live.size(); ++j) {
            if (!live[j].pointer) continue;
            const auto begin_j = reinterpret_cast<std::uintptr_t>(live[j].pointer);
            const auto end_j = begin_j + live[j].span;
            assert(end_i <= begin_j || end_j <= begin_i);
        }
    }

    assert(tinyawait::active_frames() == count);
}

int main() {
    using namespace tinyawait::detail;

    static_assert(tinyawait::frame_pool_bytes == 4096U);
    assert(tinyawait::active_frames() == 0);

    // Variable sizes, including frames larger than the old 128-byte slot.
    constexpr std::size_t sizes[] = {1, 17, 33, 65, 127, 129, 257, 511};
    void* pointers[std::size(sizes)]{};
    for (std::size_t i = 0; i < std::size(sizes); ++i) {
        pointers[i] = alloc_frame(sizes[i]);
        assert(pointers[i]);
        assert(reinterpret_cast<std::uintptr_t>(pointers[i]) % alignof(std::max_align_t) == 0);
    }
    for (std::size_t i = std::size(sizes); i-- > 0;) {
        free_frame(pointers[i], sizes[i]);
    }
    assert(tinyawait::active_frames() == 0);

    // A nearly pool-sized frame must fit after ordinary LIFO reuse.
    void* huge = alloc_frame(frame_pool_usable_bytes - 32U);
    assert(huge);
    free_frame(huge, frame_pool_usable_bytes - 32U);
    assert(tinyawait::active_frames() == 0);

    // One million deterministic variable-size allocate/free operations. Validate
    // address range, alignment, overlap, and active-frame accounting throughout.
    std::array<LiveFrame, TINYAWAIT_MAX_TASKS> live{};
    for (std::size_t operation = 0; operation < 1000000; ++operation) {
        const auto index = static_cast<std::size_t>(next_random() % live.size());
        auto& entry = live[index];

        if (entry.pointer) {
            free_frame(entry.pointer, entry.requested);
            entry = {};
        } else {
            const auto request = static_cast<std::size_t>(1U + (next_random() % 480U));
            if (void* pointer = alloc_frame(request)) {
                entry = {pointer, request, normalize_frame_size(request)};
            }
        }

        if ((operation & 1023U) == 0U) validate_live_frames(live);
    }

    for (auto& entry : live) {
        if (!entry.pointer) continue;
        free_frame(entry.pointer, entry.requested);
        entry = {};
    }
    validate_live_frames(live);

    // Deliberately create holes and release them in a non-LIFO order. Coalescing
    // plus lazy bump-tail reclaim must recover the entire arena afterwards.
    void* a = alloc_frame(80);
    void* b = alloc_frame(144);
    void* c = alloc_frame(208);
    void* d = alloc_frame(96);
    assert(a && b && c && d);

    free_frame(b, 144);
    free_frame(d, 96);
    free_frame(a, 80);
    free_frame(c, 208);
    assert(tinyawait::active_frames() == 0);

    huge = alloc_frame(frame_pool_usable_bytes);
    assert(huge);
    free_frame(huge, frame_pool_usable_bytes);
    assert(tinyawait::active_frames() == 0);

    // Allocation must still be bounded by the configured live-frame count even
    // when there is sufficient byte budget.
    void* capped[TINYAWAIT_MAX_TASKS]{};
    for (std::size_t i = 0; i < TINYAWAIT_MAX_TASKS; ++i) {
        capped[i] = alloc_frame(32);
        assert(capped[i]);
    }
    assert(alloc_frame(32) == nullptr);
    for (void* pointer : capped) free_frame(pointer, 32);
    assert(tinyawait::active_frames() == 0);

    return 0;
}
