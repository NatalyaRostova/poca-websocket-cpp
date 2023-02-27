#include "WebSocketClient.h"

#include <libwebsockets.h>

#include "logger.h"

#define MAX_PAYLOAD_SIZE 8192

namespace poca_ws {
    std::once_flag WebSocketClient::once_flag_;
    std::thread WebSocketClient::worker_thread_;
    std::atomic_bool WebSocketClient::running_;
    std::mutex WebSocketClient::mux_;
    bool WebSocketClient::protocol_inited_ = false;
    std::condition_variable WebSocketClient::cv_;
    lws_context *WebSocketClient::context_;
    std::map<lws *, WebSocketClient *> WebSocketClient::map_lws_wsc_;
    SyncDeque<std::function<void(void)>> WebSocketClient::conn_queue_;

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
        receive_buf_internal_ = new WebSocketFrameBuffer();
    }

    WebSocketClient::~WebSocketClient() {
        delete receive_buf_internal_;
        WebSocketFrameBuffer *send_buf;
        while (deque_send_buf_empty_.GetNoWait(send_buf)) {
            if (send_buf != nullptr) delete send_buf;
        }
        while (deque_send_buf_full_.GetNoWait(send_buf)) {
            if (send_buf != nullptr) delete send_buf;
        }
    }

    int WebSocketClient::LwsClientCallback(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len) {
        // poca_info("LwsClientCallback, wsi: %p, reason: %d", wsi, reason);
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
                if (first) {
                    map_lws_wsc_[wsi]->receive_buf_internal_->Clear();
                }
                map_lws_wsc_[wsi]->receive_buf_internal_->Push((uint8_t *)in, len);
                if (final) {
                    int is_binary = lws_frame_is_binary(wsi);
                    if (is_binary) {
                        map_lws_wsc_[wsi]->listener_->OnBinary(map_lws_wsc_[wsi]->receive_buf_internal_->GetPtr(),
                                                               map_lws_wsc_[wsi]->receive_buf_internal_->GetLength());
                    } else {
                        std::string msg((char *)map_lws_wsc_[wsi]->receive_buf_internal_->GetPtr());
                        map_lws_wsc_[wsi]->listener_->OnText(msg);
                    }
                    map_lws_wsc_[wsi]->receive_buf_internal_->Clear();
                }
                break;
            case LWS_CALLBACK_CLIENT_WRITEABLE:
                if (map_lws_wsc_[wsi]->close_.load() == true) {
                    return -1;
                }
                WebSocketFrameBuffer *msg_submit;
                if (map_lws_wsc_[wsi]->deque_send_buf_full_.GetNoWait(msg_submit)) {
                    lws_write(wsi, msg_submit->GetPtr() + LWS_PRE, msg_submit->GetLength(),
                              (lws_write_protocol)msg_submit->GetType());
                    msg_submit->Clear();
                    map_lws_wsc_[wsi]->deque_send_buf_empty_.Put(msg_submit);
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
        WebSocketFrameBuffer *msg_frame;
        if (!deque_send_buf_empty_.GetNoWait(msg_frame)) {
            msg_frame = new WebSocketFrameBuffer();
        }
        msg_frame->Push(nullptr, LWS_PRE);
        msg_frame->Push((uint8_t *)msg.c_str(), (int)msg.size());
        msg_frame->SetType(LWS_WRITE_TEXT);

        deque_send_buf_full_.Put(msg_frame);
        lws_callback_on_writable(wsi_);
        lws_cancel_service(context_);

        return 0;
    }

    int WebSocketClient::SendBinary(uint8_t *data, int len) {
        WaitConnEstablish();
        WebSocketFrameBuffer *msg_frame;
        if (!deque_send_buf_empty_.GetNoWait(msg_frame)) {
            msg_frame = new WebSocketFrameBuffer();
        }
        msg_frame->Push(nullptr, LWS_PRE);
        msg_frame->Push(data, len);
        msg_frame->SetType(LWS_WRITE_BINARY);

        deque_send_buf_full_.Put(msg_frame);
        lws_callback_on_writable(wsi_);
        lws_cancel_service(context_);
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
}  // namespace poca_ws