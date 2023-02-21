#include "WebSocketClient.h"

#include <libwebsockets.h>

#include <atomic>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>

#include "logger.h"

#define MAX_PAYLOAD_SIZE 8192

static std::once_flag once_flag;
static std::thread worker_thread;
static std::mutex mux;
static bool protocol_inited = false;
static std::condition_variable cv;
static lws_context *context;
static std::map<lws *, WebSocketClient *> map_lws_wsc;
static lws_protocols protocols[] = {{
                                        "ws",
                                        nullptr,
                                        MAX_PAYLOAD_SIZE,
                                        MAX_PAYLOAD_SIZE,
                                    },
                                    {NULL, NULL, 0}};
static RingFIFO<std::function<void(void)>> conn_queue(10);

void WebSocketClient::EventLoop() {
    std::function<void(void)> conn_request;
    while (true) {
        while (conn_queue.GetNoWait(conn_request)) {
            conn_request();
        }
        lws_service(context, 0);
    }
}

WebSocketClient::WebSocketClient(WebSocketClientListener &listener) {
    std::call_once(once_flag, [&]() {
        protocols[0].callback = &WebSocketClient::LwsClientCallback;
        lws_context_creation_info ctx_info = {0};
        ctx_info.port = CONTEXT_PORT_NO_LISTEN;
        ctx_info.protocols = protocols;
        ctx_info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        context = lws_create_context(&ctx_info);
        worker_thread = std::thread(&WebSocketClient::EventLoop);
        worker_thread.detach();
    });
    std::unique_lock<std::mutex> lck(mux);
    cv.wait(lck, [&]() { return protocol_inited == true; });
    listener_ = &listener;
}

int WebSocketClient::LwsClientCallback(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len) {
    poca_info("LwsClientCallback, wsi: %p, reason: %d", wsi, reason);
    std::unique_lock<std::mutex> lck(mux);
    switch (reason) {
        case LWS_CALLBACK_PROTOCOL_INIT:
            protocol_inited = true;
            cv.notify_all();
            break;
        case LWS_CALLBACK_CLIENT_RECEIVE:
            poca_info("Rx: %s, wsi: %p", (char *)in, wsi);
            break;
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            poca_info("%s: established connection, wsi = %p", __func__, wsi);
            lws_callback_on_writable(wsi);
            map_lws_wsc[wsi]->conn_established_ = true;
            cv.notify_all();
            break;
        default:
            break;
    }
    return lws_callback_http_dummy(wsi, reason, user, in, len);
}

int WebSocketClient::SendMessage(std::string &msg) {
    std::unique_lock<std::mutex> lck(mux);
    cv.wait(lck, [&]() { return conn_established_ == true; });
    lws_write(wsi_, (unsigned char *)msg.c_str(), msg.size(), LWS_WRITE_TEXT);
    return 0;
}

int WebSocketClient::Connect(std::string addr, int port, std::string path) {
    server_address_ = addr;
    port_ = port;
    path_ = path;

    lws_client_connect_info i;

    memset(&i, 0, sizeof(i));

    i.context = context;
    i.port = port_;
    i.address = server_address_.c_str();
    i.path = path_.c_str();
    i.host = i.address;
    i.origin = i.address;
    i.ssl_connection = 0;
    i.protocol = "ws";
    i.local_protocol_name = "ws";

    std::function<void(void)> client_conn = [=]() {
        wsi_ = lws_client_connect_via_info(&i);
        if (!wsi_) {
            poca_info("connect failed");
            return 1;
        } else {
            map_lws_wsc[wsi_] = this;
            poca_info("connection %s:%d, wsi_: %p", i.address, i.port, wsi_);
            return 0;
        }
    };
    conn_queue.Put(client_conn);
    return 0;
}

void WebSocketClient::Disconnect() {}