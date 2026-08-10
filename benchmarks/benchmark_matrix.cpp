#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

static volatile std::uint32_t fake_now = 0;
static inline std::uint32_t bench_now() noexcept { return fake_now; }
#define TINYAWAIT_NOW_MS() bench_now()
#include "TinyAwait.h"

using clock_type = std::chrono::steady_clock;

Async sleeper(std::uint32_t delay = 0x70000000U) { co_await delay; }

static inline void escape(void* p) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(p) : "memory");
#else
    (void)p;
#endif
}

static std::size_t poll_iterations() {
    constexpr std::size_t n = TINYAWAIT_MAX_TASKS;
    if constexpr (n <= 16) return 5000000;
    if constexpr (n <= 64) return 3000000;
    if constexpr (n <= 256) return 1000000;
    return 250000;
}

static double bench_poll_empty() {
    const auto iters = poll_iterations();
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i < iters; ++i) tinyawait::poll();
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::nano>(end - begin).count() / static_cast<double>(iters);
}

static void fill_waiters() {
    for (std::size_t i = 0; i < TINYAWAIT_MAX_TASKS; ++i) sleeper();
}

static double bench_poll_wait_same_tick() {
    fill_waiters();
    const auto iters = poll_iterations();
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i < iters; ++i) tinyawait::poll();
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::nano>(end - begin).count() / static_cast<double>(iters);
}

static double bench_poll_wait_advancing() {
    fill_waiters();
    const auto iters = poll_iterations();
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i < iters; ++i) {
        fake_now = static_cast<std::uint32_t>(fake_now + 1U);
        tinyawait::poll();
    }
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::nano>(end - begin).count() / static_cast<double>(iters);
}

static double bench_expire_same_deadline() {
    constexpr std::size_t n = TINYAWAIT_MAX_TASKS;
    const std::size_t reps = n <= 8 ? 20000 : n <= 32 ? 10000 : n <= 128 ? 3000 : n <= 256 ? 1500 : 300;
    double total_ns = 0.0;
    for (std::size_t r = 0; r < reps; ++r) {
        for (std::size_t i = 0; i < n; ++i) sleeper(1);
        fake_now = static_cast<std::uint32_t>(fake_now + 1U);
        const auto begin = clock_type::now();
        tinyawait::poll();
        const auto end = clock_type::now();
        total_ns += std::chrono::duration<double, std::nano>(end - begin).count();
    }
    return total_ns / static_cast<double>(reps);
}

static double bench_alloc_free_one() {
    const std::size_t iters = 5000000;
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i < iters; ++i) {
        void* p = tinyawait::detail::alloc_frame(1);
        escape(p);
        tinyawait::detail::free_frame(p);
    }
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::nano>(end - begin).count() / static_cast<double>(iters);
}

static double bench_alloc_free_near_capacity() {
    void* held[TINYAWAIT_MAX_TASKS]{};
    for (std::size_t i = 0; i + 1 < TINYAWAIT_MAX_TASKS; ++i) {
        held[i] = tinyawait::detail::alloc_frame(1);
        escape(held[i]);
    }
    const std::size_t iters = TINYAWAIT_MAX_TASKS <= 64 ? 2000000 : TINYAWAIT_MAX_TASKS <= 256 ? 500000 : 100000;
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i < iters; ++i) {
        void* p = tinyawait::detail::alloc_frame(1);
        escape(p);
        tinyawait::detail::free_frame(p);
    }
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::nano>(end - begin).count() / static_cast<double>(iters);
}

static double bench_alloc_all_free_all() {
    void* ptrs[TINYAWAIT_MAX_TASKS]{};
    const std::size_t reps = TINYAWAIT_MAX_TASKS <= 16 ? 100000 : TINYAWAIT_MAX_TASKS <= 64 ? 30000 : TINYAWAIT_MAX_TASKS <= 256 ? 5000 : 500;
    const auto begin = clock_type::now();
    for (std::size_t r = 0; r < reps; ++r) {
        for (std::size_t i = 0; i < TINYAWAIT_MAX_TASKS; ++i) {
            ptrs[i] = tinyawait::detail::alloc_frame(1);
            escape(ptrs[i]);
        }
        for (std::size_t i = 0; i < TINYAWAIT_MAX_TASKS; ++i) tinyawait::detail::free_frame(ptrs[i]);
    }
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::nano>(end - begin).count() /
        static_cast<double>(reps * TINYAWAIT_MAX_TASKS * 2ULL);
}

static double bench_alloc_random() {
    void* ptrs[TINYAWAIT_MAX_TASKS]{};
    std::uint32_t rng = 0x12345678U;
    const std::size_t ops = TINYAWAIT_MAX_TASKS <= 64 ? 3000000 : TINYAWAIT_MAX_TASKS <= 256 ? 1500000 : 500000;
    const auto begin = clock_type::now();
    for (std::size_t op = 0; op < ops; ++op) {
        rng = rng * 1664525U + 1013904223U;
        const auto index = static_cast<std::size_t>(rng % TINYAWAIT_MAX_TASKS);
        if (ptrs[index]) {
            tinyawait::detail::free_frame(ptrs[index]);
            ptrs[index] = nullptr;
        } else {
            ptrs[index] = tinyawait::detail::alloc_frame(1);
            escape(ptrs[index]);
        }
    }
    const auto end = clock_type::now();
    for (void* p : ptrs) if (p) tinyawait::detail::free_frame(p);
    return std::chrono::duration<double, std::nano>(end - begin).count() / static_cast<double>(ops);
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string_view mode = argv[1];
    double value = 0.0;
    if (mode == "poll0") value = bench_poll_empty();
    else if (mode == "poll_wait_same") value = bench_poll_wait_same_tick();
    else if (mode == "poll_wait_adv") value = bench_poll_wait_advancing();
    else if (mode == "expire_same") value = bench_expire_same_deadline();
    else if (mode == "alloc_one") value = bench_alloc_free_one();
    else if (mode == "alloc_near") value = bench_alloc_free_near_capacity();
    else if (mode == "alloc_cycle") value = bench_alloc_all_free_all();
    else if (mode == "alloc_random") value = bench_alloc_random();
    else return 3;
    std::printf("%.6f\n", value);
}
