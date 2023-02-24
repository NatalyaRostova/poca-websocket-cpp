#include "WebSocketClient.h"

#include <libwebsockets.h>

#include <atomic>
#include <iostream>

#include "logger.h"

#define MAX_PAYLOAD_SIZE 8192

std::once_flag WebSocketClient::once_flag_;
std::thread WebSocketClient::worker_thread_;
std::atomic_bool WebSocketClient::running_;
std::mutex WebSocketClient::mux_;
bool WebSocketClient::protocol_inited_ = false;
std::condition_variable WebSocketClient::cv_;
lws_context *WebSocketClient::context_;
std::map<lws *, WebSocketClient *> WebSocketClient::map_lws_wsc_;
RingFIFO<std::function<void(void)>> WebSocketClient::conn_queue_(10);

void WebSocketClient::EventLoop() {
    std::function<void(void)> conn_request;
    while (running_.load()) {
        while (conn_queue_.GetNoWait(conn_request)) {
            conn_request();
        }
        lws_service(context_, 0);
        cv_.notify_all();
    }
    lws_context_destroy(context_);
}

void WebSocketClient::CloseAll() {
    running_.store(false);
    lws_cancel_service(context_);
    worker_thread_.join();
}

WebSocketClient::WebSocketClient(WebSocketClientListener &listener) {
    std::call_once(once_flag_, [&]() {
        static lws_protocols protocols[] = {{
                                                "ws",
                                                &WebSocketClient::LwsClientCallback,
                                                MAX_PAYLOAD_SIZE,
                                                MAX_PAYLOAD_SIZE,
                                            },
                                            {NULL, NULL, 0}};
        lws_context_creation_info ctx_info = {0};
        ctx_info.port = CONTEXT_PORT_NO_LISTEN;
        ctx_info.protocols = protocols;
        ctx_info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        context_ = lws_create_context(&ctx_info);
        running_.store(true);
        worker_thread_ = std::thread(&WebSocketClient::EventLoop);
    });
    std::unique_lock<std::mutex> lck(mux_);
    cv_.wait(lck, [&]() { return protocol_inited_ == true; });
    listener_ = &listener;
    msg_queue_ = new RingFIFO<std::function<void(void)>>(10);
}

WebSocketClient::~WebSocketClient() { delete msg_queue_; }

int WebSocketClient::LwsClientCallback(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len) {
    poca_info("LwsClientCallback, wsi: %p, reason: %d", wsi, reason);
    std::unique_lock<std::mutex> lck(mux_);
    std::function<void(void)> msg_submit;
    int first = 0, final = 0;
    switch (reason) {
        case LWS_CALLBACK_PROTOCOL_INIT:
            protocol_inited_ = true;
            break;
        case LWS_CALLBACK_CLIENT_RECEIVE:
            first = lws_is_first_fragment(wsi);
            final = lws_is_final_fragment(wsi);
            // poca_info("Receive, wsi: %p, len: %d, first: %d, final: %d", wsi, len, first, final);
            if (first) {
                map_lws_wsc_[wsi]->receive_buf_.Clear();
            }
            map_lws_wsc_[wsi]->receive_buf_.Push(in, len);
            if (final) {
                map_lws_wsc_[wsi]->listener_->OnReceive(map_lws_wsc_[wsi]->receive_buf_.GetPtr(),
                                                        map_lws_wsc_[wsi]->receive_buf_.GetLength());
                map_lws_wsc_[wsi]->receive_buf_.Clear();
            }
            break;
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            if (map_lws_wsc_[wsi]->close_.load() == true) {
                return -1;
            }
            if (map_lws_wsc_[wsi]->msg_queue_->GetNoWait(msg_submit)) {
                msg_submit();
                lws_callback_on_writable(wsi);
            }
            break;
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            poca_info("%s: established connection, wsi = %p", __func__, wsi);
            lws_callback_on_writable(wsi);
            map_lws_wsc_[wsi]->conn_established_ = true;
            break;
        case LWS_CALLBACK_CLIENT_CLOSED:
            map_lws_wsc_[wsi]->listener_->OnClosed();
            break;
        default:
            break;
    }
    return lws_callback_http_dummy(wsi, reason, user, in, len);
}

void WebSocketClient::WaitConnEstablish() {
    std::unique_lock<std::mutex> lck(mux_);
    cv_.wait(lck, [&]() { return conn_established_ == true; });
}

int WebSocketClient::SendMessage(std::string &msg) {
    WaitConnEstablish();
    std::mutex msg_mux;
    std::unique_lock<std::mutex> msg_lck(msg_mux);
    std::condition_variable msg_cv;
    bool submitted = false;
    std::function<void(void)> msg_cmd = [&]() {
        unsigned char buf[msg.size() + LWS_PRE];
        memcpy(buf + LWS_PRE, msg.c_str(), msg.size());
        lws_write(wsi_, buf + LWS_PRE, msg.size(), LWS_WRITE_TEXT);
        submitted = true;
        msg_cv.notify_all();
    };

    msg_queue_->Put(msg_cmd);
    lws_callback_on_writable(wsi_);
    lws_cancel_service(context_);
    msg_cv.wait(msg_lck, [&]() { return submitted == true; });

    return 0;
}

int WebSocketClient::SendBinary(void *data, int len) {
    WaitConnEstablish();
    std::mutex msg_mux;
    std::unique_lock<std::mutex> msg_lck(msg_mux);
    std::condition_variable msg_cv;
    bool submitted = false;
    std::function<void(void)> msg_cmd = [&]() {
        unsigned char buf[len + LWS_PRE];
        memcpy(buf + LWS_PRE, data, len);
        lws_write(wsi_, buf + LWS_PRE, len, LWS_WRITE_BINARY);
        submitted = true;
        msg_cv.notify_all();
    };

    msg_queue_->Put(msg_cmd);
    lws_callback_on_writable(wsi_);
    lws_cancel_service(context_);
    msg_cv.wait(msg_lck, [&]() { return submitted == true; });

    return 0;
}

int WebSocketClient::Connect(std::string addr, int port, std::string path) {
    server_address_ = addr;
    port_ = port;
    path_ = path;

    lws_client_connect_info i;

    memset(&i, 0, sizeof(i));

    i.context = context_;
    i.port = port_;
    i.address = server_address_.c_str();
    i.path = path_.c_str();
    i.host = i.address;
    i.origin = i.address;
    i.ssl_connection = 0;
    i.protocol = "ws";
    i.local_protocol_name = "ws";

    std::mutex conn_mux;
    std::unique_lock<std::mutex> conn_lck(conn_mux);
    std::condition_variable conn_cv;
    int ret = -1;
    std::function<void(void)> client_conn = [&]() {
        wsi_ = lws_client_connect_via_info(&i);
        if (!wsi_) {
            poca_info("connect failed");
            ret = 1;
        } else {
            map_lws_wsc_[wsi_] = this;
            poca_info("connection %s:%d, wsi_: %p", i.address, i.port, wsi_);
            ret = 0;
        }
        conn_cv.notify_all();
    };

    close_.store(false);
    conn_queue_.Put(client_conn);
    conn_cv.wait(conn_lck, [&]() { return ret != -1; });
    return ret;
}

void WebSocketClient::Disconnect() {
    close_.store(true);
    lws_callback_on_writable(wsi_);
    lws_cancel_service(context_);
}