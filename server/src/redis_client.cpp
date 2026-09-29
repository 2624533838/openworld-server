#include "redis_client.h"

#include <winsock2.h>
#include <ws2tcpip.h>

namespace openworld {

namespace {

// 把命令编码成 RESP 数组：*N\r\n$len\r\narg\r\n...
std::string encode_command(const std::vector<std::string>& args) {
    std::string out = "*" + std::to_string(args.size()) + "\r\n";
    for (const auto& a : args) {
        out += "$" + std::to_string(a.size()) + "\r\n";
        out += a;
        out += "\r\n";
    }
    return out;
}

}  // namespace

RedisClient::~RedisClient() {
    if (sock_ >= 0) {
        ::closesocket(static_cast<SOCKET>(sock_));
        sock_ = -1;
    }
}

bool RedisClient::connect(const std::string& host, int port) {
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        ::closesocket(s);
        return false;
    }
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        ::closesocket(s);
        return false;
    }

    sock_ = static_cast<std::intptr_t>(s);
    buf_len_ = 0;
    buf_pos_ = 0;
    return true;
}

bool RedisClient::ping() {
    if (!send_command({"PING"})) return false;
    std::string line;
    return read_line(line) && line == "+PONG";
}

bool RedisClient::set(const std::string& key, const std::string& value) {
    if (!send_command({"SET", key, value})) return false;
    std::string line;
    return read_line(line) && !line.empty() && line[0] == '+';
}

bool RedisClient::del(const std::string& key) {
    if (!send_command({"DEL", key})) return false;
    std::string line;
    return read_line(line) && !line.empty() && line[0] == ':';
}

std::optional<std::string> RedisClient::get(const std::string& key) {
    if (!send_command({"GET", key})) return std::nullopt;
    std::string line;
    if (!read_line(line) || line.empty() || line[0] != '$') return std::nullopt;
    const int len = std::stoi(line.substr(1));
    if (len < 0) return std::nullopt;  // $-1 表示 key 不存在
    std::string data(static_cast<std::size_t>(len), '\0');
    if (!read_exact(data.data(), len)) return std::nullopt;
    char crlf[2];
    if (!read_exact(crlf, 2)) return std::nullopt;
    return data;
}

bool RedisClient::send_command(const std::vector<std::string>& args) {
    const std::string cmd = encode_command(args);
    const int total = static_cast<int>(cmd.size());
    int sent = 0;
    while (sent < total) {
        const int r = ::send(static_cast<SOCKET>(sock_), cmd.data() + sent, total - sent, 0);
        if (r == SOCKET_ERROR || r == 0) return false;
        sent += r;
    }
    return true;
}

bool RedisClient::read_line(std::string& line) {
    line.clear();
    for (;;) {
        const int c = read_char();
        if (c < 0) return false;
        if (c == '\r') {
            if (read_char() != '\n') return false;
            return true;
        }
        line.push_back(static_cast<char>(c));
    }
}

bool RedisClient::read_exact(char* out, int n) {
    for (int i = 0; i < n; ++i) {
        const int c = read_char();
        if (c < 0) return false;
        out[i] = static_cast<char>(c);
    }
    return true;
}

int RedisClient::read_char() {
    if (buf_pos_ >= buf_len_) {
        const int r = ::recv(static_cast<SOCKET>(sock_), buf_, sizeof(buf_), 0);
        if (r <= 0) return -1;
        buf_pos_ = 0;
        buf_len_ = r;
    }
    return static_cast<unsigned char>(buf_[buf_pos_++]);
}

}  // namespace openworld
