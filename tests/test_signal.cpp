#include <catch2/catch_test_macros.hpp>

#include "spotify/events.h"

#include <atomic>
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
