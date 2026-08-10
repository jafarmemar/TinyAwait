#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#if __cplusplus < 202002L
#error "TinyAwait requires C++20 or newer"
#endif
#include <coroutine>

#ifndef TINYAWAIT_MAX_TASKS
#define TINYAWAIT_MAX_TASKS 32
#endif

#ifndef TINYAWAIT_FRAME_SIZE
#define TINYAWAIT_FRAME_SIZE 128
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

inline constexpr std::size_t frame_storage_size =
    TINYAWAIT_FRAME_SIZE < sizeof(void*) ? sizeof(void*) : TINYAWAIT_FRAME_SIZE;
struct FrameSlot { alignas(std::max_align_t) std::byte bytes[frame_storage_size]; };
inline FrameSlot frame_pool[TINYAWAIT_MAX_TASKS]{};
using FrameIndex = std::conditional_t<
    (TINYAWAIT_MAX_TASKS <= 0xFFU), std::uint8_t,
    std::conditional_t<(TINYAWAIT_MAX_TASKS <= 0xFFFFU), std::uint16_t, std::size_t>>;
inline void* free_frame_head = nullptr;
inline FrameIndex next_unused_frame = 0;
#ifdef TINYAWAIT_TESTING
inline std::size_t last_frame_bytes = 0;
inline std::size_t max_frame_bytes = 0;
inline std::size_t active_frame_count = 0;
#endif

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

inline void* alloc_frame(std::size_t size) noexcept {
#ifdef TINYAWAIT_TESTING
    last_frame_bytes = size;
    if (size > max_frame_bytes) max_frame_bytes = size;
#endif
    if (size > TINYAWAIT_FRAME_SIZE) return nullptr;

    void* frame = free_frame_head;
    if (frame) {
        std::memcpy(&free_frame_head, frame, sizeof(free_frame_head));
    } else {
        if (static_cast<std::size_t>(next_unused_frame) >= TINYAWAIT_MAX_TASKS) return nullptr;
        frame = frame_pool[next_unused_frame].bytes;
        ++next_unused_frame;
    }
#ifdef TINYAWAIT_TESTING
    ++active_frame_count;
#endif
    return frame;
}

inline void free_frame(void* ptr) noexcept {
    std::memcpy(ptr, &free_frame_head, sizeof(free_frame_head));
    free_frame_head = ptr;
#ifdef TINYAWAIT_TESTING
    --active_frame_count;
#endif
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
        static void operator delete(void* ptr) noexcept { detail::free_frame(ptr); }
        static void operator delete(void* ptr, std::size_t) noexcept { detail::free_frame(ptr); }
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
        Async&& await_transform(Async&& child) const noexcept { return static_cast<Async&&>(child); }
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

inline void poll() noexcept {
    if (detail::active_timer_count == 0) return;
    const auto now = static_cast<std::uint32_t>(TINYAWAIT_NOW_MS());
    detail::update_clock(now);
    if (!detail::deadline_due(detail::next_deadline, detail::next_deadline_epoch, now)) return;

    detail::next_deadline_epoch = detail::invalid_deadline_epoch;
    for (auto& timer : detail::timers) {
        if (!timer.handle) continue;
        if (detail::deadline_due(timer.deadline, timer.epoch, now)) {
            const auto handle = timer.handle;
            timer.handle = {};
            --detail::active_timer_count;
            handle.resume();
        } else {
            detail::consider_next_deadline(timer.deadline, timer.epoch);
        }
    }
}

#ifdef TINYAWAIT_TESTING
inline std::size_t last_frame_size() noexcept { return detail::last_frame_bytes; }
inline std::size_t max_frame_size() noexcept { return detail::max_frame_bytes; }
inline std::size_t active_frames() noexcept { return detail::active_frame_count; }
inline std::size_t active_timers() noexcept {
    std::size_t count = 0;
    for (const auto& timer : detail::timers) count += timer.handle ? 1U : 0U;
    return count;
}
#endif

} // namespace tinyawait

using Async = tinyawait::Async;
