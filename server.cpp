#include <signal.h>

#include <atomic>
#include <iostream>

#include "WebSocketServer.h"

class Server : public poca_ws::WebSocketServerListener {
public:
    virtual void OnBinary(int64_t user_id, uint8_t* data, int len) override {
        printf("OnBinary, user_id: %ld, size: %d\n", user_id, len);
        if (ws_server) {
            std::string msg = "receive binary data, len: " + std::to_string(len);
            ws_server->SendMessage(user_id, msg);
        }
    }

    virtual void OnText(int64_t user_id, std::string& msg) override {
        printf("OnText, user_id: %ld, len: %d\n", user_id, (int)msg.size());
        if (ws_server) {
            std::string forward = "receive msg: " + msg;
            ws_server->SendMessage(user_id, forward);
        }
    }

    virtual void OnConnect(int64_t user_id) override { printf("OnConnect, user_id: %ld\n", user_id); }

    virtual void OnClose(int64_t user_id) override { printf("OnClose, user_id: %ld\n", user_id); }

    void Run(int port) {
        ws_server = new poca_ws::WebSocketServer(*this);
        ws_server->ListenAndServe(port);
    }

    void Stop() { ws_server->Close(); }

private:
    poca_ws::WebSocketServer* ws_server = nullptr;
};

Server s;

void sigint_handler(int sig) { s.Stop(); }

int main(int argc, char* argv[]) {
    signal(SIGINT, sigint_handler);
    s.Run(8080);
}