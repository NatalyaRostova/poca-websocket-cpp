#include <atomic>
#include <iostream>

#include "WebSocketClient.h"

class Callback : public poca_ws::WebSocketClientListener {
public:
    virtual void OnBinary(uint8_t* data, int len) override {
        std::cout << "Receive binary data, len: " << len << std::endl;
    }
    virtual void OnText(std::string& msg) override { std::cout << "OnText, len: " << msg.size() << std::endl; };
    virtual void OnClosed() override { std::cout << "OnClosed" << std::endl; }
};

int main(int argc, char* argv[]) {
    Callback cb;
    poca_ws::WebSocketClient client(cb);
    client.Connect("127.0.0.1", 8080);
    std::string msg = std::string(20480, 'a');
    for (int i = 0; i < 1000; ++i) {
        msg += std::to_string(i);
        client.SendMessage(msg);
        std::cout << i << std::endl;
        // client.SendBinary((void*)msg.c_str(), msg.size());
    }
    usleep(500 * 1000);
    client.Disconnect();
    usleep(500 * 1000);
    poca_ws::WebSocketClient::CloseAll();
    return 0;
}