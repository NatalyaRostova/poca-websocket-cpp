#include "WebSocketReceiveBuffer.h"

#include <cstdlib>
#include <cstring>

WebSocketReceiveBuffer::WebSocketReceiveBuffer() {
    capacity = 256;
    buf = new uint8_t[capacity];
    memset(buf, 0, capacity);
    len = 0;
}

void WebSocketReceiveBuffer::Push(void* data, int size) {
    std::lock_guard<std::mutex> lock(mux);
    bool should_move = false;
    while (len + size + 1 > capacity) {
        should_move = true;
        capacity *= 2;
    }
    if (should_move) {
        uint8_t* tmp = new uint8_t[capacity];
        memset(tmp, 0, capacity);
        if (len > 0) {
            memcpy(tmp, buf, len);
        }
        delete[] buf;
        buf = tmp;
    }
    memcpy(buf + len, data, size);
    len += size;
}

void WebSocketReceiveBuffer::Clear() {
    std::lock_guard<std::mutex> lock(mux);
    memset(buf, 0, capacity);
    len = 0;
}

void* WebSocketReceiveBuffer::GetPtr() { return buf; }

int WebSocketReceiveBuffer::GetLength() { return len; }