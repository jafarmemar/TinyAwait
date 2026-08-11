#include <cassert>
#include <cstddef>
#include <cstdint>

static std::uint32_t fake_now = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_MAX_TASKS 1
#define TINYAWAIT_FRAME_SIZE 1
#define TINYAWAIT_NOW_MS() fake_now
#include "TinyAwait.h"

int main() {
    static_assert(tinyawait::frame_pool_bytes >= alignof(std::max_align_t));

    void* frame = tinyawait::detail::alloc_frame(1);
    assert(frame != nullptr);
    tinyawait::detail::free_frame(frame, 1);
    assert(tinyawait::active_frames() == 0U);
    return 0;
}
