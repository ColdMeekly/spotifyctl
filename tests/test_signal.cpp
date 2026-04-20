#include <catch2/catch_test_macros.hpp>

#include "spotify/events.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using spotify::Signal;

TEST_CASE("connect + emit invokes all slots with arguments", "[signal]") {
    Signal<void(int, int)> sig;
    int sumA = 0, sumB = 0;
    sig.connect([&](int a, int b) { sumA += a; sumB += b; });
    sig.connect([&](int a, int b) { sumA += a; sumB += b; });

    sig(3, 4);
    CHECK(sumA == 6);
    CHECK(sumB == 8);
}

TEST_CASE("disconnect removes exactly one slot", "[signal]") {
    Signal<void()> sig;
    int hits = 0;
    auto t1 = sig.connect([&] { ++hits; });
    sig.connect([&] { ++hits; });

    CHECK(sig.size() == 2);
    sig.disconnect(t1);
    CHECK(sig.size() == 1);

    sig();
    CHECK(hits == 1);
}

TEST_CASE("clear() drops all slots", "[signal]") {
    Signal<void()> sig;
    int hits = 0;
    sig.connect([&] { ++hits; });
    sig.connect([&] { ++hits; });
    sig.clear();
    CHECK(sig.size() == 0);
    sig();
    CHECK(hits == 0);
}

TEST_CASE("disconnect of unknown token is a no-op", "[signal]") {
    Signal<void()> sig;
    int hits = 0;
    sig.connect([&] { ++hits; });
    sig.disconnect(999);  // never issued
    sig();
    CHECK(hits == 1);
}

TEST_CASE("slots are invoked on a stable snapshot", "[signal]") {
    // If a slot disconnects itself mid-emit, the other slots in the current
    // emission should still run (because we iterate a copy).
    Signal<void()> sig;
    int hits = 0;
    Signal<void()>::Token t{};
    t = sig.connect([&] { sig.disconnect(t); ++hits; });
    sig.connect([&] { ++hits; });

    sig();
    CHECK(hits == 2);
    CHECK(sig.size() == 1);
}

TEST_CASE("ConnectAndReplay invokes slot once synchronously", "[signal]") {
    Signal<void(int)> sig;
    std::vector<int> seen;
    sig.ConnectAndReplay([&](int v) { seen.push_back(v); }, 42);

    CHECK(seen == std::vector<int>{42});
    CHECK(sig.size() == 1);

    sig(7);
    CHECK(seen == std::vector<int>{42, 7});
}

TEST_CASE("ConnectAndReplay is atomic vs concurrent emit", "[signal][thread]") {
    // If a producer thread is emitting while we call ConnectAndReplay, the
    // slot must either see the replay value first and then live emits, or
    // be connected too late to see them — but never miss the replay or see
    // emits before the replay value.
    Signal<void(int)> sig;

    std::atomic<bool> go{false};
    std::atomic<bool> stop{false};
    std::thread producer([&] {
        while (!go.load()) { std::this_thread::yield(); }
        int v = 1;
        while (!stop.load()) sig(v++);
    });

    go.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    std::vector<int> seen;
    std::mutex seenMu;
    sig.ConnectAndReplay(
        [&](int v) {
            std::lock_guard lk(seenMu);
            seen.push_back(v);
        },
        -1);

    // Let the producer spin for a bit more, then stop.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    stop.store(true);
    producer.join();

    std::lock_guard lk(seenMu);
    REQUIRE_FALSE(seen.empty());
    CHECK(seen.front() == -1);
    // Post-replay, everything else must be strictly positive (live emits).
    for (std::size_t i = 1; i < seen.size(); ++i) CHECK(seen[i] > 0);
}

TEST_CASE("concurrent connect and emit is safe", "[signal][thread]") {
    Signal<void()> sig;
    std::atomic<int> hits{0};
    sig.connect([&] { ++hits; });

    constexpr int kProducers = 4;
    constexpr int kPerThread = 500;

    std::vector<std::thread> workers;
    workers.reserve(kProducers + 1);

    std::atomic<bool> stop{false};
    workers.emplace_back([&] {
        while (!stop.load()) sig();
    });

    for (int i = 0; i < kProducers; ++i) {
        workers.emplace_back([&] {
            for (int j = 0; j < kPerThread; ++j) {
                auto tok = sig.connect([&] { ++hits; });
                sig.disconnect(tok);
            }
        });
    }

    for (std::size_t i = 1; i < workers.size(); ++i) workers[i].join();
    stop.store(true);
    workers[0].join();

    // The "always connected" slot fired at least once, and nothing crashed.
    CHECK(hits.load() > 0);
}
