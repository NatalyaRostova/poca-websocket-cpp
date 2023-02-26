#ifndef POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_LISTENER_H
#define POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_LISTENER_H

#include "libwebsockets.h"

class WebSocketServerListener {
public:
    WebSocketServerListener() {}
    ~WebSocketServerListener() {}

    virtual void OnReceive(int64_t user_id, void* data, int len) = 0;
    virtual void OnConnect(int64_t user_id) = 0;
    virtual void OnClose(int64_t user_id) = 0;
};

#endif
