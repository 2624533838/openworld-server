# openworld-server 后端技术方案

> 本文档是**整个后端**的架构与设计总览，覆盖从网络层到持久化、从单进程到分布式的完整技术路线。
> 各模块的详细设计见 [tech-design.md](tech-design.md)（移动同步与 AOI）与 [protocol.md](protocol.md)（协议）。

## 1. 概述

后端是 2D 开放世界 RPG 的服务器：接受玩家连接、维护共享世界状态、权威同步多玩家、持久化玩家数据。核心要跑通的四个概念：**分线 / AOI 视野 / 状态同步 / 数据持久化**。

定位是「一个人能完成的微缩版」，不是复刻原神：每个概念都做完整、可讲，而不是每个都做大。

## 2. 技术栈与选型理由

| 层 | 技术 | 选型理由 |
|----|------|----------|
| 语言 | C++20 | 游戏服务端主流，性能与内存可控 |
| 网络 | Asio（standalone） | 跨平台（Windows IOCP / Linux epoll），替代手写 epoll；开发机是 Windows |
| 并发模型 | 单线程 io_context（Reactor） | 简单可靠，先单线程，后续 one-loop-per-thread 扩展 |
| 序列化 | protobuf（proto3） | 跨语言（后端 C++ / 前端 Java），强类型、版本演进友好 |
| 日志 | spdlog | header-only、异步、高性能 |
| 测试 | GoogleTest + 假客户端 | 单测覆盖纯逻辑，集成测试覆盖协议与同步 |
| 存储 | 内存 → Redis → MySQL | 分层落盘，见第 7 节 |
| 构建 | CMake + vendor 依赖 | 离线可构建，见第 10 节 |

## 3. 总体架构

### 3.1 分层

```
客户端(Java 2D)
   │  原生 TCP + protobuf（长度前缀帧）
   ▼
┌─────────────────────────────────────────┐
│ 网络层  Connection：连接生命周期、帧收发、粘包拆包 │
├─────────────────────────────────────────┤
│ 协议层  Envelope 编解码、消息分发            │
├─────────────────────────────────────────┤
│ 世界层  World / Player / 地图 / AOI / tick  │
├─────────────────────────────────────────┤
│ 持久化  内存权威 → Redis 热数据 → MySQL 冷存档 │
└─────────────────────────────────────────┘
```

### 3.2 演进路线

- **Phase 1（单进程）**：网络 + 协议 + 世界层合并一个进程，持久化只用内存 —— 即 MVP
- **Phase 2（持久化）**：接入 Redis/MySQL，玩家数据掉线不丢
- **Phase 3（分布式）**：拆网关（gateway）+ 逻辑服（game server），支撑多线

三个阶段是同一份代码逐步演进，不是重写。

## 4. 网络层

- **模型**：单线程 `io_context` 驱动所有异步 IO（`io.run()` 一个线程），等效 Reactor；每个连接一个 `Connection` 对象
- **生命周期**：`acceptor.async_accept` → `Connection::start` → `async_read_some` 循环收 → 断开自清理
- **帧协议**：`[4 字节大端长度][payload]`，解码器 `FrameDecoder` 累积缓冲、切完整帧，天然处理 TCP 粘包/半包；长度上限防御恶意超大帧
- **写队列**：`deque<string>` 支持连续多次 send，避免并发写串扰
- **多核扩展**：后续改为 one-loop-per-thread（N 个 io_context，连接哈希分配），世界层需要跨线程同步时才引入锁/消息队列

## 5. 协议层

- **Envelope 信封**：`type` 枚举 + `oneof body`，所有消息包一层，便于统一分发与扩展
- **消息**：登录（LoginReq/Ack）、移动（MoveReq/Broadcast）、心跳（Heartbeat），详见 [protocol.md](protocol.md)
- **分发**：Connection 收到完整帧 → `ParseFromString` → 按 type 路由到世界层 handler
- **原则**：服务器权威——客户端只发意图，位置/结果由服务器算好下发

## 6. 世界层（移动 + AOI）

核心设计见 [tech-design.md](tech-design.md)，要点：

- **World**：持有全部在线玩家 + 格子索引 + 固定 tick 循环（20Hz）
- **Player**：id、权威位置、移动意图、绑定连接、所在格
- **移动同步**：服务器权威 + tick 推进（`pos += dir*speed*dt`），客户端只发方向速度
- **AOI**：地图切 200 单位方格，九宫格视野，跨格时 enter/leave
- **防作弊**：速度上限 clamp、方向归一化、边界 clamp

## 7. 持久化层

### 7.1 三层模型

```
内存（权威，在线热数据） → Redis（热数据缓存/会话） → MySQL（离线存档）
```

- **内存**：在线玩家的实时状态（位置、属性）的权威来源
- **Redis**：登录会话 token、在线状态、跨服共享热数据
- **MySQL**：离线存档（角色等级、道具、位置快照），重登录时加载

### 7.2 落盘策略

- **触发**：定时落盘（如 5 分钟一轮）+ 关键事件即时落盘（登出、断线、重大变更）
- **脏标记**：只写发生过变更的数据，避免每轮全量写
- **异步**：落盘在独立线程/异步 IO，不阻塞游戏 tick

### 7.3 数据模型（草案）

- `player`：id、username、token、创建时间
- `player_profile`：player_id、level、exp、金币、位置（存档快照）

## 8. 分布式演进（网关 + 逻辑服）

| 角色 | 职责 |
|------|------|
| 网关 Gateway | 连接管理、鉴权、把客户端连接转发到逻辑服、负载均衡 |
| 逻辑服 Game Server | AOI、状态同步、玩法、持久化，每个逻辑服负责一批玩家 |

- **内部通信**：网关与逻辑服之间用 TCP + protobuf 的内部 RPC
- **分线**：按负载或区域把玩家分配到不同逻辑服；跨服移动/交互是后期难点，MVP 不做
- **好处**：连接层与业务层解耦，可水平扩展；连接是 IO 密集、业务是 CPU 密集，拆分后各自伸缩

## 9. 心跳与断线重连

当前实现是**简化版**（本版本），完整方案留作 Phase 3 演进。

- **心跳**：客户端定时发 Heartbeat，服务器回服务器时间戳（含客户端时间回显，可算 RTT）
- **活动刷新**：`Player::last_active_ms_`，任何已登录消息（move/heartbeat）到达就刷新（`World::handle` 里 `touch`）
- **断线检测**：`tick()` 里 `now - last_active_ms_ > heartbeat_timeout_ms_`（默认 30s ≈ 6 个心跳周期）就主动 `Connection::close()`，走 `on_disconnect` 完整清理（落盘 + 广播离开）。主动 close 是为了回收「客户端断电/断网」的半开连接——它不会主动发 FIN，服务器收不到 read 错误
- **断线重连（简化版）**：重连 = 同名重登 + 恢复存档。断线时 `save_offline` 已落盘位置，客户端（demo）2s 后自动重连、同名重登，服务器按用户名从 Redis/MySQL 恢复最后位置
- **演进方向（Phase 3）**：登录发 token，服务器据此免登录恢复会话；断线后一段宽限期内可重连，超时才归档落盘。`LoginReq.token` 字段已预留，本版本未用

## 10. 工程化

- **构建**：CMake；第三方依赖 vendor 到 `server/third_party/`（Asio 头文件、protobuf 编译产物），离线可构建；`build.bat` 用 vswhere 定位 VS 工具链
- **编码**：MSVC 加 `/utf-8`（中文 Windows 默认 GBK），静态 CRT 与 protobuf 保持一致
- **日志**：spdlog，分级（连接/业务/错误），后续按模块 logger
- **测试**：GoogleTest 单测纯逻辑（帧、格子、推进数学、clamp）+ Python 假客户端集成测试（登录/移动/广播/AOI）
- **压测**：模拟 N 个假客户端，测连接数、广播带宽、tick 耗时

## 11. 一条消息的完整旅程

```
客户端 MoveReq(dir, speed)
  → 网络层收字节、FrameDecoder 切帧
  → 协议层 ParseFromString 出 Envelope
  → 世界层：定位 Player、更新意图、防作弊校验
  → tick 推进位置、跨格检测
  → AOI：找到视野内玩家 → 构造 MoveBroadcast
  → 协议层序列化 + 网络层加长度前缀 → 发给各客户端
```

## 12. 风险与权衡

| 点 | 权衡 |
|----|------|
| 单线程 | 简单可靠，但单核瓶颈；MVP 够用，扩展走 one-loop-per-thread |
| 固定 tick | 逻辑清晰，但空转有开销；后期可按需降频或事件驱动 |
| enter/leave 简化 | MVP 复用 MoveBroadcast + 客户端超时，省协议改动；显式消息留后期 |
| 持久化分层 | 内存→Redis→MySQL 增加了复杂度，但换来掉线不丢档与可扩展性 |
| 网关拆分 | 架构更清晰但增加一次转发延迟；Phase 3 才做 |
