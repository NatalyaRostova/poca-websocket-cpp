#ifndef POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_LISTENER_H
#define POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_LISTENER_H

namespace poca_ws {
    class WebSocketClientListener {
    public:
        WebSocketClientListener() {}
        ~WebSocketClientListener() {}

        virtual void OnReceive(void* data, int len) = 0;
        virtual void OnClosed() = 0;
    };
}  // namespace poca_ws
#endif
