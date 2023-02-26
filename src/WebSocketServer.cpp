#include "WebSocketServer.h"

#include <libwebsockets.h>

#include "logger.h"

#define MAX_PAYLOAD_SIZE 8192

std::map<lws_context*, WebSocketServer*> WebSocketServer::server_ptr_;
std::mutex WebSocketServer::server_ptr_mux_;

WebSocketServer::WebSocketServer(WebSocketServerListener& listener) {
    listener_ = &listener;
    msg_queue_ = new RingFIFO<std::function<void(void)>>(10);
    callback_queue_ = new RingFIFO<std::function<void(void)>>(10);
}

WebSocketServer::~WebSocketServer() { delete msg_queue_; }

void WebSocketServer::CallbackEventLoop() {
    std::function<void(void)> callback;
    while (true) {
        callback = callback_queue_->Get();
        callback();
        if (close_.load()) break;
    }
}

int WebSocketServer::_LwsClientCallback(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len) {
    // poca_info("LwsClientCallback, wsi: %p, reason: %d", wsi, reason);
    lws_context* context = lws_get_context(wsi);
    WebSocketServer* server = nullptr;
    server_ptr_mux_.lock();
    if (server_ptr_.count(context)) {
        server = server_ptr_[context];
    }
    server_ptr_mux_.unlock();
    if (server == nullptr) {
        return lws_callback_http_dummy(wsi, reason, user, in, len);
    } else {
        return server->LwsClientCallback(wsi, reason, user, in, len);
    }
}

int WebSocketServer::LwsClientCallback(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len) {
    poca_info("LwsClientCallback, wsi: %p, reason: %d", wsi, reason);
    std::unique_lock<std::mutex> lck(mux_);
    std::function<void(void)> msg_submit;
    int first = 0, final = 0;
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            poca_info("client [%p] connect", wsi);
            receive_buf_[wsi] = new WebSocketReceiveBuffer();
            {
                std::function<void(void)> on_connect =
                    std::bind([&](int64_t user_id) { listener_->OnConnect(user_id); }, (int64_t)wsi);
                callback_queue_->Put(on_connect);
            }
            break;
        case LWS_CALLBACK_CLOSED:
            poca_info("client connect close, wsi: %p", wsi);
            receive_buf_.erase(wsi);
            {
                std::function<void(void)> on_close =
                    std::bind([&](int64_t user_id) { listener_->OnClose(user_id); }, (int64_t)wsi);
                callback_queue_->Put(on_close);
            }
            break;
        case LWS_CALLBACK_RECEIVE:
            // lws_rx_flow_control(wsi, 0);
            first = lws_is_first_fragment(wsi);
            final = lws_is_final_fragment(wsi);
            poca_info("Receive, wsi: %p, len: %d, first: %d, final: %d", wsi, len, first, final);
            if (first) {
                lws_rx_flow_control(wsi, 0);
                receive_buf_[wsi]->Clear();
            }
            receive_buf_[wsi]->Push(in, len);
            if (final) {
                lws_rx_flow_control(wsi, 1);
                bool submitted = false;
                std::mutex on_receive_mux;
                std::unique_lock<std::mutex> on_receive_lck(on_receive_mux);
                std::condition_variable on_receive_cv;
                std::function<void(void)> on_receive = std::bind(
                    [&](int64_t user_id, void* data, int len) {
                        uint8_t buf[len + 1];
                        memcpy(buf, data, len);
                        buf[len] = 0;
                        submitted = true;
                        on_receive_cv.notify_all();
                        listener_->OnReceive(user_id, buf, len);
                    },
                    (int64_t)wsi, receive_buf_[wsi]->GetPtr(), receive_buf_[wsi]->GetLength());
                callback_queue_->Put(on_receive);
                on_receive_cv.wait(on_receive_lck, [&]() { return submitted == true; });
                receive_buf_[wsi]->Clear();
            }
            lws_callback_on_writable(wsi);
            break;
        case LWS_CALLBACK_SERVER_WRITEABLE:
            if (msg_queue_->GetNoWait(msg_submit)) {
                msg_submit();
                lws_callback_on_writable(wsi);
            }
            break;
        default:
            break;
    }
    return lws_callback_http_dummy(wsi, reason, user, in, len);
}

int WebSocketServer::ListenAndServe(int port) {
    port_ = port;
    static lws_protocols protocols[] = {{
                                            "ws",
                                            &WebSocketServer::_LwsClientCallback,
                                            MAX_PAYLOAD_SIZE,
                                            MAX_PAYLOAD_SIZE,
                                        },
                                        {NULL, NULL, 0, 0, 0, NULL, 0}};
    lws_context_creation_info ctx_info = {0};
    ctx_info.port = port_;
    ctx_info.protocols = protocols;
    ctx_info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;

    context_ = lws_create_context(&ctx_info);
    if (!context_) {
        return -1;
    }
    server_ptr_mux_.lock();
    server_ptr_[context_] = this;
    server_ptr_mux_.unlock();

    callback_thread_ = std::thread(&WebSocketServer::CallbackEventLoop, this);

    while (!close_.load()) {
        lws_service(context_, 0);
        cv_.notify_all();
    }
    lws_context_destroy(context_);
    server_ptr_mux_.lock();
    server_ptr_.erase(context_);
    server_ptr_mux_.unlock();
    return 0;
}

int WebSocketServer::SendMessage(int64_t user_id, std::string& msg) {
    std::mutex msg_mux;
    std::unique_lock<std::mutex> msg_lck(msg_mux);
    std::condition_variable msg_cv;
    bool submitted = false;
    lws* wsi = (lws*)user_id;
    std::function<void(void)> msg_cmd = [&]() {
        unsigned char buf[msg.size() + LWS_PRE];
        memcpy(buf + LWS_PRE, msg.c_str(), msg.size());
        submitted = true;
        msg_cv.notify_all();
        lws_write(wsi, buf + LWS_PRE, msg.size(), LWS_WRITE_TEXT);
    };

    msg_queue_->Put(msg_cmd);
    lws_callback_on_writable(wsi);
    lws_cancel_service(context_);
    msg_cv.wait(msg_lck, [&]() { return submitted == true; });

    return 0;
}

int WebSocketServer::SendBinary(int64_t user_id, void* data, int len) {
    std::mutex msg_mux;
    std::unique_lock<std::mutex> msg_lck(msg_mux);
    std::condition_variable msg_cv;
    bool submitted = false;
    lws* wsi = (lws*)user_id;
    std::function<void(void)> msg_cmd = [&]() {
        unsigned char buf[len + LWS_PRE];
        memcpy(buf + LWS_PRE, data, len);
        submitted = true;
        msg_cv.notify_all();
        lws_write(wsi, buf + LWS_PRE, len, LWS_WRITE_BINARY);
    };

    msg_queue_->Put(msg_cmd);
    lws_callback_on_writable(wsi);
    lws_cancel_service(context_);
    msg_cv.wait(msg_lck, [&]() { return submitted == true; });

    return 0;
}

void WebSocketServer::Close() {
    close_.store(true);
    std::function<void(void)> do_nothing = []() {};
    callback_queue_->Put(do_nothing);
    lws_cancel_service(context_);
}