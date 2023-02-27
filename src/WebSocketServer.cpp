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

    WebSocketServer::WebSocketServer(WebSocketServerListener& listener) { listener_ = &listener; }

    WebSocketServer::~WebSocketServer() {
        WebSocketFrameBuffer* receive_and_send_buf;
        while (deque_receive_buf_empty_.GetNoWait(receive_and_send_buf)) {
            if (receive_and_send_buf != nullptr) delete receive_and_send_buf;
        }
        while (deque_receive_buf_full_.GetNoWait(receive_and_send_buf)) {
            if (receive_and_send_buf != nullptr) delete receive_and_send_buf;
        }
        while (deque_send_buf_empty_.GetNoWait(receive_and_send_buf)) {
            if (receive_and_send_buf != nullptr) delete receive_and_send_buf;
        }
        while (deque_send_buf_full_.GetNoWait(receive_and_send_buf)) {
            if (receive_and_send_buf != nullptr) delete receive_and_send_buf;
        }
    }

    void WebSocketServer::CallbackEventLoop() {
        WebSocketFrameBuffer* buf;
        while (true) {
            if (close_.load()) break;
            buf = deque_receive_buf_full_.Get();
            if (buf == nullptr) {
                continue;
            }
            switch (buf->GetType()) {
                case ServerCallbackOnBinaryReceive:
                    listener_->OnBinary(buf->GetUserId(), buf->GetPtr(), buf->GetLength());
                    break;
                case ServerCallbackOnTextReceive: {
                    std::string msg((char*)buf->GetPtr());
                    listener_->OnText(buf->GetUserId(), msg);
                } break;
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
            deque_receive_buf_empty_.Put(buf);
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
        // poca_info("LwsClientCallback, wsi: %p, reason: %d", wsi, reason);
        int64_t user_id = int64_t(wsi);
        switch (reason) {
            case LWS_CALLBACK_ESTABLISHED:
                poca_info("client [%p] connect", wsi);
                {
                    WebSocketFrameBuffer* on_connect;
                    if (!deque_receive_buf_empty_.GetNoWait(on_connect)) {
                        on_connect = new WebSocketFrameBuffer();
                    }
                    on_connect->SetUserId(user_id);
                    on_connect->SetType(ServerCallbackOnConnect);
                    deque_receive_buf_full_.Put(on_connect);
                }
                break;
            case LWS_CALLBACK_CLOSED:
                poca_info("client connect close, wsi: %p", wsi);
                {
                    WebSocketFrameBuffer* on_close;
                    if (!deque_receive_buf_empty_.GetNoWait(on_close)) {
                        on_close = new WebSocketFrameBuffer();
                    }
                    on_close->SetUserId(user_id);
                    on_close->SetType(ServerCallbackOnClose);
                    deque_receive_buf_full_.Put(on_close);
                }
                break;
            case LWS_CALLBACK_RECEIVE: {
                int first = lws_is_first_fragment(wsi);
                int final = lws_is_final_fragment(wsi);
                int is_binary = lws_frame_is_binary(wsi);
                // poca_info("Receive, wsi: %p, len: %d, first: %d, final: %d", wsi, len, first, final);
                WebSocketFrameBuffer* on_receive;
                if (first) {
                    if (!deque_receive_buf_empty_.GetNoWait(on_receive)) {
                        on_receive = new WebSocketFrameBuffer();
                    }
                    on_receive->Clear();
                    receive_buf_internal_[wsi] = on_receive;
                } else {
                    on_receive = receive_buf_internal_[wsi];
                }
                on_receive->Push((uint8_t*)in, len);
                if (final) {
                    on_receive->SetUserId(user_id);
                    if (is_binary) {
                        on_receive->SetType(ServerCallbackOnBinaryReceive);
                    } else {
                        on_receive->SetType(ServerCallbackOnTextReceive);
                    }
                    deque_receive_buf_full_.Put(on_receive);
                }
            } break;
            case LWS_CALLBACK_SERVER_WRITEABLE: {
                WebSocketFrameBuffer* msg_submit;
                if (deque_send_buf_full_.GetNoWait(msg_submit)) {
                    lws_write(wsi, msg_submit->GetPtr() + LWS_PRE, msg_submit->GetLength(),
                              (lws_write_protocol)msg_submit->GetType());
                    msg_submit->Clear();
                    deque_send_buf_empty_.Put(msg_submit);
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
        WebSocketFrameBuffer* msg_frame;
        if (!deque_send_buf_empty_.GetNoWait(msg_frame)) {
            msg_frame = new WebSocketFrameBuffer();
        }
        msg_frame->Push(nullptr, LWS_PRE);
        msg_frame->Push((uint8_t*)msg.c_str(), (int)msg.size());
        msg_frame->SetUserId(user_id);
        msg_frame->SetType(LWS_WRITE_TEXT);

        deque_send_buf_full_.Put(msg_frame);
        lws_callback_on_writable(wsi);
        lws_cancel_service(context_);

        return 0;
    }

    int WebSocketServer::SendBinary(int64_t user_id, uint8_t* data, int len) {
        lws* wsi = (lws*)user_id;
        WebSocketFrameBuffer* msg_frame;
        if (!deque_send_buf_empty_.GetNoWait(msg_frame)) {
            msg_frame = new WebSocketFrameBuffer();
        }
        msg_frame->Push(nullptr, LWS_PRE);
        msg_frame->Push(data, len);
        msg_frame->SetUserId(user_id);
        msg_frame->SetType(LWS_WRITE_BINARY);

        deque_send_buf_full_.Put(msg_frame);
        lws_callback_on_writable(wsi);
        lws_cancel_service(context_);

        return 0;
    }

    void WebSocketServer::Close() {
        close_.store(true);
        WebSocketFrameBuffer* none = nullptr;
        deque_receive_buf_full_.Put(none);
        lws_cancel_service(context_);
    }
}  // namespace poca_ws