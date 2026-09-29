// World：单线程游戏世界。持有所有玩家 + 九宫格网格 + 30Hz tick。
// 消息处理、移动、AOI 进入/离开广播、断开清理都在这里。
// 全部逻辑跑在同一个 io_context 线程上，无需加锁。

#pragma once

#include <asio.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "connection.h"
#include "grid.h"
#include "persistence.h"
#include "player.h"

namespace openworld {

// 地图/移动常量
inline constexpr float kCellSize = 20.0f;     // 分块格子边长
inline constexpr float kMaxSpeed = 20.0f;     // 移动速度上限（防作弊）
inline constexpr float kWorldSize = 2000.0f;  // 世界边长（正方形）
inline constexpr std::int64_t kTickMs = 33;   // tick 间隔（~30Hz）
// 心跳超时：超过此时长没收到该连接任何消息则踢线（≈6 个 5s 心跳周期，宽松避免误踢）
inline constexpr std::int64_t kHeartbeatTimeoutMs = 30000;

class World {
public:
    World(asio::io_context& io, PersistenceService& persistence,
          std::int64_t heartbeat_timeout_ms = kHeartbeatTimeoutMs);

    // 消息入口（由 Connection 的 handler 调用）
    void handle(Connection& conn, Envelope& env);
    // 连接断开（由 Connection 的 on_close 调用）
    void on_disconnect(Connection* conn);

private:
    void on_login(Connection& conn, const LoginReq& req);
    void on_move(Connection& conn, const MoveReq& req);
    void on_heartbeat(Connection& conn);
    // 存档加载完成后的回调（在 io_context 线程执行）
    void complete_login(Connection& conn, std::optional<std::pair<float, float>> saved);
    void send_login_denied(Connection& conn, const std::string& error);

    void start_tick();
    void tick();

    // ---- 九宫格网格（纯逻辑在 Grid，见 grid.h）----
    // 遍历 p 九宫格内的其他玩家（不含 p 自身）
    template <typename F>
    void for_each_in_aoi(const Player& p, F&& fn) const;

    // ---- 消息构造与发送 ----
    void send_login_ack(Player& p);
    void send_enter_to(Player& viewer, Player& subject);
    void send_leave_to(Player& viewer, Player& subject);
    // 广播 env 给 p 九宫格内的所有玩家（含 p 自身，作为位置回传）
    void broadcast_to_aoi(const Player& p, Envelope& env);
    // 跨格子时的进入/离开（在 grid 更新之后调用）
    void on_cell_changed(Player& p, int old_cx, int old_cy);

    asio::io_context& io_;
    asio::steady_timer tick_;
    PersistenceService& persistence_;
    std::int64_t heartbeat_timeout_ms_;
    std::unordered_map<std::string, std::shared_ptr<Player>> players_;
    std::unordered_map<Connection*, std::shared_ptr<Player>> by_conn_;
    std::unordered_map<std::string, Player*> by_name_;           // 在线用户名 → 玩家
    std::unordered_set<std::string> pending_names_;              // 登录中占用的用户名
    std::unordered_map<Connection*, std::string> pending_login_; // 登录中的连接 → 用户名
    Grid<Player> grid_;                                          // 九宫格（见 grid.h）
    int next_id_ = 1;
    std::int64_t next_save_ms_ = 0;                              // 下次定时落盘时间
};

template <typename F>
void World::for_each_in_aoi(const Player& p, F&& fn) const {
    grid_.for_each_in_aoi(p.cell_x(), p.cell_y(), [&](Player* q) {
        if (q != &p) fn(q);
    });
}

}  // namespace openworld
