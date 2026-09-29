// 九宫格网格：格子索引 + 按 cell 存取 + AOI 遍历。
// 纯逻辑、与 asio/网络解耦，便于 GoogleTest 单测（AOI 正确性是核心考点）。
// cell = 地图切分成的方格；九宫格 = 以某格为中心的 3×3 相邻格。

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openworld {

// 格子坐标 → 唯一 key（高 32 位存 x，低 32 位存 y，支持负坐标）
inline std::int64_t cell_key(int cx, int cy) {
    return (static_cast<std::int64_t>(cx) << 32) | static_cast<std::uint32_t>(cy);
}

// 移动前后九宫格差集：返回 (进入的格子, 离开的格子)。
// 进入 = 新九宫格有、旧九宫格没有的格子；离开反之。用于跨格时的 enter/leave 判定。
inline std::pair<std::vector<std::pair<int, int>>, std::vector<std::pair<int, int>>>
aoi_diff(int old_cx, int old_cy, int new_cx, int new_cy) {
    std::vector<std::pair<int, int>> enter, leave;
    for (int gx = new_cx - 1; gx <= new_cx + 1; ++gx)
        for (int gy = new_cy - 1; gy <= new_cy + 1; ++gy)
            if (std::abs(gx - old_cx) > 1 || std::abs(gy - old_cy) > 1)
                enter.emplace_back(gx, gy);
    for (int gx = old_cx - 1; gx <= old_cx + 1; ++gx)
        for (int gy = old_cy - 1; gy <= old_cy + 1; ++gy)
            if (std::abs(gx - new_cx) > 1 || std::abs(gy - new_cy) > 1)
                leave.emplace_back(gx, gy);
    return {std::move(enter), std::move(leave)};
}

// 九宫格网格：cell → 实体指针列表。只存指针、不拥有实体。
template <typename T>
class Grid {
public:
    void add(T* item, int cx, int cy) { cells_[cell_key(cx, cy)].push_back(item); }

    void remove(T* item, int cx, int cy) {
        auto it = cells_.find(cell_key(cx, cy));
        if (it == cells_.end()) return;
        auto& v = it->second;
        v.erase(std::remove(v.begin(), v.end(), item), v.end());
        if (v.empty()) cells_.erase(it);
    }

    // 遍历单个格子内的所有实体
    template <typename F>
    void for_each_cell(int cx, int cy, F&& fn) const {
        auto it = cells_.find(cell_key(cx, cy));
        if (it == cells_.end()) return;
        for (T* item : it->second) fn(item);
    }

    // 遍历 (cx,cy) 九宫格内的所有实体（含中心格；是否排除自身由调用方决定）
    template <typename F>
    void for_each_in_aoi(int cx, int cy, F&& fn) const {
        for (int gx = cx - 1; gx <= cx + 1; ++gx)
            for (int gy = cy - 1; gy <= cy + 1; ++gy)
                for_each_cell(gx, gy, fn);
    }

private:
    std::unordered_map<std::int64_t, std::vector<T*>> cells_;
};

}  // namespace openworld
