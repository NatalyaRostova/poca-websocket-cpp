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
        const int receive_buf_ring_size = 5;
        const int send_buf_ring_size = 5;
        ring_receive_buf_empty_ = new RingFIFO<WebSocketFrameBuffer*>(receive_buf_ring_size);
        ring_receive_buf_full_ = new RingFIFO<WebSocketFrameBuffer*>(receive_buf_ring_size);

        for (int i = 0; i < receive_buf_ring_size; ++i) {
            WebSocketFrameBuffer* receive_buf = new WebSocketFrameBuffer();
            receive_buf->Clear();
            ring_receive_buf_empty_->Put(receive_buf);
        }

        ring_send_buf_empty_ = new RingFIFO<WebSocketFrameBuffer*>(send_buf_ring_size);
        ring_send_buf_full_ = new RingFIFO<WebSocketFrameBuffer*>(send_buf_ring_size);

        for (int i = 0; i < send_buf_ring_size; ++i) {
            WebSocketFrameBuffer* send_buf = new WebSocketFrameBuffer();
            send_buf->Clear();
            ring_send_buf_empty_->Put(send_buf);
        }
    }

    WebSocketServer::~WebSocketServer() {
        WebSocketFrameBuffer* receive_and_send_buf;
        while (ring_receive_buf_empty_->GetNoWait(receive_and_send_buf)) {
            if (receive_and_send_buf != nullptr) delete receive_and_send_buf;
        }
        while (ring_receive_buf_full_->GetNoWait(receive_and_send_buf)) {
            if (receive_and_send_buf != nullptr) delete receive_and_send_buf;
        }
        while (ring_send_buf_empty_->GetNoWait(receive_and_send_buf)) {
            if (receive_and_send_buf != nullptr) delete receive_and_send_buf;
        }
        while (ring_send_buf_full_->GetNoWait(receive_and_send_buf)) {
            if (receive_and_send_buf != nullptr) delete receive_and_send_buf;
        }
    }

    void WebSocketServer::CallbackEventLoop() {
        WebSocketFrameBuffer* buf;
        while (true) {
            if (close_.load()) break;
            buf = ring_receive_buf_full_->Get();
            if (buf == nullptr) continue;
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
            buf->Clear();
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
        int64_t user_id = int64_t(wsi);
        switch (reason) {
            case LWS_CALLBACK_ESTABLISHED:
                poca_info("client [%p] connect", wsi);
                {
                    WebSocketFrameBuffer* on_connect = ring_receive_buf_empty_->Get();
                    on_connect->SetUserId(user_id);
                    on_connect->SetType(ServerCallbackOnConnect);
                    ring_receive_buf_full_->Put(on_connect);
                }
                break;
            case LWS_CALLBACK_CLOSED:
                poca_info("client connect close, wsi: %p", wsi);
                {
                    WebSocketFrameBuffer* on_connect = ring_receive_buf_empty_->Get();
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
                WebSocketFrameBuffer* on_receive = receive_buf_internal_[wsi];
                on_receive->SetUserId(user_id);
                if (is_binary) {
                    on_receive->SetType(ServerCallbackOnBinaryReceive);
                } else {
                    on_receive->SetType(ServerCallbackOnTextReceive);
                }
                on_receive->Push((uint8_t*)in, len);
                if (final) {
                    ring_receive_buf_full_->Put(on_receive);
                }
                lws_callback_on_writable(wsi);
            } break;
            case LWS_CALLBACK_SERVER_WRITEABLE: {
                WebSocketFrameBuffer* msg_submit;
                if (ring_send_buf_full_->GetNoWait(msg_submit)) {
                    lws_write(wsi, msg_submit->GetPtr() + LWS_PRE, msg_submit->GetLength(),
                              (lws_write_protocol)msg_submit->GetType());
                    // lws_callback_on_writable(wsi);
                    msg_submit->Clear();
                    ring_send_buf_empty_->Put(msg_submit);
                }
            } break;
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
        lws* wsi = (lws*)user_id;
        WebSocketFrameBuffer* msg_frame = ring_send_buf_empty_->Get();
        msg_frame->Push(nullptr, LWS_PRE);
        msg_frame->Push((uint8_t*)msg.c_str(), (int)msg.size());
        msg_frame->SetUserId(user_id);
        msg_frame->SetType(LWS_WRITE_TEXT);

        ring_send_buf_full_->Put(msg_frame);
        lws_callback_on_writable(wsi);
        lws_cancel_service(context_);

        return 0;
    }

    int WebSocketServer::SendBinary(int64_t user_id, uint8_t* data, int len) {
        lws* wsi = (lws*)user_id;
        WebSocketFrameBuffer* msg_frame = ring_send_buf_empty_->Get();
        msg_frame->Push(nullptr, LWS_PRE);
        msg_frame->Push(data, len);
        msg_frame->SetUserId(user_id);
        msg_frame->SetType(LWS_WRITE_BINARY);

        ring_send_buf_full_->Put(msg_frame);
        lws_callback_on_writable(wsi);
        lws_cancel_service(context_);

        return 0;
    }

    void WebSocketServer::Close() {
        close_.store(true);
        WebSocketFrameBuffer* none = nullptr;
        ring_receive_buf_full_->Put(none);
        lws_cancel_service(context_);
    }
}  // namespace poca_ws