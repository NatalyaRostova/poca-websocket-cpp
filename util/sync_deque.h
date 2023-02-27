#ifndef POCA_WEBSOCKET_CPP_UTIL_SYNC_DEQUE_H
#define POCA_WEBSOCKET_CPP_UTIL_SYNC_DEQUE_H

#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>

template <typename T>
class SyncDeque {
public:
    void Put(T& t) {
        std::unique_lock<std::mutex> lock(mux_);
        que_.push_back(t);
        cv_.notify_one();
    }

    T Get() {
        std::unique_lock<std::mutex> lock(mux_);
        while (que_.empty()) {
            cv_.wait(lock);
        }
        T ret = que_.front();
        que_.pop_front();
        return ret;
    }

    bool GetNoWait(T& t) {
        std::unique_lock<std::mutex> lock(mux_);
        if (que_.empty()) {
            return false;
        }
        t = que_.front();
        que_.pop_front();
        return true;
    }

    int GetSize() {
        std::unique_lock<std::mutex> lock(mux_);
        return que_.size();
    }

private:
    std::deque<T> que_;

    std::mutex mux_;
    std::condition_variable cv_;
};

#endif