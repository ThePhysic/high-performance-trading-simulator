#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace hpts {

// Blocking bounded handoff. close() wakes all waiters; pop() drains queued items.
// Ownership transfers by move, and the mutex establishes publication visibility.
template <class T>
class BoundedQueue {
public:
    struct Stats { std::size_t high_water = 0, full_waits = 0; };
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {
        if (capacity == 0) throw std::invalid_argument("queue capacity must be positive");
    }
    bool push(T value) {
        std::unique_lock lock(mutex_);
        if (!closed_ && queue_.size() == capacity_) ++stats_.full_waits;
        writable_.wait(lock, [&] { return closed_ || queue_.size() < capacity_; });
        if (closed_) return false;
        queue_.push_back(std::move(value));
        if (queue_.size() > stats_.high_water) stats_.high_water = queue_.size();
        readable_.notify_one();
        return true;
    }
    bool pop(T& value) {
        std::unique_lock lock(mutex_);
        readable_.wait(lock, [&] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) return false;
        value = std::move(queue_.front());
        queue_.pop_front();
        writable_.notify_one();
        return true;
    }
    void close() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        readable_.notify_all();
        writable_.notify_all();
    }
    [[nodiscard]] Stats stats() const { std::lock_guard lock(mutex_); return stats_; }
private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable readable_, writable_;
    std::deque<T> queue_;
    bool closed_ = false;
    Stats stats_;
};

}  // namespace hpts
