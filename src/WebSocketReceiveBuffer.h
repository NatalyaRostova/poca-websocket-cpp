#ifndef POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_RECEIVE_BUFFER_H
#define POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_RECEIVE_BUFFER_H

#include <cstdint>
#include <mutex>

class WebSocketReceiveBuffer {
public:
    WebSocketReceiveBuffer();
    ~WebSocketReceiveBuffer();

    void Push(void* data, int size);
    void Clear();
    void* GetPtr();
    int GetLength();

private:
    std::mutex mux_;
    int capacity_;
    int len_;
    uint8_t* buf_;
};

#endif