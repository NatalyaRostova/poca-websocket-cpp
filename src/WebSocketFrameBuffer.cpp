#include "WebSocketFrameBuffer.h"

#include <cstdlib>
#include <cstring>

namespace poca_ws {
    WebSocketFrameBuffer::WebSocketFrameBuffer() {
        capacity_ = 256;
        buf_ = new uint8_t[capacity_];
        memset(buf_, 0, capacity_);
        len_ = 0;
    }

    WebSocketFrameBuffer::~WebSocketFrameBuffer() { delete[] buf_; }

    void WebSocketFrameBuffer::Lock() { mux_.lock(); }

    void WebSocketFrameBuffer::Unlock() { mux_.unlock(); }

    void WebSocketFrameBuffer::SetType(int type) { type_ = type; }

    int WebSocketFrameBuffer::GetType() { return type_; }

    void WebSocketFrameBuffer::SetUserId(int64_t user_id) { user_id_ = user_id; }

    int64_t WebSocketFrameBuffer::GetUserId() { return user_id_; }

    void WebSocketFrameBuffer::Push(uint8_t* data, int size) {
        bool should_move = false;
        while (len_ + size >= capacity_) {
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
        if (data != nullptr) memcpy(buf_ + len_, data, size);
        len_ += size;
    }

    void WebSocketFrameBuffer::Clear() {
        memset(buf_, 0, capacity_);
        len_ = 0;
    }

    uint8_t* WebSocketFrameBuffer::GetPtr() { return buf_; }

    int WebSocketFrameBuffer::GetLength() { return len_; }
}  // namespace poca_ws