#include <iostream>

#include "WebSocketClient.h"

class Callback : public WebSocketClientListener {
    virtual void OnReceive() override { std::cout << "OnReceive" << std::endl; }
    virtual void OnClosed() override { std::cout << "OnClosed" << std::endl; }
};

int main(int argc, char* argv[]) {
    Callback cb;
    WebSocketClient client(cb);
    client.Connect("127.0.0.1", 8080);
    for (int i = 0; i < 1000; ++i) {
        std::string msg = "aaa";
        client.SendMessage(msg);
    }
    return 0;
}