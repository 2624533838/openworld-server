// MVP：登录（发号）+ 移动 + 九宫格 AOI 广播 + 多玩家同屏。
// 在 echo 服务器基础上升级：收二进制帧 → 解析 Envelope → 交给 World 处理。

#include <asio.hpp>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include "connection.h"
#include "db_config.h"
#include "persistence.h"
#include "world.h"

using asio::ip::tcp;

class Server {
public:
    Server(asio::io_context& io, unsigned short port, openworld::World& world)
        : acceptor_(io, tcp::endpoint(tcp::v4(), port)), world_(world) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](std::error_code ec, tcp::socket socket) {
                if (!ec) {
                    auto conn = std::make_shared<openworld::Connection>(
                        std::move(socket),
                        [this](openworld::Connection& c, openworld::Envelope& e) {
                            world_.handle(c, e);
                        },
                        [this](openworld::Connection* c) {
                            world_.on_disconnect(c);
                        });
                    conn->start();
                }
                do_accept();
            });
    }

    tcp::acceptor acceptor_;
    openworld::World& world_;
};

int main(int argc, char* argv[]) {
    unsigned short port = 9000;
    bool no_db = false;
    std::int64_t heartbeat_timeout_ms = openworld::kHeartbeatTimeoutMs;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--no-db") {
            no_db = true;
        } else if (arg == "--heartbeat-timeout-ms") {
            if (i + 1 < argc) heartbeat_timeout_ms = std::stoll(argv[++i]);
        } else {
            port = static_cast<unsigned short>(std::stoi(arg));
        }
    }

    try {
        asio::io_context io;
        openworld::DbConfig db_cfg;  // 开发默认值，后续可换环境变量/配置
        openworld::PersistenceService persistence(io, db_cfg, !no_db);
        persistence.start();

        openworld::World world(io, persistence, heartbeat_timeout_ms);
        Server server(io, port, world);
        std::cout << "openworld server listening on 127.0.0.1:" << port
                  << (no_db ? " (仅内存，无持久化)" : "") << std::endl;

        // Ctrl+C / 关闭信号：停 io_context 让 io.run() 返回，再 drain 持久化队列
        // （保证退出前把最后一批掉线存档写完，不丢档）
        asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&io](std::error_code, int) { io.stop(); });

        io.run();
        persistence.stop();
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
