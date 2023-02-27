#include "WebSocketServer.h"

#include <libwebsockets.h>

#include "logger.h"

#define MAX_PAYLOAD_SIZE 8192

namespace poca_ws {
    std::map<lws_context*, WebSocketServer*> WebSocketServer::server_ptr_;
    std::mutex WebSocketServer::server_ptr_mux_;

    enum {
        ServerCallbackOnBinaryReceive = 0,
        ServerCallbackOnTextReceive,
        ServerCallbackOnConnect,
        ServerCallbackOnClose
    };

    WebSocketServer::WebSocketServer(WebSocketServerListener& listener) {
        listener_ = &listener;
        msg_queue_ = new RingFIFO<std::function<void(void)>>(10);
        const int receive_buf_ring_size = 10;
        ring_receive_buf_empty_ = new RingFIFO<WebSocketCallbackBuffer*>(receive_buf_ring_size);
        ring_receive_buf_full_ = new RingFIFO<WebSocketCallbackBuffer*>(receive_buf_ring_size);

        for (int i = 0; i < receive_buf_ring_size; ++i) {
            WebSocketCallbackBuffer* receive_buf = new WebSocketCallbackBuffer();
            ring_receive_buf_empty_->Put(receive_buf);
        }
    }

    WebSocketServer::~WebSocketServer() {
        delete msg_queue_;
        WebSocketCallbackBuffer* receive_buf;
        while (ring_receive_buf_empty_->GetNoWait(receive_buf)) {
            delete receive_buf;
        }
        while (ring_receive_buf_full_->GetNoWait(receive_buf)) {
            delete receive_buf;
        }
    }

    void WebSocketServer::CallbackEventLoop() {
        WebSocketCallbackBuffer* buf;
        while (true) {
            if (close_.load()) break;
            buf = ring_receive_buf_full_->Get();
            if (buf == nullptr) continue;
            buf->Lock();
            switch (buf->GetType()) {
                case ServerCallbackOnBinaryReceive:
                    listener_->OnReceive(buf->GetUserId(), buf->GetPtr(), buf->GetLength());
                    break;
                case ServerCallbackOnTextReceive:
                    listener_->OnReceive(buf->GetUserId(), buf->GetPtr(), buf->GetLength());
                    break;
                case ServerCallbackOnConnect:
                    listener_->OnConnect(buf->GetUserId());
                    break;
                case ServerCallbackOnClose:
                    listener_->OnClose(buf->GetUserId());
                    break;
                default:
                    break;
            }
            buf->Unlock();
            ring_receive_buf_empty_->Put(buf);
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
        int64_t user_id = int64_t(wsi);
        switch (reason) {
            case LWS_CALLBACK_ESTABLISHED:
                poca_info("client [%p] connect", wsi);
                {
                    WebSocketCallbackBuffer* on_connect = ring_receive_buf_empty_->Get();
                    on_connect->SetUserId(user_id);
                    on_connect->SetType(ServerCallbackOnConnect);
                    ring_receive_buf_full_->Put(on_connect);
                }
                break;
            case LWS_CALLBACK_CLOSED:
                poca_info("client connect close, wsi: %p", wsi);
                {
                    WebSocketCallbackBuffer* on_connect = ring_receive_buf_empty_->Get();
                    on_connect->SetUserId(user_id);
                    on_connect->SetType(ServerCallbackOnClose);
                    ring_receive_buf_full_->Put(on_connect);
                }
                break;
            case LWS_CALLBACK_RECEIVE: {
                int first = lws_is_first_fragment(wsi);
                int final = lws_is_final_fragment(wsi);
                int is_binary = lws_frame_is_binary(wsi);
                // poca_info("Receive, wsi: %p, len: %d, first: %d, final: %d", wsi, len, first, final);
                if (first) {
                    receive_buf_internal_[wsi] = ring_receive_buf_empty_->Get();
                }
                WebSocketCallbackBuffer* on_receive = receive_buf_internal_[wsi];
                on_receive->SetUserId(user_id);
                if (is_binary) {
                    on_receive->SetType(ServerCallbackOnBinaryReceive);
                } else {
                    on_receive->SetType(ServerCallbackOnTextReceive);
                }
                on_receive->Push(in, len);
                if (final) {
                    ring_receive_buf_full_->Put(on_receive);
                }
                lws_callback_on_writable(wsi);
            } break;
            case LWS_CALLBACK_SERVER_WRITEABLE:
                if (msg_queue_->GetNoWait(msg_submit)) {
                    msg_submit();
                    lws_callback_on_writable(wsi);
                }
                break;
            default:
                break;
        }
        return 0;
    }

    int WebSocketServer::ListenAndServe(int port) {
        port_ = port;
        static const lws_protocols protocols[] = {{
                                                      "ws",
                                                      &WebSocketServer::_LwsClientCallback,
                                                      MAX_PAYLOAD_SIZE,
                                                      MAX_PAYLOAD_SIZE,
                                                  },
                                                  {NULL, NULL, 0, 0, 0, NULL, 0}};
        lws_context_creation_info ctx_info = {0};
        ctx_info.port = port_;
        ctx_info.protocols = protocols;
        ctx_info.options =
            LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE | LWS_SERVER_OPTION_VALIDATE_UTF8;

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
        WebSocketCallbackBuffer* none = nullptr;
        ring_receive_buf_full_->Put(none);
        lws_cancel_service(context_);
    }
}  // namespace poca_ws