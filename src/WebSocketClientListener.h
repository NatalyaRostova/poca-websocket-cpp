#ifndef POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_LISTENER_H
#define POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_LISTENER_H

#include <string>

namespace poca_ws {
    class WebSocketClientListener {
    public:
        WebSocketClientListener() {}
        ~WebSocketClientListener() {}

        virtual void OnBinary(uint8_t* data, int len) = 0;
        virtual void OnText(std::string& msg) = 0;
        virtual void OnClosed() = 0;
    };
}  // namespace poca_ws
#endif
