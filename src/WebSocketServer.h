#ifndef POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_H
#define POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_H

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "WebSocketFrameBuffer.h"
#include "WebSocketServerListener.h"
#include "libwebsockets.h"
#include "ring_fifo.h"
#include "sync_deque.h"

namespace poca_ws {
    class WebSocketServer {
    public:
        WebSocketServer(WebSocketServerListener& listener);
        WebSocketServer() = delete;
        WebSocketServer(const WebSocketServer&) = delete;
        WebSocketServer& operator=(const WebSocketServer&) = delete;
        ~WebSocketServer();

        int ListenAndServe(int port);
        void Close();

        int SendMessage(int64_t user_id, std::string& msg);
        int SendBinary(int64_t user_id, uint8_t* data, int len);

    private:
        WebSocketServerListener* listener_;

        int port_;
        bool conn_established_ = false;
        std::atomic_bool close_ = ATOMIC_VAR_INIT(false);

        SyncDeque<WebSocketFrameBuffer*> deque_receive_buf_empty_;
        SyncDeque<WebSocketFrameBuffer*> deque_receive_buf_full_;
        std::map<lws*, WebSocketFrameBuffer*> receive_buf_internal_;
        std::thread callback_thread_;
        void CallbackEventLoop();

        SyncDeque<WebSocketFrameBuffer*> deque_send_buf_empty_;
        SyncDeque<WebSocketFrameBuffer*> deque_send_buf_full_;

        int LwsClientCallback(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len);

        std::mutex mux_;
        std::condition_variable cv_;
        lws_context* context_;

        static std::map<lws_context*, WebSocketServer*> server_ptr_;
        static std::mutex server_ptr_mux_;
        static int _LwsClientCallback(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len);
    };
}  // namespace poca_ws
#endif