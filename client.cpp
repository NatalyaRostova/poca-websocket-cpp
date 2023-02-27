#include <atomic>
#include <iostream>

#include "WebSocketClient.h"

class Client : public poca_ws::WebSocketClientListener {
public:
    virtual void OnBinary(uint8_t* data, int len) override {
        std::cout << "Receive binary data, len: " << len << std::endl;
    }
    virtual void OnText(std::string& msg) override { std::cout << "OnText: " << msg << std::endl; };
    virtual void OnClosed() override { std::cout << "OnClosed" << std::endl; }

    void Run(std::string addr, int port) {
        ws_client = new poca_ws::WebSocketClient(*this);
        ws_client->Connect(addr, port);

        std::string msg = std::string(20480, 'a');
        for (int i = 0; i < 1000; ++i) {
            msg += std::to_string(i);
            ws_client->SendMessage(msg);
            std::cout << i << std::endl;
        }
        usleep(500 * 1000);
        ws_client->Disconnect();
        usleep(500 * 1000);
    }

    poca_ws::WebSocketClient* ws_client;
};

int main(int argc, char* argv[]) {
    Client c;
    c.Run("127.0.0.1", 8080);
    poca_ws::WebSocketClient::CloseAll();
    return 0;
}