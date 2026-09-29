// Player 推进数学单测：integrate 位移 + clamp 边界、set_velocity。
// 用 nullptr 连接构造（不碰真实网络，只测纯逻辑）。

#include <gtest/gtest.h>

#include "player.h"

namespace openworld {
namespace {

Player make_player(float x, float y) {
    return Player("player_1", "tester", x, y, nullptr);
}

TEST(PlayerTest, IntegrateMovesByVelDt) {
    Player p = make_player(0.0f, 0.0f);
    p.set_velocity(10.0f, 5.0f);
    p.integrate(0.5f, 1000.0f, 1000.0f);
    EXPECT_NEAR(p.x(), 5.0f, 1e-4f);   // 10 * 0.5
    EXPECT_NEAR(p.y(), 2.5f, 1e-4f);   // 5 * 0.5
}

TEST(PlayerTest, IntegrateClampUpperBound) {
    Player p = make_player(999.0f, 1000.0f);
    p.set_velocity(100.0f, 0.0f);
    p.integrate(1.0f, 1000.0f, 1000.0f);
    EXPECT_NEAR(p.x(), 1000.0f, 1e-4f);  // 被 clamp 在 [0, 1000]
    EXPECT_NEAR(p.y(), 1000.0f, 1e-4f);
}

TEST(PlayerTest, IntegrateClampLowerBound) {
    Player p = make_player(1.0f, 1.0f);
    p.set_velocity(-100.0f, -100.0f);
    p.integrate(1.0f, 1000.0f, 1000.0f);
    EXPECT_NEAR(p.x(), 0.0f, 1e-4f);  // 不会低于 0
    EXPECT_NEAR(p.y(), 0.0f, 1e-4f);
}

TEST(PlayerTest, SetVelocityStores) {
    Player p = make_player(0.0f, 0.0f);
    p.set_velocity(3.5f, -2.0f);
    EXPECT_FLOAT_EQ(p.vx(), 3.5f);
    EXPECT_FLOAT_EQ(p.vy(), -2.0f);
}

}  // namespace
}  // namespace openworld
