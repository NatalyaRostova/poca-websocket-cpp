#ifndef POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_H
#define POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_H

#include <atomic>
#include <functional>
#include <string>

#include "WebSocketClientListener.h"
#include "WebSocketReceiveBuffer.h"
#include "libwebsockets.h"
#include "ring_fifo.h"

class WebSocketClient {
public:
    WebSocketClient(WebSocketClientListener& listener);
    WebSocketClient() = delete;
    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;
    ~WebSocketClient();

    int Connect(std::string addr, int port, std::string path = "/");
    void Disconnect();
    static void CloseAll();

    int SendMessage(std::string& msg);
    int SendBinary(void* data, int len);

private:
    WebSocketClientListener* listener_;

    lws* wsi_ = nullptr;

    std::string server_address_;
    int port_;
    std::string path_;
    bool conn_established_ = false;
    std::atomic_bool close_ = ATOMIC_VAR_INIT(false);
    WebSocketReceiveBuffer receive_buf_;

    RingFIFO<std::function<void(void)>>* msg_queue_;

    static int LwsClientCallback(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len);
    static void EventLoop();
    void WaitConnEstablish();
};

#endif