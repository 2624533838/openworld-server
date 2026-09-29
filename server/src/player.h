// Player 实体：一条会话 = 连接 + 游戏状态（位置/速度）。
// 只承载数据与发送；移动/AOI/广播逻辑在 World。

#pragma once

#include <memory>
#include <string>

#include "connection.h"

namespace openworld {

class Player {
public:
    Player(std::string id, std::string name, float spawn_x, float spawn_y,
           std::shared_ptr<Connection> conn);

    const std::string& id() const { return id_; }
    const std::string& name() const { return name_; }
    float x() const { return x_; }
    float y() const { return y_; }
    float vx() const { return vx_; }
    float vy() const { return vy_; }
    float spawn_x() const { return spawn_x_; }
    float spawn_y() const { return spawn_y_; }
    bool dirty() const { return dirty_; }
    int cell_x() const { return cell_x_; }
    int cell_y() const { return cell_y_; }

    // 设置速度（方向已归一化、速度已限速）
    void set_velocity(float vx, float vy);
    // tick 积分：pos += vel*dt，并 clamp 到 [0, max]
    void integrate(float dt, float max_x, float max_y);
    void set_cell(int cx, int cy);
    void mark_dirty() { dirty_ = true; }
    void clear_dirty() { dirty_ = false; }

    // 便捷发送
    void send(Envelope& env) { conn_->send(env); }

private:
    std::string id_;
    std::string name_;
    float x_, y_;       // 当前位置
    float spawn_x_, spawn_y_;
    float vx_ = 0.0f, vy_ = 0.0f;
    bool dirty_ = false;
    int cell_x_ = 0, cell_y_ = 0;
    std::shared_ptr<Connection> conn_;
};

}  // namespace openworld
