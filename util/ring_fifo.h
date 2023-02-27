#ifndef POCA_WEBSOCKET_CPP_UTIL_RING_FIFO_H
#define POCA_WEBSOCKET_CPP_UTIL_RING_FIFO_H

#include <condition_variable>
#include <mutex>

template <typename T>
class RingFIFO {
public:
    void Put(T& t) {
        std::unique_lock<std::mutex> lock(mux_);
        cv_empty_.wait(lock, [=] { return head_ - tail_ < size_; });
        buffer_[head_ % size_] = t;
        head_++;
        cv_full_.notify_one();
    }

    bool PutNoWait(T& t) {
        std::unique_lock<std::mutex> lock(mux_);

        if (head_ - tail_ >= size_) {
            return false;
        }

        buffer_[head_ % size_] = t;
        head_++;
        cv_full_.notify_one();

        return true;
    }

    T Get() {
        std::unique_lock<std::mutex> lock(mux_);
        cv_full_.wait(lock, [=] { return head_ > tail_; });
        T ret = buffer_[tail_ % size_];
        tail_++;
        cv_empty_.notify_one();
        return ret;
    }

    bool GetNoWait(T& t) {
        std::unique_lock<std::mutex> lock(mux_);
        if (head_ <= tail_) {
            return false;
        }
        t = buffer_[tail_ % size_];
        tail_++;
        cv_empty_.notify_one();
        return true;
    }

    RingFIFO() = delete;
    RingFIFO(int size) {
        buffer_ = new T[size];
        head_ = 0;
        tail_ = 0;
        size_ = size;
    }
    ~RingFIFO() { delete[] buffer_; }

private:
    T* buffer_;
    int size_;
    int head_;
    int tail_;

    std::mutex mux_;
    std::condition_variable cv_full_;
    std::condition_variable cv_empty_;
};

#endif