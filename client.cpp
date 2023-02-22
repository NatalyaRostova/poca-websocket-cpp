#include <atomic>
#include <iostream>

#include "WebSocketClient.h"

class Callback : public WebSocketClientListener {
    virtual void OnReceive(void* data, int len) override {
        std::cout << "Receive data: " << (std::string)(char*)data << ", len: " << len << std::endl;
    }
    virtual void OnClosed() override { std::cout << "OnClosed" << std::endl; }
};

int main(int argc, char* argv[]) {
    Callback cb;
    WebSocketClient client(cb);
    client.Connect("127.0.0.1", 8080);
    std::string msg = std::string(2048, 'a');
    for (int i = 0; i < 1000; ++i) {
        for (int j = 0; j < msg.size(); ++j) {
            msg[j] = 'a' + (j % 26);
        }
        client.SendMessage(msg);
    }
    usleep(500 * 1000);
    client.Disconnect();
    usleep(500 * 1000);
    WebSocketClient::CloseAll();
    return 0;
}