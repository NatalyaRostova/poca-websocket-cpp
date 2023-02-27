#include "WebSocketCallbackBuffer.h"

#include <cstdlib>
#include <cstring>

namespace poca_ws {
    WebSocketCallbackBuffer::WebSocketCallbackBuffer() {
        capacity_ = 256;
        buf_ = new uint8_t[capacity_];
        memset(buf_, 0, capacity_);
        len_ = 0;
    }

    WebSocketCallbackBuffer::~WebSocketCallbackBuffer() { delete[] buf_; }

    void WebSocketCallbackBuffer::Lock() { mux_.lock(); }

    void WebSocketCallbackBuffer::Unlock() { mux_.unlock(); }

    void WebSocketCallbackBuffer::SetType(int type) { type_ = type; }

    int WebSocketCallbackBuffer::GetType() { return type_; }

    void WebSocketCallbackBuffer::SetUserId(int64_t user_id) { user_id_ = user_id; }

    int64_t WebSocketCallbackBuffer::GetUserId() { return user_id_; }

    void SetUserId(int64_t user_id);
    int64_t GetUserId();

    void WebSocketCallbackBuffer::Push(void* data, int size) {
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
        memcpy(buf_ + len_, data, size);
        len_ += size;
    }

    void WebSocketCallbackBuffer::Clear() {
        memset(buf_, 0, capacity_);
        len_ = 0;
    }

    void* WebSocketCallbackBuffer::GetPtr() { return buf_; }

    int WebSocketCallbackBuffer::GetLength() { return len_; }
}  // namespace poca_ws