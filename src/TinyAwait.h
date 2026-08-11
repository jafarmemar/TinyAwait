#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#if __cplusplus < 202002L
#error "TinyAwait requires C++20 or newer"
#endif
#include <coroutine>

#ifndef TINYAWAIT_MAX_TASKS
#define TINYAWAIT_MAX_TASKS 32
#endif

// Kept for source/configuration compatibility. It is no longer a hard
// per-coroutine-frame limit; unless TINYAWAIT_FRAME_POOL_BYTES is defined,
// it contributes to the total frame-memory budget below.
#ifndef TINYAWAIT_FRAME_SIZE
#define TINYAWAIT_FRAME_SIZE 128
#endif

#ifndef TINYAWAIT_FRAME_POOL_BYTES
#define TINYAWAIT_FRAME_POOL_BYTES (TINYAWAIT_MAX_TASKS * TINYAWAIT_FRAME_SIZE)
#endif

#ifndef TINYAWAIT_NOW_MS
#if defined(ARDUINO)
#include <Arduino.h>
#define TINYAWAIT_NOW_MS() static_cast<std::uint32_t>(millis())
#else
#error "Define TINYAWAIT_NOW_MS() to return a monotonic uint32_t millisecond counter before including TinyAwait.h"
#endif
#endif

static_assert(TINYAWAIT_MAX_TASKS > 0, "TINYAWAIT_MAX_TASKS must be greater than zero");
static_assert(TINYAWAIT_FRAME_SIZE > 0, "TINYAWAIT_FRAME_SIZE must be greater than zero");
static_assert(TINYAWAIT_FRAME_POOL_BYTES > 0, "TINYAWAIT_FRAME_POOL_BYTES must be greater than zero");

namespace tinyawait {
namespace detail {

[[noreturn]] inline void default_failure() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_trap();
#else
    for (;;) {}
#endif
}

#ifndef TINYAWAIT_ON_ERROR
#define TINYAWAIT_ON_ERROR() ::tinyawait::detail::default_failure()
#endif

struct FreeFrameBlock {
    std::size_t size;
    FreeFrameBlock* next;
};

inline constexpr std::size_t frame_alignment = alignof(std::max_align_t);
inline constexpr std::size_t frame_min_block =
    ((sizeof(FreeFrameBlock) + frame_alignment - 1U) / frame_alignment) * frame_alignment;
inline constexpr std::size_t frame_pool_usable_bytes =
    (static_cast<std::size_t>(TINYAWAIT_FRAME_POOL_BYTES) / frame_alignment) * frame_alignment;

static_assert(
    frame_pool_usable_bytes >= frame_min_block,
    "TINYAWAIT_FRAME_POOL_BYTES is too small for TinyAwait frame allocator metadata");

alignas(std::max_align_t) inline std::byte frame_pool[TINYAWAIT_FRAME_POOL_BYTES]{};
using FrameIndex = std::conditional_t<
    (TINYAWAIT_MAX_TASKS <= 0xFFU), std::uint8_t,
    std::conditional_t<(TINYAWAIT_MAX_TASKS <= 0xFFFFU), std::uint16_t, std::size_t>>;
inline FreeFrameBlock* free_frame_head = nullptr;
inline std::size_t frame_bump_bytes = 0;
inline FrameIndex active_frame_count = 0;

#ifdef TINYAWAIT_TESTING
inline std::size_t last_frame_bytes = 0;
inline std::size_t max_frame_bytes = 0;
inline std::size_t frame_alloc_search_steps = 0;
inline std::size_t frame_alloc_search_max = 0;
#endif

inline constexpr std::size_t normalize_frame_size(std::size_t size) noexcept {
    const auto at_least_metadata = size < frame_min_block ? frame_min_block : size;
    return ((at_least_metadata + frame_alignment - 1U) / frame_alignment) * frame_alignment;
}

inline void reclaim_free_bump_tail() noexcept {
    for (;;) {
        FreeFrameBlock* previous = nullptr;
        auto* block = free_frame_head;
        while (block &&
               static_cast<std::size_t>(reinterpret_cast<std::byte*>(block) - frame_pool) +
                       block->size <
                   frame_bump_bytes) {
            previous = block;
            block = block->next;
        }
        if (!block) return;

        const auto offset =
            static_cast<std::size_t>(reinterpret_cast<std::byte*>(block) - frame_pool);
        if (offset + block->size != frame_bump_bytes) return;

        frame_bump_bytes = offset;
        if (previous) previous->next = block->next;
        else free_frame_head = block->next;
    }
}

inline void* alloc_frame(std::size_t size) noexcept {
#ifdef TINYAWAIT_TESTING
    last_frame_bytes = size;
    if (size > max_frame_bytes) max_frame_bytes = size;
    std::size_t search_steps = 0;
#endif

    if (size > frame_pool_usable_bytes ||
        static_cast<std::size_t>(active_frame_count) >= TINYAWAIT_MAX_TASKS) {
        return nullptr;
    }

    const auto needed = normalize_frame_size(size);

    // Hot path: no released holes exist, so extend the compact bump frontier.
    if (!free_frame_head) {
        if (needed > frame_pool_usable_bytes - frame_bump_bytes) return nullptr;
        void* frame = frame_pool + frame_bump_bytes;
        frame_bump_bytes += needed;
        ++active_frame_count;
        return frame;
    }

    FreeFrameBlock* previous = nullptr;
    auto* block = free_frame_head;
    while (block && block->size < needed) {
#ifdef TINYAWAIT_TESTING
        ++search_steps;
#endif
        previous = block;
        block = block->next;
    }

    if (block) {
#ifdef TINYAWAIT_TESTING
        ++search_steps;
        frame_alloc_search_steps += search_steps;
        if (search_steps > frame_alloc_search_max) frame_alloc_search_max = search_steps;
#endif

        const auto remaining = block->size - needed;
        if (remaining != 0U) {
            auto* split_address = reinterpret_cast<std::byte*>(block) + needed;
            auto* split = ::new (static_cast<void*>(split_address))
                FreeFrameBlock{remaining, block->next};
            if (previous) previous->next = split;
            else free_frame_head = split;
        } else {
            if (previous) previous->next = block->next;
            else free_frame_head = block->next;
        }

        ++active_frame_count;
        return block;
    }

#ifdef TINYAWAIT_TESTING
    frame_alloc_search_steps += search_steps;
    if (search_steps > frame_alloc_search_max) frame_alloc_search_max = search_steps;
#endif

    if (needed > frame_pool_usable_bytes - frame_bump_bytes) {
        reclaim_free_bump_tail();
        if (needed > frame_pool_usable_bytes - frame_bump_bytes) return nullptr;
    }

    void* frame = frame_pool + frame_bump_bytes;
    frame_bump_bytes += needed;
    ++active_frame_count;
    return frame;
}

inline void free_frame(void* ptr, std::size_t size) noexcept {
    if (!ptr) return;

    const auto released = normalize_frame_size(size);
    auto* address = static_cast<std::byte*>(ptr);
    const auto offset = static_cast<std::size_t>(address - frame_pool);

    // Common LIFO release: retreat the bump frontier immediately. Any lower
    // adjacent free span is merged lazily only if a later allocation needs it.
    if (offset + released == frame_bump_bytes) {
        frame_bump_bytes = offset;
        --active_frame_count;
        return;
    }

    FreeFrameBlock* previous = nullptr;
    auto* next = free_frame_head;
    while (next && reinterpret_cast<std::byte*>(next) < address) {
        previous = next;
        next = next->next;
    }

    auto* block = ::new (ptr) FreeFrameBlock{released, next};
    if (previous) previous->next = block;
    else free_frame_head = block;

    if (next && address + block->size == reinterpret_cast<std::byte*>(next)) {
        block->size += next->size;
        block->next = next->next;
    }
    if (previous &&
        reinterpret_cast<std::byte*>(previous) + previous->size == address) {
        previous->size += block->size;
        previous->next = block->next;
    }

    --active_frame_count;
}

inline constexpr std::uint8_t invalid_deadline_epoch = 0xFFU;
inline constexpr std::uint8_t epoch_mask = 0x03U;

struct TimerSlot {
    std::uint32_t deadline{};
    std::uint8_t epoch{};
    std::coroutine_handle<> handle{};
};
inline TimerSlot timers[TINYAWAIT_MAX_TASKS]{};
inline std::size_t active_timer_count = 0;
inline std::uint32_t clock_last = 0;
inline std::uint8_t clock_epoch = 0;
inline std::uint32_t next_deadline = 0;
inline std::uint8_t next_deadline_epoch = invalid_deadline_epoch;

inline void update_clock(std::uint32_t now) noexcept {
    if (now < clock_last) {
        clock_epoch = static_cast<std::uint8_t>((clock_epoch + 1U) & epoch_mask);
    }
    clock_last = now;
}

inline bool deadline_due(std::uint32_t deadline, std::uint8_t epoch, std::uint32_t now) noexcept {
    const auto epoch_distance = static_cast<std::uint8_t>((epoch - clock_epoch) & epoch_mask);
    if (epoch_distance == 0U) return now >= deadline;
    if (epoch_distance == 1U) return false;
    return true;
}

inline bool deadline_before(
    std::uint32_t lhs_deadline, std::uint8_t lhs_epoch,
    std::uint32_t rhs_deadline, std::uint8_t rhs_epoch) noexcept {
    const auto lhs_distance = static_cast<std::uint8_t>((lhs_epoch - clock_epoch) & epoch_mask);
    const auto rhs_distance = static_cast<std::uint8_t>((rhs_epoch - clock_epoch) & epoch_mask);
    if (lhs_distance != rhs_distance) return lhs_distance < rhs_distance;
    return lhs_deadline < rhs_deadline;
}

inline void consider_next_deadline(std::uint32_t deadline, std::uint8_t epoch) noexcept {
    if (next_deadline_epoch == invalid_deadline_epoch ||
        deadline_before(deadline, epoch, next_deadline, next_deadline_epoch)) {
        next_deadline = deadline;
        next_deadline_epoch = epoch;
    }
}

inline bool add_timer(std::uint32_t delay, std::coroutine_handle<> handle) noexcept {
    const auto now = static_cast<std::uint32_t>(TINYAWAIT_NOW_MS());
    if (active_timer_count == 0) {
        clock_last = now;
        clock_epoch = 0;
        next_deadline_epoch = invalid_deadline_epoch;
    } else {
        update_clock(now);
    }

    const auto deadline = static_cast<std::uint32_t>(now + delay);
    const auto deadline_epoch = static_cast<std::uint8_t>(
        (clock_epoch + (deadline < now ? 1U : 0U)) & epoch_mask);

    for (auto& timer : timers) {
        if (!timer.handle) {
            timer = {deadline, deadline_epoch, handle};
            if (next_deadline_epoch == invalid_deadline_epoch ||
                (!deadline_due(next_deadline, next_deadline_epoch, now) &&
                 deadline_before(deadline, deadline_epoch, next_deadline, next_deadline_epoch))) {
                next_deadline = deadline;
                next_deadline_epoch = deadline_epoch;
            }
            ++active_timer_count;
            return true;
        }
    }
    return false;
}

struct DelayAwaiter {
    std::uint32_t delay;
    bool await_ready() const noexcept { return delay == 0; }
    bool await_suspend(std::coroutine_handle<> handle) const noexcept {
        if (add_timer(delay, handle)) return true;
        TINYAWAIT_ON_ERROR();
        return false;
    }
    void await_resume() const noexcept {}
};

} // namespace detail

inline constexpr std::uint32_t max_delay_ms = ~std::uint32_t{0};
inline constexpr std::size_t frame_pool_bytes = detail::frame_pool_usable_bytes;

struct Async {
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    Async() noexcept = default;
    Async(const Async&) = delete;
    Async& operator=(const Async&) = delete;
    Async(Async&& other) noexcept : handle_(other.handle_) { other.handle_ = {}; }
    Async& operator=(Async&&) = delete;
    ~Async() { detach(); }

    struct Awaiter {
        handle_type child{};
        bool await_ready() const noexcept { return !child || child.done(); }
        void await_suspend(std::coroutine_handle<> parent) const noexcept {
            child.promise().continuation = parent;
        }
        void await_resume() const noexcept {
            if (child) child.destroy();
        }
    };

    Awaiter operator co_await() && noexcept {
        const auto child = handle_;
        handle_ = {};
        return {child};
    }

    struct promise_type {
        std::coroutine_handle<> continuation{};
        bool detached = false;

        static void* operator new(std::size_t size) noexcept { return detail::alloc_frame(size); }
        static void operator delete(void* ptr, std::size_t size) noexcept {
            detail::free_frame(ptr, size);
        }
        static Async get_return_object_on_allocation_failure() noexcept {
            TINYAWAIT_ON_ERROR();
            return {};
        }
        Async get_return_object() noexcept { return Async{handle_type::from_promise(*this)}; }
        std::suspend_never initial_suspend() const noexcept { return {}; }

        struct FinalAwaiter {
            bool detached;
            bool await_ready() const noexcept { return detached; }
            std::coroutine_handle<> await_suspend(handle_type handle) const noexcept {
                const auto continuation = handle.promise().continuation;
                return continuation ? continuation : std::noop_coroutine();
            }
            void await_resume() const noexcept {}
        };

        FinalAwaiter final_suspend() const noexcept { return {detached}; }
        void return_void() const noexcept {}
        void unhandled_exception() const noexcept { TINYAWAIT_ON_ERROR(); }

        template <typename T>
            requires (std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
        detail::DelayAwaiter await_transform(T ms) const noexcept {
            if constexpr (std::is_signed_v<T>) {
                if (ms < 0) {
                    TINYAWAIT_ON_ERROR();
                    return {0};
                }
            }
            using unsigned_type = std::make_unsigned_t<T>;
            if constexpr (sizeof(unsigned_type) > sizeof(std::uint32_t)) {
                if (static_cast<unsigned_type>(ms) > static_cast<unsigned_type>(max_delay_ms)) {
                    TINYAWAIT_ON_ERROR();
                    return {0};
                }
            }
            return {static_cast<std::uint32_t>(ms)};
        }

        Async&& await_transform(Async&& child) const noexcept {
            return static_cast<Async&&>(child);
        }
    };

private:
    explicit Async(handle_type handle) noexcept : handle_(handle) {}

    void detach() noexcept {
        const auto handle = handle_;
        handle_ = {};
        if (!handle) return;
        if (handle.done()) handle.destroy();
        else handle.promise().detached = true;
    }

    handle_type handle_{};
};

namespace detail {

inline void poll_due(std::uint32_t now) noexcept {
restart_scan:
    const auto scan_epoch = clock_epoch;
    next_deadline_epoch = invalid_deadline_epoch;
    for (auto& timer : timers) {
        if (!timer.handle) continue;
        if (deadline_due(timer.deadline, timer.epoch, now)) {
            const auto handle = timer.handle;
            timer.handle = {};
            --active_timer_count;
            handle.resume();

            // A resumed coroutine may register another delay. If that clock read
            // crosses the 32-bit wrap, add_timer() advances clock_epoch while
            // this scan still holds the pre-wrap `now`. Restart before examining
            // another timer so old `now` is never paired with a new epoch.
            if (clock_epoch != scan_epoch) [[unlikely]] {
                now = clock_last;
                goto restart_scan;
            }
        } else {
            consider_next_deadline(timer.deadline, timer.epoch);
        }
    }
}

} // namespace detail

inline void poll() noexcept {
    if (detail::active_timer_count == 0) return;
    const auto now = static_cast<std::uint32_t>(TINYAWAIT_NOW_MS());
    detail::update_clock(now);
    if (!detail::deadline_due(detail::next_deadline, detail::next_deadline_epoch, now)) return;
    detail::poll_due(now);
}

#ifdef TINYAWAIT_TESTING
inline std::size_t last_frame_size() noexcept { return detail::last_frame_bytes; }
inline std::size_t max_frame_size() noexcept { return detail::max_frame_bytes; }
inline std::size_t active_frames() noexcept {
    return static_cast<std::size_t>(detail::active_frame_count);
}
inline std::size_t active_timers() noexcept {
    std::size_t count = 0;
    for (const auto& timer : detail::timers) count += timer.handle ? 1U : 0U;
    return count;
}
inline std::size_t frame_allocator_max_search() noexcept {
    return detail::frame_alloc_search_max;
}
#endif

} // namespace tinyawait

using Async = tinyawait::Async;
