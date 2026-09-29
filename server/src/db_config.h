// 持久化层配置：MySQL 冷存档 + Redis 热数据。
// 连接参数从环境变量读取，未设置则用本地开发默认值。
// 密码等敏感项不写死在代码里（避免随源码提交进版本库），运行时注入：
//   OPENWORLD_MYSQL_HOST / _PORT / _USER / _PASS / _DB
//   OPENWORLD_REDIS_HOST / _PORT
// 例（PowerShell）： $env:OPENWORLD_MYSQL_PASS="你的密码"; ./openworld_server.exe

#pragma once

#include <cstdlib>
#include <string>

namespace openworld {

struct DbConfig {
    DbConfig();

    // MySQL（冷存档）
    std::string mysql_host;
    unsigned int mysql_port;
    std::string mysql_user;
    std::string mysql_pass;  // 无默认值，必须用环境变量注入
    std::string mysql_db;

    // Redis（热数据）
    std::string redis_host;
    int redis_port;
};

inline const char* env_or(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : fallback;
}

inline DbConfig::DbConfig() {
    mysql_host = env_or("OPENWORLD_MYSQL_HOST", "127.0.0.1");
    mysql_port = static_cast<unsigned int>(
        std::strtoul(env_or("OPENWORLD_MYSQL_PORT", "3306"), nullptr, 10));
    mysql_user = env_or("OPENWORLD_MYSQL_USER", "openworld");
    mysql_pass = env_or("OPENWORLD_MYSQL_PASS", "");
    mysql_db = env_or("OPENWORLD_MYSQL_DB", "openworld");
    redis_host = env_or("OPENWORLD_REDIS_HOST", "127.0.0.1");
    redis_port = static_cast<int>(
        std::strtol(env_or("OPENWORLD_REDIS_PORT", "6379"), nullptr, 10));
}

}  // namespace openworld
