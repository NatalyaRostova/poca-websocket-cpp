#ifndef POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_H
#define POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_H

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

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
    void WaitConnEstablish();

    static int LwsClientCallback(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len);
    static void EventLoop();

    static std::once_flag once_flag_;
    static std::thread worker_thread_;
    static std::atomic_bool running_;
    static std::mutex mux_;
    static bool protocol_inited_;
    static std::condition_variable cv_;
    static lws_context* context_;
    static std::map<lws*, WebSocketClient*> map_lws_wsc_;
    static RingFIFO<std::function<void(void)>> conn_queue_;
};

#endif