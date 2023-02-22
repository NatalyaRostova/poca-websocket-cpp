#ifndef POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_LISTENER_H
#define POCA_WEBSOCKET_CPP_SRC_WEB_SOCKET_CLIENT_LISTENER_H

class WebSocketClientListener {
public:
    WebSocketClientListener() {}
    ~WebSocketClientListener() {}

    virtual void OnReceive(void* data, int len) = 0;
    virtual void OnClosed() = 0;

private:
};

#endif
