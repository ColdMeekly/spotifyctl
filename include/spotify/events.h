#pragma once

// Minimal thread-safe signal/slot primitive. Replaces boost::signals2 so the
// library has zero external dependencies.

#include <algorithm>
#include <cstddef>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace spotify {

template <typename Signature>
class Signal;

template <typename... Args>
class Signal<void(Args...)> {
 public:
    using Slot = std::function<void(Args...)>;
    using Token = std::size_t;

    Signal() = default;
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
    Signal(Signal&&) = delete;
    Signal& operator=(Signal&&) = delete;

    Token connect(Slot slot) {
        std::lock_guard<std::mutex> lock(mu_);
        Token t = ++next_;
        slots_.emplace_back(t, std::move(slot));
        return t;
    }

    // Connects a slot and synchronously replays it once with the supplied
    // arguments, all under the signal's internal lock. This guarantees the new
    // slot sees at least one value and cannot interleave with a concurrent
    // emit — consumers that need "current + all future" updates no longer
    // have to race connect() against a manual Latest*() call.
    template <typename... ReplayArgs>
    Token ConnectAndReplay(Slot slot, ReplayArgs&&... replayArgs) {
        std::lock_guard<std::mutex> lock(mu_);
        Token t = ++next_;
        slots_.emplace_back(t, slot);
        slot(std::forward<ReplayArgs>(replayArgs)...);
        return t;
    }

    void disconnect(Token t) {
        std::lock_guard<std::mutex> lock(mu_);
        slots_.erase(
            std::remove_if(slots_.begin(), slots_.end(),
                           [t](const auto& p) { return p.first == t; }),
            slots_.end());
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mu_);
        slots_.clear();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return slots_.size();
    }

    void operator()(Args... args) const {
        std::vector<Slot> snapshot;
        {
            std::lock_guard<std::mutex> lock(mu_);
            snapshot.reserve(slots_.size());
            for (const auto& p : slots_) snapshot.push_back(p.second);
        }
        for (auto& fn : snapshot) fn(args...);
    }

 private:
    mutable std::mutex mu_;
    std::vector<std::pair<Token, Slot>> slots_;
    Token next_ = 0;
};

}  // namespace spotify
