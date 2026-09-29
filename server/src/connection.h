// 一条 TCP 连接：收帧 → 反序列化 Envelope → 交给 handler；
// 再把 handler 要回的 Envelope 序列化 → 加长度前缀 → 发出去。

#pragma once

#include <asio.hpp>

#include <deque>
#include <functional>
#include <memory>
#include <string>

#include "frame.h"
#include "openworld.pb.h"

namespace openworld {

using asio::ip::tcp;

// 收到一条完整消息时的回调，第一个参数是本连接（便于回包）
using MessageHandler = std::function<void(class Connection&, Envelope&)>;

class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(tcp::socket socket, MessageHandler handler);

    void start();

    // 发送一个 Envelope（自动加长度前缀）
    void send(Envelope& env);

private:
    void do_read();
    void do_write();

    tcp::socket socket_;
    MessageHandler handler_;
    FrameDecoder decoder_;
    char data_[4096];
    std::deque<std::string> write_queue_;  // 写队列，支持连续多次 send
};

}  // namespace openworld
