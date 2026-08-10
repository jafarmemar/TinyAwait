#include <chrono>
#include <cstdint>
#include <cstdio>

static std::uint32_t fake_now = 0;
#define TINYAWAIT_TESTING
#define TINYAWAIT_NOW_MS() fake_now
#include "TinyAwait.h"

Async sleeper() { co_await 1000; }
Async child() { co_await 1000; }
Async parent() { co_await child(); }

static double bench_poll(std::size_t iterations) {
    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iterations; ++i) tinyawait::poll();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(end - begin).count() / static_cast<double>(iterations);
}

int main() {
    constexpr std::size_t iterations = 5000000;
    const auto zero = bench_poll(iterations);
    sleeper();
    const auto sample_frame = tinyawait::last_frame_size();
    const auto one = bench_poll(iterations);
    for (int i = 1; i < 32; ++i) sleeper();
    const auto thirty_two = bench_poll(iterations);

    const auto start = std::chrono::steady_clock::now();
    fake_now = 1000;
    tinyawait::poll();
    const auto stop = std::chrono::steady_clock::now();
    const auto resume = std::chrono::duration<double, std::nano>(stop - start).count() / 32.0;

    fake_now = 2000;
    parent();
    const auto nested_parent_frame = tinyawait::max_frame_size();
    fake_now = 3000;
    tinyawait::poll();

    std::printf("poll_ns_0=%.3f\n", zero);
    std::printf("poll_ns_1=%.3f\n", one);
    std::printf("poll_ns_32=%.3f\n", thirty_two);
    std::printf("resume_ns_approx=%.3f\n", resume);
    std::printf("sample_coroutine_frame_bytes=%zu\n", sample_frame);
    std::printf("max_frame_bytes_after_nested=%zu\n", nested_parent_frame);
    std::printf("timer_slot_bytes=%zu\n", sizeof(tinyawait::detail::TimerSlot));
    std::printf("frame_slot_bytes=%zu\n", sizeof(tinyawait::detail::FrameSlot));
    return 0;
}
