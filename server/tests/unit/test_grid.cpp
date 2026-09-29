// AOI 九宫格网格单测：cell_key 唯一性、增删、九宫格遍历、跨格 enter/leave 差集。
// Grid 是纯逻辑（与 asio 解耦），这里是 spec「AOI」的核心正确性验证。

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "grid.h"

namespace openworld {
namespace {

// 测试用实体：只带一个 id
struct Entity {
    int id;
};

// 收集 (cx,cy) 九宫格内所有实体 id，排序后返回（便于断言）
std::vector<int> collect_aoi(const Grid<Entity>& grid, int cx, int cy) {
    std::vector<int> ids;
    grid.for_each_in_aoi(cx, cy, [&](Entity* e) { ids.push_back(e->id); });
    std::sort(ids.begin(), ids.end());
    return ids;
}

TEST(CellKeyTest, UniquenessAndNegativeCoords) {
    EXPECT_NE(cell_key(0, 0), cell_key(0, 1));
    EXPECT_NE(cell_key(0, 0), cell_key(1, 0));
    EXPECT_NE(cell_key(-1, -1), cell_key(0, 0));
    // 负坐标不碰撞：x 有符号放高 32 位，y 无符号放低 32 位
    EXPECT_NE(cell_key(-5, 3), cell_key(3, -5));
}

TEST(GridTest, AddRemoveAndForEachCell) {
    Grid<Entity> grid;
    Entity a{1}, b{2};
    grid.add(&a, 0, 0);
    grid.add(&b, 0, 0);

    std::vector<int> ids;
    grid.for_each_cell(0, 0, [&](Entity* e) { ids.push_back(e->id); });
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids, (std::vector<int>{1, 2}));

    grid.remove(&a, 0, 0);
    ids.clear();
    grid.for_each_cell(0, 0, [&](Entity* e) { ids.push_back(e->id); });
    EXPECT_EQ(ids, (std::vector<int>{2}));

    grid.remove(&b, 0, 0);  // 清空后格子被移除
    ids.clear();
    grid.for_each_cell(0, 0, [&](Entity* e) { ids.push_back(e->id); });
    EXPECT_TRUE(ids.empty());
}

TEST(GridTest, ForEachInAoiHitsNineGridOnly) {
    Grid<Entity> grid;
    Entity center{1}, near{2}, diag{3}, far{4};
    grid.add(&center, 0, 0);
    grid.add(&near, 1, 0);   // 九宫格内
    grid.add(&diag, -1, 1);  // 九宫格对角
    grid.add(&far, 5, 5);    // 远处，不应命中

    EXPECT_EQ(collect_aoi(grid, 0, 0), (std::vector<int>{1, 2, 3}));
}

TEST(AoiDiffTest, MoveRightByOneCell) {
    auto [enter, leave] = aoi_diff(0, 0, 1, 0);
    EXPECT_EQ(enter.size(), 3u);
    EXPECT_EQ(leave.size(), 3u);
    for (auto [gx, gy] : enter) EXPECT_EQ(gx, 2);   // 新九宫格右侧一列
    for (auto [gx, gy] : leave) EXPECT_EQ(gx, -1);  // 旧九宫格左侧一列
}

TEST(AoiDiffTest, MoveDiagonallyLShape) {
    auto [enter, leave] = aoi_diff(0, 0, 1, 1);
    EXPECT_EQ(enter.size(), 5u);  // 对角移动：进入/离开各 5 格（L 形）
    EXPECT_EQ(leave.size(), 5u);
}

TEST(AoiDiffTest, NoMoveEmpty) {
    auto [enter, leave] = aoi_diff(3, 3, 3, 3);
    EXPECT_TRUE(enter.empty());
    EXPECT_TRUE(leave.empty());
}

}  // namespace
}  // namespace openworld
