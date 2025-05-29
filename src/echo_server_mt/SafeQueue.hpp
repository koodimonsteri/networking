#pragma once

#include <deque>
#include <mutex>
#include <vector>
#include <functional>
#include <optional>

// From minecraft project with some modifications
template <class T>
class SafeQueue {
public:
    SafeQueue() = default;

    void push(T t) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(t));
    }

    std::optional<T> pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop_front();
        return item;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::deque<T> queue_;
};