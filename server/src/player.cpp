#include "player.h"

#include <algorithm>
#include <utility>

namespace openworld {

Player::Player(std::string id, std::string name, float spawn_x, float spawn_y,
               std::shared_ptr<Connection> conn)
    : id_(std::move(id)),
      name_(std::move(name)),
      x_(spawn_x), y_(spawn_y),
      spawn_x_(spawn_x), spawn_y_(spawn_y),
      conn_(std::move(conn)) {}

void Player::set_velocity(float vx, float vy) {
    vx_ = vx;
    vy_ = vy;
}

void Player::integrate(float dt, float max_x, float max_y) {
    x_ = std::clamp(x_ + vx_ * dt, 0.0f, max_x);
    y_ = std::clamp(y_ + vy_ * dt, 0.0f, max_y);
}

void Player::set_cell(int cx, int cy) {
    cell_x_ = cx;
    cell_y_ = cy;
}

}  // namespace openworld
