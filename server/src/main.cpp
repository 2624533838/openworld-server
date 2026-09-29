// 第 2 周里程碑：协议层 —— protobuf 编解码 + 长度前缀帧（粘包/拆包）。
// 在 echo 服务器基础上升级：收二进制帧 → 解析 Envelope → 按类型处理 → 回包。

#include <asio.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>

#include "connection.h"
#include "openworld.pb.h"

using asio::ip::tcp;
using namespace openworld;

namespace {

// 服务器当前时间（毫秒，自 epoch）
std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// 消息分发：按 Envelope.type 处理并回包
void handle_message(Connection& conn, Envelope& env) {
    Envelope resp;
    switch (env.type()) {
        case Envelope::LOGIN_REQ: {
            auto* ack = resp.mutable_login_ack();
            ack->set_ok(true);
            ack->set_player_id("player_1");  // Step 3 再接真实玩家管理
            ack->mutable_spawn()->set_x(100.0f);
            ack->mutable_spawn()->set_y(100.0f);
            resp.set_type(Envelope::LOGIN_ACK);
            conn.send(resp);
            break;
        }
        case Envelope::HEARTBEAT: {
            resp.set_type(Envelope::HEARTBEAT);
            resp.mutable_heartbeat()->set_server_time(now_ms());
            conn.send(resp);
            break;
        }
        default:
            // 其他类型（MOVE_REQ 等）留到后续里程碑处理
            break;
    }
}

}  // namespace

class Server {
public:
    Server(asio::io_context& io, unsigned short port)
        : acceptor_(io, tcp::endpoint(tcp::v4(), port)) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](std::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<Connection>(std::move(socket), handle_message)
                        ->start();
                }
                do_accept();
            });
    }

    tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
    unsigned short port = 9000;
    if (argc > 1) {
        port = static_cast<unsigned short>(std::stoi(argv[1]));
    }

    try {
        asio::io_context io;
        Server server(io, port);
        std::cout << "openworld server listening on 127.0.0.1:" << port << "\n";
        io.run();
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
