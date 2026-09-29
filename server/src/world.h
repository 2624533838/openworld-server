// World：单线程游戏世界。持有所有玩家 + 九宫格网格 + 30Hz tick。
// 消息处理、移动、AOI 进入/离开广播、断开清理都在这里。
// 全部逻辑跑在同一个 io_context 线程上，无需加锁。

#pragma once

#include <asio.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "connection.h"
#include "player.h"

namespace openworld {

// 地图/移动常量
inline constexpr float kCellSize = 20.0f;     // 分块格子边长
inline constexpr float kMaxSpeed = 20.0f;     // 移动速度上限（防作弊）
inline constexpr float kWorldSize = 2000.0f;  // 世界边长（正方形）
inline constexpr std::int64_t kTickMs = 33;   // tick 间隔（~30Hz）

class World {
public:
    explicit World(asio::io_context& io);

    // 消息入口（由 Connection 的 handler 调用）
    void handle(Connection& conn, Envelope& env);
    // 连接断开（由 Connection 的 on_close 调用）
    void on_disconnect(Connection* conn);

private:
    void on_login(Connection& conn, const LoginReq& req);
    void on_move(Connection& conn, const MoveReq& req);
    void on_heartbeat(Connection& conn);

    void start_tick();
    void tick();

    // ---- 九宫格网格 ----
    static std::int64_t cell_key(int cx, int cy);
    void grid_add(Player* p);
    void grid_remove(Player* p);
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
    std::unordered_map<std::string, std::shared_ptr<Player>> players_;
    std::unordered_map<Connection*, std::shared_ptr<Player>> by_conn_;
    std::unordered_map<std::int64_t, std::vector<Player*>> grid_;
    int next_id_ = 1;
};

template <typename F>
void World::for_each_in_aoi(const Player& p, F&& fn) const {
    for (int cx = p.cell_x() - 1; cx <= p.cell_x() + 1; ++cx) {
        for (int cy = p.cell_y() - 1; cy <= p.cell_y() + 1; ++cy) {
            auto it = grid_.find(cell_key(cx, cy));
            if (it == grid_.end()) continue;
            for (Player* q : it->second) {
                if (q != &p) fn(q);
            }
        }
    }
}

}  // namespace openworld
