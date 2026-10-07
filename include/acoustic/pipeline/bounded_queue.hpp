#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

#include <acoustic/core/error.hpp>

namespace acoustic {

enum class BackpressurePolicy {
    BlockProducer,
    DropOldest,
    DropNewest,
    KeepLatest,
    Reject,
};

enum class PushResult {
    Enqueued,
    DroppedOldest,
    DroppedNewest,
    ReplacedWithLatest,
    Rejected,
    Closed,
};

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity, BackpressurePolicy policy)
        : capacity_(capacity), policy_(policy) {
        if (capacity_ == 0U) {
            throw Error{ErrorCode::InvalidArgument, "BoundedQueue capacity must be non-zero"};
        }
    }

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    PushResult push(T value) {
        std::unique_lock<std::mutex> lock{mutex_};
        if (closed_) {
            return PushResult::Closed;
        }

        if (policy_ == BackpressurePolicy::BlockProducer) {
            not_full_.wait(lock, [this] { return closed_ || queue_.size() < capacity_; });
            if (closed_) {
                return PushResult::Closed;
            }
            queue_.push_back(std::move(value));
            not_empty_.notify_one();
            return PushResult::Enqueued;
        }

        if (queue_.size() < capacity_) {
            queue_.push_back(std::move(value));
            not_empty_.notify_one();
            return PushResult::Enqueued;
        }

        switch (policy_) {
        case BackpressurePolicy::DropOldest:
            queue_.pop_front();
            queue_.push_back(std::move(value));
            not_empty_.notify_one();
            return PushResult::DroppedOldest;
        case BackpressurePolicy::DropNewest:
            return PushResult::DroppedNewest;
        case BackpressurePolicy::KeepLatest:
            queue_.clear();
            queue_.push_back(std::move(value));
            not_empty_.notify_one();
            return PushResult::ReplacedWithLatest;
        case BackpressurePolicy::Reject:
            return PushResult::Rejected;
        case BackpressurePolicy::BlockProducer:
            break;
        }
        return PushResult::Rejected;
    }

    [[nodiscard]] std::optional<T> pop() {
        std::unique_lock<std::mutex> lock{mutex_};
        not_empty_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return value;
    }

    [[nodiscard]] std::optional<T> tryPop() {
        std::lock_guard<std::mutex> lock{mutex_};
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return value;
    }

    void close() {
        std::lock_guard<std::mutex> lock{mutex_};
        closed_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    [[nodiscard]] bool closed() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return closed_;
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return queue_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] BackpressurePolicy policy() const noexcept { return policy_; }

private:
    const std::size_t capacity_;
    const BackpressurePolicy policy_;
    mutable std::mutex mutex_{};
    std::condition_variable not_empty_{};
    std::condition_variable not_full_{};
    std::deque<T> queue_{};
    bool closed_{false};
};

}  // namespace acoustic
