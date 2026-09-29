#include "connection.h"

#include <iostream>

namespace openworld {

Connection::Connection(tcp::socket socket, MessageHandler handler, CloseHandler on_close)
    : socket_(std::move(socket)),
      handler_(std::move(handler)),
      on_close_(std::move(on_close)) {}

void Connection::start() {
    do_read();
}

void Connection::send(const Envelope& env) {
    std::string payload;
    if (!env.SerializeToString(&payload)) {
        std::cerr << "Connection::send: SerializeToString failed\n";
        return;
    }
    const bool write_in_progress = !write_queue_.empty();
    write_queue_.push_back(encode_frame(payload));
    if (!write_in_progress) {
        do_write();
    }
}

void Connection::do_read() {
    auto self(shared_from_this());
    socket_.async_read_some(asio::buffer(data_, sizeof(data_)),
        [this, self](std::error_code ec, std::size_t length) {
            if (ec) {
                std::cerr << "connection closed (read): " << ec.message() << "\n";
                if (on_close_) on_close_(this);
                return;  // 对方关闭或出错，结束本连接
            }
            try {
                decoder_.feed(data_, length);
                std::string payload;
                while (decoder_.pop(payload)) {
                    Envelope env;
                    if (env.ParseFromString(payload)) {
                        handler_(*this, env);
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Connection: protocol error: " << e.what() << "\n";
                if (on_close_) on_close_(this);
                return;  // 协议错误，断开连接
            }
            do_read();
        });
}

void Connection::do_write() {
    auto self(shared_from_this());
    asio::async_write(socket_, asio::buffer(write_queue_.front()),
        [this, self](std::error_code ec, std::size_t) {
            if (ec) {
                std::cerr << "connection closed (write): " << ec.message() << "\n";
                if (on_close_) on_close_(this);
                return;
            }
            write_queue_.pop_front();
            if (!write_queue_.empty()) {
                do_write();
            }
        });
}

}  // namespace openworld
