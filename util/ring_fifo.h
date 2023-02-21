#ifndef POCA_WEBSOCKET_CPP_UTIL_RING_FIFO_H
#define POCA_WEBSOCKET_CPP_UTIL_RING_FIFO_H

#include <condition_variable>
#include <mutex>

template <typename T>
class RingFIFO {
public:
    void Put(T& t);
    bool PutNoWait(T& t);

    T Get();
    bool GetNoWait(T& t);

    RingFIFO() = delete;
    RingFIFO(int size);
    ~RingFIFO();

private:
    T* buffer_;
    int size_;
    int head_;
    int tail_;

    std::mutex mux_;
    std::condition_variable cv_full_;
    std::condition_variable cv_empty_;
};

template <typename T>
RingFIFO<T>::RingFIFO(int size) {
    buffer_ = new T[size];
    head_ = 0;
    tail_ = 0;
    size_ = size;
}

template <typename T>
RingFIFO<T>::~RingFIFO() {
    delete[] buffer_;
}

template <typename T>
void RingFIFO<T>::Put(T& t) {
    std::unique_lock<std::mutex> lock(mux_);
    cv_empty_.wait(lock, [=] { return head_ - tail_ < size_; });
    buffer_[head_ % size_] = t;
    head_++;
    cv_full_.notify_one();
}

template <typename T>
bool RingFIFO<T>::PutNoWait(T& t) {
    std::unique_lock<std::mutex> lock(mux_);

    if (head_ - tail_ >= size_) {
        return false;
    }

    buffer_[head_ % size_] = t;
    head_++;
    cv_full_.notify_one();

    return true;
}

template <typename T>
T RingFIFO<T>::Get() {
    std::unique_lock<std::mutex> lock(mux_);
    cv_full_.wait(lock, [=] { return head_ > tail_; });
    T ret = buffer_[tail_ % size_];
    tail_++;
    cv_empty_.notify_one();
    return ret;
}

template <typename T>
bool RingFIFO<T>::GetNoWait(T& t) {
    std::unique_lock<std::mutex> lock(mux_);
    if (head_ <= tail_) {
        return false;
    }
    t = buffer_[tail_ % size_];
    tail_++;
    cv_empty_.notify_one();
    return true;
}

#endif