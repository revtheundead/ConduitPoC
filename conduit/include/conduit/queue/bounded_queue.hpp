// SPDX-License-Identifier: MIT
// Conduit - Thread-Safe Bounded Queue

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <vector>

namespace conduit::queue {

// ============================================================================
// Drop Policy
// ============================================================================

enum class DropPolicy {
    DropOldest,   // Remove oldest element to make room
    DropNewest,   // Reject new element
    Block         // Block until space available
};

// ============================================================================
// Queue Statistics
// ============================================================================

struct QueueStats {
    std::atomic<uint64_t> enqueued{0};
    std::atomic<uint64_t> dequeued{0};
    std::atomic<uint64_t> dropped{0};
    std::atomic<size_t> peak_size{0};
    std::atomic<size_t> current_size{0};

    void reset() {
        enqueued.store(0);
        dequeued.store(0);
        dropped.store(0);
        peak_size.store(0);
        current_size.store(0);
    }
};

struct QueueStatsSnapshot {
    uint64_t enqueued = 0;
    uint64_t dequeued = 0;
    uint64_t dropped = 0;
    size_t peak_size = 0;
    size_t current_size = 0;

    static QueueStatsSnapshot from(const QueueStats& stats) {
        return {
            .enqueued = stats.enqueued.load(std::memory_order_relaxed),
            .dequeued = stats.dequeued.load(std::memory_order_relaxed),
            .dropped = stats.dropped.load(std::memory_order_relaxed),
            .peak_size = stats.peak_size.load(std::memory_order_relaxed),
            .current_size = stats.current_size.load(std::memory_order_relaxed),
        };
    }
};

// ============================================================================
// BoundedQueue: Thread-safe bounded MPMC queue (ring buffer)
// ============================================================================

template<typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity, DropPolicy policy = DropPolicy::DropOldest)
        : capacity_(capacity > 0 ? capacity : 1)
        , drop_policy_(policy)
        , buffer_(capacity_)
        , head_(0)
        , tail_(0)
        , size_(0)
        , closed_(false) {}

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;
    BoundedQueue(BoundedQueue&&) = delete;
    BoundedQueue& operator=(BoundedQueue&&) = delete;

    // ========================================================================
    // Producer operations
    // ========================================================================

    bool try_push(T item) {
        std::unique_lock lock(mutex_);

        if (closed_) return false;

        if (size_ >= capacity_) {
            switch (drop_policy_) {
                case DropPolicy::DropOldest:
                    head_ = (head_ + 1) % capacity_;
                    --size_;
                    stats_.dropped.fetch_add(1, std::memory_order_relaxed);
                    break;
                case DropPolicy::DropNewest:
                    stats_.dropped.fetch_add(1, std::memory_order_relaxed);
                    return false;
                case DropPolicy::Block:
                    stats_.dropped.fetch_add(1, std::memory_order_relaxed);
                    return false;
            }
        }

        buffer_[tail_] = std::move(item);
        tail_ = (tail_ + 1) % capacity_;
        ++size_;

        stats_.enqueued.fetch_add(1, std::memory_order_relaxed);
        update_peak_size();

        lock.unlock();
        not_empty_.notify_one();
        return true;
    }

    bool push(T item) {
        std::unique_lock lock(mutex_);

        if (drop_policy_ == DropPolicy::Block) {
            not_full_.wait(lock, [this] {
                return size_ < capacity_ || closed_;
            });
        }

        if (closed_) return false;

        if (size_ >= capacity_) {
            if (drop_policy_ == DropPolicy::DropOldest) {
                head_ = (head_ + 1) % capacity_;
                --size_;
                stats_.dropped.fetch_add(1, std::memory_order_relaxed);
            } else if (drop_policy_ == DropPolicy::DropNewest) {
                stats_.dropped.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        }

        buffer_[tail_] = std::move(item);
        tail_ = (tail_ + 1) % capacity_;
        ++size_;

        stats_.enqueued.fetch_add(1, std::memory_order_relaxed);
        update_peak_size();

        lock.unlock();
        not_empty_.notify_one();
        return true;
    }

    template<typename Rep, typename Period>
    bool push_for(T item, std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock lock(mutex_);

        if (drop_policy_ == DropPolicy::Block && size_ >= capacity_) {
            if (!not_full_.wait_for(lock, timeout, [this] {
                return size_ < capacity_ || closed_;
            })) {
                return false;
            }
        }

        if (closed_) return false;

        if (size_ >= capacity_) {
            if (drop_policy_ == DropPolicy::DropOldest) {
                head_ = (head_ + 1) % capacity_;
                --size_;
                stats_.dropped.fetch_add(1, std::memory_order_relaxed);
            } else {
                stats_.dropped.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        }

        buffer_[tail_] = std::move(item);
        tail_ = (tail_ + 1) % capacity_;
        ++size_;

        stats_.enqueued.fetch_add(1, std::memory_order_relaxed);
        update_peak_size();

        lock.unlock();
        not_empty_.notify_one();
        return true;
    }

    // ========================================================================
    // Consumer operations
    // ========================================================================

    std::optional<T> try_pop() {
        std::unique_lock lock(mutex_);
        if (size_ == 0) return std::nullopt;

        T item = std::move(buffer_[head_]);
        buffer_[head_] = T{};
        head_ = (head_ + 1) % capacity_;
        --size_;

        stats_.dequeued.fetch_add(1, std::memory_order_relaxed);
        stats_.current_size.store(size_, std::memory_order_relaxed);

        lock.unlock();
        not_full_.notify_one();
        return std::move(item);
    }

    std::optional<T> pop() {
        std::unique_lock lock(mutex_);

        not_empty_.wait(lock, [this] {
            return size_ > 0 || closed_;
        });

        if (size_ == 0) return std::nullopt;

        T item = std::move(buffer_[head_]);
        buffer_[head_] = T{};
        head_ = (head_ + 1) % capacity_;
        --size_;

        stats_.dequeued.fetch_add(1, std::memory_order_relaxed);
        stats_.current_size.store(size_, std::memory_order_relaxed);

        lock.unlock();
        not_full_.notify_one();
        return std::move(item);
    }

    template<typename Rep, typename Period>
    std::optional<T> pop_for(std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock lock(mutex_);

        if (!not_empty_.wait_for(lock, timeout, [this] {
            return size_ > 0 || closed_;
        })) {
            return std::nullopt;
        }

        if (size_ == 0) return std::nullopt;

        T item = std::move(buffer_[head_]);
        buffer_[head_] = T{};
        head_ = (head_ + 1) % capacity_;
        --size_;

        stats_.dequeued.fetch_add(1, std::memory_order_relaxed);
        stats_.current_size.store(size_, std::memory_order_relaxed);

        lock.unlock();
        not_full_.notify_one();
        return std::move(item);
    }

    std::vector<T> pop_batch(size_t max_count) {
        std::unique_lock lock(mutex_);

        std::vector<T> result;
        size_t count = std::min(max_count, size_);
        result.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            result.push_back(std::move(buffer_[head_]));
            buffer_[head_] = T{};
            head_ = (head_ + 1) % capacity_;
        }
        size_ -= count;

        stats_.dequeued.fetch_add(count, std::memory_order_relaxed);
        stats_.current_size.store(size_, std::memory_order_relaxed);

        lock.unlock();
        if (count > 0) not_full_.notify_all();
        return result;
    }

    // ========================================================================
    // Queue management
    // ========================================================================

    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    [[nodiscard]] bool is_closed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }

    void clear() {
        {
            std::lock_guard lock(mutex_);
            while (size_ > 0) {
                buffer_[head_] = T{};
                head_ = (head_ + 1) % capacity_;
                --size_;
            }
            stats_.current_size.store(0, std::memory_order_relaxed);
        }
        not_full_.notify_all();
    }

    // ========================================================================
    // Queries
    // ========================================================================

    [[nodiscard]] size_t size() const {
        std::lock_guard lock(mutex_);
        return size_;
    }

    [[nodiscard]] size_t capacity() const noexcept {
        return capacity_;
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard lock(mutex_);
        return size_ == 0;
    }

    [[nodiscard]] bool full() const {
        std::lock_guard lock(mutex_);
        return size_ >= capacity_;
    }

    [[nodiscard]] const QueueStats& stats() const noexcept {
        return stats_;
    }

    void reset_stats() {
        std::lock_guard lock(mutex_);
        stats_.reset();
    }

private:
    void update_peak_size() {
        stats_.current_size.store(size_, std::memory_order_release);
        size_t current_peak = stats_.peak_size.load(std::memory_order_acquire);
        while (size_ > current_peak &&
               !stats_.peak_size.compare_exchange_weak(current_peak, size_,
                   std::memory_order_acq_rel, std::memory_order_acquire)) {
        }
    }

    size_t capacity_;
    DropPolicy drop_policy_;

    std::vector<T> buffer_;
    size_t head_;
    size_t tail_;
    size_t size_;
    bool closed_;

    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;

    QueueStats stats_;
};

} // namespace conduit::queue
