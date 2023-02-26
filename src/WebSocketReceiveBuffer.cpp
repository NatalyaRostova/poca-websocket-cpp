#include "WebSocketReceiveBuffer.h"

#include <cstdlib>
#include <cstring>

WebSocketReceiveBuffer::WebSocketReceiveBuffer() {
    capacity_ = 256;
    buf_ = new uint8_t[capacity_];
    memset(buf_, 0, capacity_);
    len_ = 0;
}

WebSocketReceiveBuffer::~WebSocketReceiveBuffer() { delete[] buf_; }

void WebSocketReceiveBuffer::Push(void* data, int size) {
    std::lock_guard<std::mutex> lck(mux_);
    bool should_move = false;
    while (len_ + size + 1 > capacity_) {
        should_move = true;
        capacity_ *= 2;
    }
    if (should_move) {
        uint8_t* tmp = new uint8_t[capacity_];
        memset(tmp, 0, capacity_);
        if (len_ > 0) {
            memcpy(tmp, buf_, len_);
        }
        delete[] buf_;
        buf_ = tmp;
    }
    memcpy(buf_ + len_, data, size);
    len_ += size;
}

void WebSocketReceiveBuffer::Clear() {
    std::lock_guard<std::mutex> lck(mux_);
    memset(buf_, 0, capacity_);
    len_ = 0;
}

void* WebSocketReceiveBuffer::GetPtr() {
    std::lock_guard<std::mutex> lck(mux_);
    return buf_;
}

int WebSocketReceiveBuffer::GetLength() {
    std::lock_guard<std::mutex> lck(mux_);
    return len_;
}