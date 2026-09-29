#include "persistence.h"

// winsock2.h 必须先于 mysql.h（否则 mysql.h 引入的 winsock.h 会与 winsock2.h 冲突）
#include <winsock2.h>
#include <mysql.h>

#include <charconv>
#include <cstring>
#include <iostream>
#include <system_error>

namespace openworld {

namespace {

std::string redis_key(const std::string& username) {
    return "openworld:player:" + username;
}

// 用最短往返表示序列化浮点，避免 %.4f 丢精度
std::string encode_xy(float x, float y) {
    char bx[32], by[32];
    auto rx = std::to_chars(bx, bx + sizeof(bx), x);
    auto ry = std::to_chars(by, by + sizeof(by), y);
    return std::string(bx, rx.ptr) + "," + std::string(by, ry.ptr);
}

bool decode_xy(const std::string& s, float& x, float& y) {
    const auto comma = s.find(',');
    if (comma == std::string::npos) return false;
    float fx = 0.0f, fy = 0.0f;
    const char* first = s.data();
    const char* second = s.data() + comma + 1;
    const char* end = s.data() + s.size();
    auto r1 = std::from_chars(first, first + comma, fx);
    auto r2 = std::from_chars(second, end, fy);
    if (r1.ec != std::errc{} || r2.ec != std::errc{}) return false;
    x = fx;
    y = fy;
    return true;
}

}  // namespace

// ---- MysqlDb：RAII 包 MYSQL* + 预处理语句（防用户名 SQL 注入）----

class MysqlDb {
public:
    ~MysqlDb() {
        if (stmt_load_) mysql_stmt_close(stmt_load_);
        if (stmt_save_) mysql_stmt_close(stmt_save_);
        if (mysql_) mysql_close(mysql_);
    }

    bool connect(const DbConfig& cfg) {
        mysql_ = mysql_init(nullptr);
        if (!mysql_) return false;
        if (!mysql_real_connect(mysql_, cfg.mysql_host.c_str(), cfg.mysql_user.c_str(),
                                cfg.mysql_pass.c_str(), cfg.mysql_db.c_str(),
                                cfg.mysql_port, nullptr, 0)) {
            std::cerr << "MySQL 连接失败: " << mysql_error(mysql_) << "\n";
            return false;
        }
        mysql_set_character_set(mysql_, "utf8mb4");  // 中文用户名
        return true;
    }

    bool ensure_schema() {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS players ("
            " username VARCHAR(64) PRIMARY KEY,"
            " x FLOAT NOT NULL, y FLOAT NOT NULL,"
            " updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
        return mysql_query(mysql_, sql) == 0;
    }

    bool prepare() {
        stmt_load_ = mysql_stmt_init(mysql_);
        stmt_save_ = mysql_stmt_init(mysql_);
        const char* sql_load = "SELECT x, y FROM players WHERE username = ?";
        const char* sql_save =
            "INSERT INTO players (username, x, y) VALUES (?, ?, ?)"
            " ON DUPLICATE KEY UPDATE x = VALUES(x), y = VALUES(y)";
        return mysql_stmt_prepare(stmt_load_, sql_load, (unsigned long)std::strlen(sql_load)) == 0 &&
               mysql_stmt_prepare(stmt_save_, sql_save, (unsigned long)std::strlen(sql_save)) == 0;
    }

    std::optional<std::pair<float, float>> load(const std::string& username) {
        MYSQL_BIND param[1];
        std::memset(param, 0, sizeof(param));
        unsigned long name_len = (unsigned long)username.size();
        param[0].buffer_type = MYSQL_TYPE_STRING;
        param[0].buffer = const_cast<char*>(username.c_str());
        param[0].buffer_length = name_len;
        param[0].length = &name_len;
        if (mysql_stmt_bind_param(stmt_load_, param)) return std::nullopt;

        float x = 0.0f, y = 0.0f;
        MYSQL_BIND result[2];
        std::memset(result, 0, sizeof(result));
        result[0].buffer_type = MYSQL_TYPE_FLOAT;
        result[0].buffer = &x;
        result[1].buffer_type = MYSQL_TYPE_FLOAT;
        result[1].buffer = &y;
        if (mysql_stmt_bind_result(stmt_load_, result)) return std::nullopt;

        if (mysql_stmt_execute(stmt_load_)) return std::nullopt;
        const int rc = mysql_stmt_fetch(stmt_load_);
        mysql_stmt_reset(stmt_load_);  // 清状态，便于复用
        if (rc != 0) return std::nullopt;  // MYSQL_NO_DATA 或出错
        return std::make_pair(x, y);
    }

    bool save(const std::string& username, float x, float y) {
        MYSQL_BIND param[3];
        std::memset(param, 0, sizeof(param));
        unsigned long name_len = (unsigned long)username.size();
        param[0].buffer_type = MYSQL_TYPE_STRING;
        param[0].buffer = const_cast<char*>(username.c_str());
        param[0].buffer_length = name_len;
        param[0].length = &name_len;
        param[1].buffer_type = MYSQL_TYPE_FLOAT;
        param[1].buffer = &x;
        param[2].buffer_type = MYSQL_TYPE_FLOAT;
        param[2].buffer = &y;
        if (mysql_stmt_bind_param(stmt_save_, param)) return false;
        if (mysql_stmt_execute(stmt_save_)) return false;
        mysql_stmt_reset(stmt_save_);
        return true;
    }

private:
    MYSQL* mysql_ = nullptr;
    MYSQL_STMT* stmt_load_ = nullptr;
    MYSQL_STMT* stmt_save_ = nullptr;
};

// ---- PersistenceService ----

PersistenceService::PersistenceService(asio::io_context& io, const DbConfig& cfg, bool enabled)
    : io_(io), cfg_(cfg), enabled_(enabled) {}

PersistenceService::~PersistenceService() {
    stop();
}

void PersistenceService::start() {
    if (!enabled_) return;
    worker_ = std::thread([this] { worker_loop(); });
}

void PersistenceService::stop() {
    if (!enabled_) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void PersistenceService::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
}

void PersistenceService::async_load(
    std::string username,
    std::function<void(std::optional<std::pair<float, float>>)> cb) {
    if (!enabled_) {
        asio::post(io_, [cb = std::move(cb)] { cb(std::nullopt); });
        return;
    }
    enqueue([this, name = std::move(username), cb = std::move(cb)] {
        std::optional<std::pair<float, float>> pos;
        if (available_) {
            // 读穿：先查 Redis 热缓存，miss 再查 MySQL 并回填
            const std::string key = redis_key(name);
            if (auto v = redis_.get(key)) {
                float x, y;
                if (decode_xy(*v, x, y)) pos = std::make_pair(x, y);
            }
            if (!pos && mysql_) pos = mysql_->load(name);
            if (pos) redis_.set(key, encode_xy(pos->first, pos->second));
        }
        asio::post(io_, [cb = std::move(cb), pos] { cb(pos); });
    });
}

void PersistenceService::save_online(std::string username, float x, float y) {
    if (!enabled_) return;
    enqueue([this, name = std::move(username), x, y] {
        if (!available_) return;
        if (mysql_) mysql_->save(name, x, y);
        redis_.set(redis_key(name), encode_xy(x, y));
    });
}

void PersistenceService::save_offline(std::string username, float x, float y) {
    if (!enabled_) return;
    enqueue([this, name = std::move(username), x, y] {
        if (!available_) return;
        if (mysql_) mysql_->save(name, x, y);
        redis_.del(redis_key(name));
    });
}

void PersistenceService::worker_loop() {
    mysql_ = std::make_unique<MysqlDb>();
    bool ok = mysql_->connect(cfg_) && mysql_->ensure_schema() && mysql_->prepare();
    ok = ok && redis_.connect(cfg_.redis_host, cfg_.redis_port);
    available_ = ok;
    if (ok) {
        std::cout << "持久化已启用：MySQL 冷存档 + Redis 热数据\n";
    } else {
        std::cerr << "持久化不可用（MySQL/Redis 连接失败），降级为仅内存模式\n";
    }

    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty()) break;
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        task();
    }
}

}  // namespace openworld
