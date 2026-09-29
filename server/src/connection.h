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
// 连接关闭（对方断开或出错）时的回调，参数是本连接指针，用于上层清理
using CloseHandler = std::function<void(class Connection*)>;

class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(tcp::socket socket, MessageHandler handler, CloseHandler on_close);

    void start();

    // 发送一个 Envelope（自动加长度前缀）
    void send(const Envelope& env);

    // 主动关闭连接（如心跳超时踢线）。关闭会取消未完成的异步读写，
    // 其完成回调以 operation_aborted 触发 on_close_，由上层做清理。
    void close();

private:
    void do_read();
    void do_write();

    tcp::socket socket_;
    MessageHandler handler_;
    CloseHandler on_close_;
    FrameDecoder decoder_;
    char data_[4096];
    std::deque<std::string> write_queue_;  // 写队列，支持连续多次 send
};

}  // namespace openworld
