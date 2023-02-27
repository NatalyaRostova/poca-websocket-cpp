#include <signal.h>

#include <atomic>
#include <iostream>

#include "WebSocketServer.h"

class Callback : public poca_ws::WebSocketServerListener {
public:
    poca_ws::WebSocketServer* s = nullptr;

    virtual void OnReceive(int64_t user_id, void* data, int len) override {
        printf("OnReceive, user_id: %ld, size: %d\n", user_id, len);
        if (s) {
            std::string msg = "receive data, len: " + std::to_string(len);
            s->SendMessage(user_id, msg);
        }
    }

    virtual void OnConnect(int64_t user_id) override { printf("OnConnect, user_id: %ld\n", user_id); }

    virtual void OnClose(int64_t user_id) override { printf("OnClose, user_id: %ld\n", user_id); }
};

Callback cb;
poca_ws::WebSocketServer* server;

void sigint_handler(int sig) { server->Close(); }

int main(int argc, char* argv[]) {
    signal(SIGINT, sigint_handler);
    server = new poca_ws::WebSocketServer(cb);
    cb.s = server;
    server->ListenAndServe(8080);
}