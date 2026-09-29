// 第 1 周里程碑：Asio echo 服务器
// 目标：验证工具链 + 跑通「TCP 连接 → 收数据 → 原样返回」的最小闭环。
// 后续 Reactor、粘包/拆包、protobuf 都在这之上迭代。

#include <asio.hpp>

#include <iostream>
#include <memory>

using asio::ip::tcp;

// 一条连接对应一个 Session：负责收、回写
class Session : public std::enable_shared_from_this<Session> {
public:
    explicit Session(tcp::socket socket) : socket_(std::move(socket)) {}

    void start() { do_read(); }

private:
    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some(asio::buffer(data_),
            [this, self](std::error_code ec, std::size_t length) {
                if (ec) {
                    return;  // 对方关闭或出错，直接结束本连接
                }
                do_write(length);
            });
    }

    void do_write(std::size_t length) {
        auto self(shared_from_this());
        asio::async_write(socket_, asio::buffer(data_, length),
            [this, self](std::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    do_read();
                }
            });
    }

    tcp::socket socket_;
    char data_[1024];
};

// 服务器：接受连接，每个连接交给一个 Session
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
                    std::make_shared<Session>(std::move(socket))->start();
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
        std::cout << "Echo server listening on 127.0.0.1:" << port << "\n";
        io.run();
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
