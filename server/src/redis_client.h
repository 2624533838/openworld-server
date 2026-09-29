// 极简 Redis RESP 客户端：只实现本项目用到的 SET / GET / DEL / PING。
// 阻塞式，只能在持久化工作线程里单线程使用（无需加锁）。

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openworld {

class RedisClient {
public:
    RedisClient() = default;
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    bool connect(const std::string& host, int port);
    bool ping();
    bool set(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);

private:
    bool send_command(const std::vector<std::string>& args);
    bool read_line(std::string& line);   // 读 \r\n 结尾的一行（不含结尾符）
    bool read_exact(char* out, int n);
    int read_char();                     // 读一个字节（带内部缓冲），EOF 返回 -1

    // SOCKET 在 Windows 上是 UINT_PTR，存成 intptr_t 避免头文件引入 winsock
    std::intptr_t sock_ = -1;
    char buf_[4096];
    int buf_len_ = 0;
    int buf_pos_ = 0;
};

}  // namespace openworld
