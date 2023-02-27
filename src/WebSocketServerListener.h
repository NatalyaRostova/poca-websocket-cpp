#ifndef POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_LISTENER_H
#define POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_LISTENER_H

#include <string>

namespace poca_ws {
    class WebSocketServerListener {
    public:
        WebSocketServerListener() {}
        ~WebSocketServerListener() {}

        virtual void OnBinary(int64_t user_id, uint8_t* data, int len) = 0;
        virtual void OnText(int64_t user_id, std::string& msg) = 0;
        virtual void OnConnect(int64_t user_id) = 0;
        virtual void OnClose(int64_t user_id) = 0;
    };
}  // namespace poca_ws
#endif
