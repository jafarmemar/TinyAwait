#pragma once

#include <cstddef>
#include <cstdint>

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

struct FrameSlot { alignas(std::max_align_t) std::byte bytes[TINYAWAIT_FRAME_SIZE]; };
inline FrameSlot frame_pool[TINYAWAIT_MAX_TASKS]{};
inline bool frame_used[TINYAWAIT_MAX_TASKS]{};
#ifdef TINYAWAIT_TESTING
inline std::size_t last_frame_bytes = 0;
inline std::size_t max_frame_bytes = 0;
#endif

struct TimerSlot {
    std::uint32_t last{};
    std::uint32_t remaining{};
    std::coroutine_handle<> handle{};
};
inline TimerSlot timers[TINYAWAIT_MAX_TASKS]{};

inline void* alloc_frame(std::size_t size) noexcept {
#ifdef TINYAWAIT_TESTING
    last_frame_bytes = size;
    if (size > max_frame_bytes) max_frame_bytes = size;
#endif
    if (size > TINYAWAIT_FRAME_SIZE) return nullptr;
    for (std::size_t i = 0; i < TINYAWAIT_MAX_TASKS; ++i) {
        if (!frame_used[i]) {
            frame_used[i] = true;
            return frame_pool[i].bytes;
        }
    }
    return nullptr;
}

inline void free_frame(void* ptr) noexcept {
    for (std::size_t i = 0; i < TINYAWAIT_MAX_TASKS; ++i) {
        if (static_cast<void*>(frame_pool[i].bytes) == ptr) {
            frame_used[i] = false;
            return;
        }
    }
}

inline bool add_timer(std::uint32_t delay, std::coroutine_handle<> handle) noexcept {
    for (auto& timer : timers) {
        if (!timer.handle) {
            timer = {static_cast<std::uint32_t>(TINYAWAIT_NOW_MS()), delay, handle};
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
        detail::DelayAwaiter await_transform(std::uint32_t ms) const noexcept { return {ms}; }
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
    const auto now = static_cast<std::uint32_t>(TINYAWAIT_NOW_MS());
    for (auto& timer : detail::timers) {
        if (!timer.handle) continue;
        const auto elapsed = static_cast<std::uint32_t>(now - timer.last);
        if (elapsed >= timer.remaining) {
            const auto handle = timer.handle;
            timer.handle = {};
            handle.resume();
        } else {
            timer.last = now;
            timer.remaining -= elapsed;
        }
    }
}

#ifdef TINYAWAIT_TESTING
inline std::size_t last_frame_size() noexcept { return detail::last_frame_bytes; }
inline std::size_t max_frame_size() noexcept { return detail::max_frame_bytes; }
inline std::size_t active_frames() noexcept {
    std::size_t count = 0;
    for (bool used : detail::frame_used) count += used ? 1U : 0U;
    return count;
}
inline std::size_t active_timers() noexcept {
    std::size_t count = 0;
    for (const auto& timer : detail::timers) count += timer.handle ? 1U : 0U;
    return count;
}
#endif

} // namespace tinyawait

using Async = tinyawait::Async;
