// MVP：登录（发号）+ 移动 + 九宫格 AOI 广播 + 多玩家同屏。
// 在 echo 服务器基础上升级：收二进制帧 → 解析 Envelope → 交给 World 处理。

#include <asio.hpp>

#include <cstdint>
#include <iostream>
#include <memory>

#include "connection.h"
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
    if (argc > 1) {
        port = static_cast<unsigned short>(std::stoi(argv[1]));
    }

    try {
        asio::io_context io;
        openworld::World world(io);
        Server server(io, port, world);
        std::cout << "openworld server listening on 127.0.0.1:" << port << std::endl;
        io.run();
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
