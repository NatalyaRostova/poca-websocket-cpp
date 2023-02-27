#ifndef POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CALLBACK_BUFFER_H
#define POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CALLBACK_BUFFER_H

#include <cstdint>
#include <mutex>

namespace poca_ws {
    class WebSocketCallbackBuffer {
    public:
        WebSocketCallbackBuffer();
        ~WebSocketCallbackBuffer();

        void Push(void* data, int size);
        void Clear();
        void* GetPtr();
        int GetLength();

        void SetType(int type);
        int GetType();

        void SetUserId(int64_t user_id);
        int64_t GetUserId();

        void Lock();
        void Unlock();

    private:
        std::mutex mux_;
        int type_;
        int64_t user_id_;
        int capacity_;
        int len_;
        uint8_t* buf_;
    };
}  // namespace poca_ws
#endif