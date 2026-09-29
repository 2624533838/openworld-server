// 持久化层：独立工作线程 + 任务队列，把阻塞的 MySQL/Redis IO 与主循环解耦。
// 分层：内存（权威）→ Redis 热数据（在线缓存）→ MySQL 冷存档（离线真相源）。
// 登录用回调续传（worker 查完库 post 回 io_context）；保存 fire-and-forget。

#pragma once

#include <asio.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "db_config.h"
#include "redis_client.h"

namespace openworld {

class MysqlDb;  // 定义在 persistence.cpp，避免头文件引入 mysql.h

class PersistenceService {
public:
    PersistenceService(asio::io_context& io, const DbConfig& cfg, bool enabled);
    ~PersistenceService();

    PersistenceService(const PersistenceService&) = delete;
    PersistenceService& operator=(const PersistenceService&) = delete;

    void start();   // 启动工作线程
    void stop();    // 停止并 join

    // 登录时异步加载存档位置；cb 在 io_context 线程执行（无存档则 nullopt）
    void async_load(std::string username,
                    std::function<void(std::optional<std::pair<float, float>>)> cb);
    // 定时在线保存（MySQL upsert + Redis SET），fire-and-forget
    void save_online(std::string username, float x, float y);
    // 断开离线保存（MySQL upsert + Redis DEL），fire-and-forget
    void save_offline(std::string username, float x, float y);

private:
    void worker_loop();
    void enqueue(std::function<void()> task);

    asio::io_context& io_;
    DbConfig cfg_;
    bool enabled_;
    std::atomic<bool> available_{false};

    std::thread worker_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> tasks_;
    bool stop_ = false;

    std::unique_ptr<MysqlDb> mysql_;
    RedisClient redis_;
};

}  // namespace openworld
