# 移动同步与 AOI 技术方案

> 范围：MVP 的 Step 2（登录 + 移动 + 广播）与 Step 3（AOI 九宫格）。目标是把「多个玩家在同一张地图上移动、互相看得见」跑通，这是开放世界 RPG 服务端最有辨识度的能力。

## 1. 目标

- 玩家登录后进入一张 2D 地图，获得出生点
- 玩家移动，服务器**权威**计算位置并同步给周围玩家
- 只同步「视野内」的玩家（AOI），走近才看见、走远消失
- 单进程、单线程 io_context（`io.run()` 一个线程），先不做分布式

## 2. 总体架构

```
客户端(Java) ──TCP/protobuf──> Connection ──> World（业务核心）
                                      │
                                      ├── Player（在线玩家，绑定一条连接）
                                      ├── Grid 索引（地图分块，AOI 查询）
                                      └── tick 循环（asio::steady_timer，固定频率推进）
```

现状里 `handle_message` 是全局函数、无玩家身份。本阶段引入 **World** 作为业务核心：持有所有在线玩家 + 地图格子索引 + 游戏循环。

## 3. 地图分块（Grid）

- 地图坐标是连续的二维平面（单位：米），出生区在 `(0,0)` 附近
- 切成边长 `GRID_SIZE = 200` 的方格，玩家所在格由位置算出：

```
grid_x = floor(pos.x / GRID_SIZE)
grid_y = floor(pos.y / GRID_SIZE)
```

- World 维护格子索引：`unordered_map<GridKey, vector<Player*>>`，玩家跨格时从旧格移除、加入新格
- 用途：AOI 广播时只查周围 3×3 格子的玩家，避免 O(N) 全量遍历

## 4. 玩家实体（Player）

```cpp
class Player {
    std::string id_;
    std::shared_ptr<Connection> conn_;   // 绑定的连接
    Vec2 pos_;                            // 权威位置
    Vec2 dir_{0,0}; float speed_ = 0;    // 移动意图（由 MoveReq 更新）
    int grid_x_ = 0, grid_y_ = 0;        // 所在格
};
```

- 登录成功时创建 Player，`conn_` 保存连接；Connection 侧记录 `player_id_`，之后收到消息据此找到 Player
- 断线时从 World 移除，并广播离开

## 5. 移动同步（服务器权威）

### 5.1 模式选择

| 方案 | 说明 | 取舍 |
|------|------|------|
| 客户端预测 + 校正 | 客户端先动、服务器后校正 | 体验最好，但复杂，需处理回滚/插值 |
| **服务器权威 + tick**（采用） | 客户端只发「意图」，服务器按帧推进并广播 | 简单、防作弊，MVP 足够 |
| 点对点驱动 | 客户端发目标位置，服务器立即更新 | 无游戏循环，体验差 |

采用**服务器权威 + 固定 tick**，这是多人实时同步游戏的常见做法。

### 5.2 意图与推进

- 客户端发 `MoveReq(dir, speed, seq)`，只表达「想朝哪个方向、多快移动」，**不发坐标**
- 服务器收到后更新该玩家的 `dir_`/`speed_`（不直接改位置）
- tick 循环每 50ms（20Hz）对所有玩家推进：

```
pos += dir * speed * dt
```

- 客户端停止移动时发 `dir=(0,0)` 的 MoveReq，或靠服务器对 `speed=0` 的判断

### 5.3 权威校验（防作弊）

1. `speed` 上限 clamp（如 `MAX_SPEED = 100` 米/秒）
2. `dir` 服务器重新归一化，长度超过 1 就缩到 1（防止客户端发超大方向向量）
3. 位置越过地图边界时 clamp 到边界内

## 6. AOI 九宫格

### 6.1 视野定义

- 玩家视野 = 所在格 + 周围 8 格（3×3）
- 广播只发给视野内的玩家

### 6.2 跨格与 enter/leave

玩家每 tick 推进后检查格子是否变化；变化时对比新旧视野集合：

```
enter = 新视野 - 旧视野   → 把新进入玩家的当前位置发给双方（互见）
leave = 旧视野 - 新视野   → 通知离开
```

- **enter**：MVP 复用 `MoveBroadcast` 承载（服务器把新玩家当前 pos 广播给进入者，反过来同理）
- **leave**：MVP 由客户端基于「一段时间没收到该玩家广播」超时移除；后续可加 `EnterView`/`LeaveView` 消息把语义做显式

### 6.3 广播节流

不需要每 tick 全量广播（浪费带宽）。MVP 策略：

- 只广播「位置发生变化」的玩家（`dir != 0` 或位移超过阈值）
- 静止玩家不重复广播

## 7. 消息时序

```
登录：
  客户端 ──LoginReq──> 服务器
  服务器：创建 Player、分配 id 与出生点、加入 World
  客户端 <──LoginAck(ok, player_id, spawn)── 服务器

移动（持续）：
  客户端 ──MoveReq(dir, speed, seq)──> 服务器：更新意图
  ... 每 50ms tick ...
  服务器：推进位置 → 跨格检测 → AOI 广播
  视野内各客户端 <──MoveBroadcast(player_id, pos, vel)── 服务器

断线：
  客户端断开 → Connection 回调 → 服务器移除 Player → 广播离开
```

## 8. 类与数据结构

```cpp
class World {
    asio::io_context& io_;
    asio::steady_timer tick_timer_;
    std::unordered_map<std::string, std::shared_ptr<Player>> players_;
    std::unordered_map<GridKey, std::vector<Player*>> grid_index_;
    void start_tick();   // 启动 50ms 循环
    void tick();         // 推进 + 广播 + enter/leave
    Player* player_by_connection(Connection&);
};

class Player { /* 见第 4 节 */ };

// GridKey：把 (grid_x, grid_y) 打包成一个 64 位整数作 map key
```

改动点：

- `main.cpp`：`handle_message` 改为 `World` 的成员方法，登录建 Player，MoveReq 更新意图
- `connection.h`：增加 `player_id_`（登录后设置），供分发时定位 Player
- 新增 `world.h/cpp`、`player.h/cpp`

## 9. 边界与异常

| 场景 | 处理 |
|------|------|
| 未登录先发 MoveReq | 忽略（找不到 player_id 对应的 Player） |
| 速度超上限 / 方向非法 | clamp / 归一化 |
| 走到地图边界 | 位置 clamp |
| 玩家断线 | 从 World 与 grid_index 移除，广播 leave |
| 帧解码异常 | 断开连接（协议层已处理） |

## 10. 测试与验收

- **单元测试**（后续接 GoogleTest，MVP 先用断言脚本）：格子坐标计算、3×3 视野集合、意图推进数学（`pos += dir*speed*dt`）、边界 clamp
- **集成测试**（Python 假客户端，扩展现有 `test_protocol.py`）：
  1. 两个客户端登录，出生点相同
  2. A 发 MoveReq 移动，断言 B 收到 A 的 `MoveBroadcast` 且位置正确
  3. 让 A 移动足够远跨出 B 的 3×3 视野，断言 B 不再收到 A 的广播（leave 生效）
  4. 反向验证 enter：A 走回 B 视野，B 重新收到广播

## 11. 风险与后续

- **固定 tick 的定时器驱动**是新增的运行时模型，需注意 tick 内不能阻塞（所有 IO 仍异步）
- enter/leave 显式消息、断线重连、客户端插值留到 MVP 之后
- 持久化（Redis/MySQL）与网关拆分不在此阶段
