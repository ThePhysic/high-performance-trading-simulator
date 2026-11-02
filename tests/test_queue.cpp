#include "hpts/queue.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

void check(bool condition, const char* description) {
    if (!condition) {
        throw std::runtime_error(description);
    }
}

template <class T>
struct CloseOnExit {
    hpts::BoundedQueue<T>& queue;
    ~CloseOnExit() { queue.close(); }
};

template <class Predicate>
bool wait_until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

void test_close_drain_and_move() {
    bool threw = false;
    try {
        hpts::BoundedQueue<int> invalid(0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "zero capacity accepted");
    hpts::BoundedQueue<std::unique_ptr<int>> queue(3);
    check(queue.push(std::make_unique<int>(1)), "first push failed");
    check(queue.push(std::make_unique<int>(2)), "second push failed");
    check(queue.push(std::make_unique<int>(3)), "third push failed");
    queue.close();
    queue.close();
    check(!queue.push(std::make_unique<int>(4)), "closed queue accepted item");
    for (int expected = 1; expected <= 3; ++expected) {
        std::unique_ptr<int> value;
        check(queue.pop(value) && value && *value == expected, "close must drain FIFO items");
    }
    std::unique_ptr<int> empty;
    check(!queue.pop(empty), "drained closed queue produced item");
    check(queue.stats().high_water == 3, "wrong high-water mark");
}

void test_bounded_producer_backpressure() {
    hpts::BoundedQueue<int> queue(1);
    check(queue.push(1), "initial push failed");
    std::promise<bool> finished;
    auto result = finished.get_future();
    std::jthread producer([&] { finished.set_value(queue.push(2)); });
    CloseOnExit guard{queue};
    check(wait_until([&] { return queue.stats().full_waits == 1; }),
          "producer did not encounter bounded backpressure");
    check(result.wait_for(0ms) == std::future_status::timeout,
          "producer completed while queue was full");
    int value = 0;
    check(queue.pop(value) && value == 1, "first FIFO value incorrect");
    check(result.wait_for(5s) == std::future_status::ready && result.get(),
          "producer did not resume when capacity freed");
    check(queue.pop(value) && value == 2, "resumed producer value missing");
    check(queue.stats().high_water == 1, "queue exceeded capacity");
}

void test_close_wakes_consumers() {
    hpts::BoundedQueue<int> queue(1);
    std::promise<bool> finished;
    auto result = finished.get_future();
    std::jthread consumer([&] {
        int value = 0;
        finished.set_value(queue.pop(value));
    });
    CloseOnExit guard{queue};
    check(result.wait_for(20ms) == std::future_status::timeout,
          "empty open queue returned before close");
    queue.close();
    check(result.wait_for(5s) == std::future_status::ready && !result.get(),
          "close did not release blocked consumer");
}

void test_close_wakes_all_producers() {
    hpts::BoundedQueue<int> queue(1);
    check(queue.push(1), "initial push failed");
    constexpr std::size_t producer_count = 4;
    std::vector<std::promise<bool>> promises(producer_count);
    std::vector<std::future<bool>> results;
    std::vector<std::jthread> producers;
    for (std::size_t index = 0; index < producer_count; ++index) {
        results.push_back(promises[index].get_future());
        producers.emplace_back([&, index] {
            promises[index].set_value(queue.push(static_cast<int>(index + 2)));
        });
    }
    CloseOnExit guard{queue};
    check(wait_until([&] { return queue.stats().full_waits == producer_count; }),
          "producers failed to reach full-queue wait");
    queue.close();
    for (auto& result : results) {
        check(result.wait_for(5s) == std::future_status::ready && !result.get(),
              "close did not release a blocked producer");
    }
    int value = 0;
    check(queue.pop(value) && value == 1 && !queue.pop(value),
          "close altered previously queued item");
}

void test_concurrent_exactly_once_and_per_producer_order() {
    constexpr std::size_t producer_count = 4;
    constexpr std::size_t values_per_producer = 2000;
    constexpr std::size_t capacity = 7;
    hpts::BoundedQueue<std::uint64_t> queue(capacity);
    std::vector<std::jthread> producers;
    std::vector<std::uint64_t> received;
    received.reserve(producer_count * values_per_producer);
    std::jthread consumer([&] {
        std::uint64_t value = 0;
        while (queue.pop(value)) {
            received.push_back(value);
        }
    });
    CloseOnExit guard{queue};
    for (std::size_t index = 0; index < producer_count; ++index) {
        producers.emplace_back([&, index] {
            for (std::size_t item = 0; item < values_per_producer; ++item) {
                if (!queue.push(index * values_per_producer + item)) {
                    return;
                }
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    queue.close();
    consumer.join();
    check(received.size() == producer_count * values_per_producer,
          "concurrent queue lost items");
    std::vector<std::size_t> next(producer_count, 0);
    for (const auto value : received) {
        const auto producer = value / values_per_producer;
        const auto offset = value % values_per_producer;
        check(producer < producer_count && offset == next[static_cast<std::size_t>(producer)]++,
              "concurrent queue duplicated or reordered producer items");
    }
    check(queue.stats().high_water <= capacity, "concurrent queue exceeded bound");
}

}  // namespace

int main() {
    try {
        test_close_drain_and_move();
        test_bounded_producer_backpressure();
        test_close_wakes_consumers();
        test_close_wakes_all_producers();
        test_concurrent_exactly_once_and_per_producer_order();
        std::cout << "PASS bounded queue: drain, move-only values, backpressure, wakeups, "
                     "8000 concurrent items with per-producer FIFO and no duplicates.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL bounded queue: " << error.what() << '\n';
        return 1;
    }
}
