#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <cstddef>

template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(size_t capacity = 8) : cap_(capacity) {}

    /* Push an item. Blocks if full. Returns false if queue is closed. */
    bool push(T &&item) {
        std::unique_lock<std::mutex> lk(mtx_);
        full_cv_.wait(lk, [this] { return q_.size() < cap_ || closed_; });
        if (closed_) return false;
        q_.push(std::move(item));
        empty_cv_.notify_one();
        return true;
    }

    /* Pop an item. Blocks if empty. Returns false if queue is closed and empty. */
    bool pop(T &item) {
        std::unique_lock<std::mutex> lk(mtx_);
        empty_cv_.wait(lk, [this] { return !q_.empty() || closed_; });
        if (q_.empty()) return false;
        item = std::move(q_.front());
        q_.pop();
        full_cv_.notify_one();
        return true;
    }

    /* Close the queue. All blocked push/pop will unblock and return false. */
    void close() {
        std::lock_guard<std::mutex> lk(mtx_);
        closed_ = true;
        full_cv_.notify_all();
        empty_cv_.notify_all();
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return closed_;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return q_.size();
    }

private:
    std::queue<T>         q_;
    size_t                cap_;
    bool                  closed_ = false;
    mutable std::mutex    mtx_;
    std::condition_variable full_cv_;
    std::condition_variable empty_cv_;
};
