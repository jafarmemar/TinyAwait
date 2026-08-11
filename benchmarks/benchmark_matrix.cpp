#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string_view>

static volatile std::uint32_t fake_now = 0;
static inline std::uint32_t bench_now() noexcept { return fake_now; }
#define TINYAWAIT_NOW_MS() bench_now()
#include "TinyAwait.h"

using clock_type = std::chrono::steady_clock;

Async sleeper(std::uint32_t delay = 0x70000000U) { co_await delay; }

static inline void escape(void* pointer) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(pointer) : "memory");
#else
    (void)pointer;
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
    const auto iterations = poll_iterations();
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i < iterations; ++i) tinyawait::poll();
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::nano>(end - begin).count() /
        static_cast<double>(iterations);
}

static void fill_waiters() {
    for (std::size_t i = 0; i < TINYAWAIT_MAX_TASKS; ++i) sleeper();
}

static double bench_poll_wait_same_tick() {
    fill_waiters();
    const auto iterations = poll_iterations();
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i < iterations; ++i) tinyawait::poll();
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::nano>(end - begin).count() /
        static_cast<double>(iterations);
}

static double bench_poll_wait_advancing() {
    fill_waiters();
    const auto iterations = poll_iterations();
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        fake_now = static_cast<std::uint32_t>(fake_now + 1U);
        tinyawait::poll();
    }
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::nano>(end - begin).count() /
        static_cast<double>(iterations);
}

static double bench_expire_same_deadline() {
    constexpr std::size_t n = TINYAWAIT_MAX_TASKS;
    const std::size_t repetitions =
        n <= 8 ? 20000 : n <= 32 ? 10000 : n <= 128 ? 3000 : n <= 256 ? 1500 : 300;
    double total_ns = 0.0;

    for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
        for (std::size_t i = 0; i < n; ++i) sleeper(1);
        fake_now = static_cast<std::uint32_t>(fake_now + 1U);
        const auto begin = clock_type::now();
        tinyawait::poll();
        const auto end = clock_type::now();
        total_ns += std::chrono::duration<double, std::nano>(end - begin).count();
    }
    return total_ns / static_cast<double>(repetitions);
}

static double bench_alloc_free_one() {
    constexpr std::size_t request = 56;
    const std::size_t iterations = 5000000;
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        void* pointer = tinyawait::detail::alloc_frame(request);
        escape(pointer);
        tinyawait::detail::free_frame(pointer, request);
    }
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::nano>(end - begin).count() /
        static_cast<double>(iterations);
}

static double bench_alloc_free_near_capacity() {
    constexpr std::size_t request = 56;
    void* held[TINYAWAIT_MAX_TASKS]{};
    for (std::size_t i = 0; i + 1 < TINYAWAIT_MAX_TASKS; ++i) {
        held[i] = tinyawait::detail::alloc_frame(request);
        escape(held[i]);
    }

    const std::size_t iterations =
        TINYAWAIT_MAX_TASKS <= 64 ? 2000000 : TINYAWAIT_MAX_TASKS <= 256 ? 500000 : 100000;
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        void* pointer = tinyawait::detail::alloc_frame(request);
        escape(pointer);
        tinyawait::detail::free_frame(pointer, request);
    }
    const auto end = clock_type::now();

    for (std::size_t i = 0; i + 1 < TINYAWAIT_MAX_TASKS; ++i) {
        tinyawait::detail::free_frame(held[i], request);
    }

    return std::chrono::duration<double, std::nano>(end - begin).count() /
        static_cast<double>(iterations);
}

static double bench_alloc_mixed_lifo() {
    constexpr std::size_t pattern[] = {48, 64, 80, 96, 112, 128, 144, 160};
    void* pointers[TINYAWAIT_MAX_TASKS]{};
    std::size_t sizes[TINYAWAIT_MAX_TASKS]{};

    const std::size_t repetitions = TINYAWAIT_MAX_TASKS <= 32 ? 100000 : 30000;
    const auto begin = clock_type::now();
    for (std::size_t repetition = 0; repetition < repetitions; ++repetition) {
        for (std::size_t i = 0; i < TINYAWAIT_MAX_TASKS; ++i) {
            sizes[i] = pattern[i % (sizeof(pattern) / sizeof(pattern[0]))];
            pointers[i] = tinyawait::detail::alloc_frame(sizes[i]);
            if (!pointers[i]) return -1.0;
            escape(pointers[i]);
        }
        for (std::size_t i = TINYAWAIT_MAX_TASKS; i-- > 0;) {
            tinyawait::detail::free_frame(pointers[i], sizes[i]);
        }
    }
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::nano>(end - begin).count() /
        static_cast<double>(repetitions * TINYAWAIT_MAX_TASKS * 2ULL);
}

static double bench_coroutine_lifecycle() {
    const std::size_t iterations = 2000000;
    const auto begin = clock_type::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        sleeper(1);
        fake_now = static_cast<std::uint32_t>(fake_now + 1U);
        tinyawait::poll();
    }
    const auto end = clock_type::now();
    return std::chrono::duration<double, std::nano>(end - begin).count() /
        static_cast<double>(iterations);
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
    else if (mode == "alloc_mixed") value = bench_alloc_mixed_lifo();
    else if (mode == "lifecycle") value = bench_coroutine_lifecycle();
    else return 3;

    std::printf("%.6f\n", value);
}
