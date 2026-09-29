# openworld-server

2D 开放世界 RPG 服务器—— 一个人能完成的开放世界RPG服务端微缩版。

**目标**：把开放世界RPG服务端的四个核心概念——**分线、AOI 视野、状态同步、数据持久化**——在可完成的尺度下完整跑通一遍，作为游戏服务端的作品。。

## 技术栈

| 端 | 技术 |
|---|---|
| 服务器 | C++20 · Asio · 自实现 Reactor · protobuf · spdlog · GoogleTest · Redis/MySQL |
| 客户端 | Java（，硬约束：原生 TCP + protobuf）+ 网页演示客户端（demo/） |
| 存储 | Redis（在线热数据）+ MySQL（离线存档），分层落盘 |

## 目录结构

```
openworld-server/
├── server/        C++ 服务器
├── bridge/        网页 ↔ 服务器 的 WebSocket 桥（纯 Python 标准库）
├── demo/          网页客户端（浏览器直连真实服务器）
├── client/        Java 客户端
├── proto/         protobuf 协议定义（前后端公共合同）
└── docs/          项目规格 + 协议文档
```

## 当前进度

- [x] 协议草案 + 消息时序表（docs/protocol.md）
- [x] Asio echo 服务器 + protobuf 编解码 + 粘包/拆包（长度前缀帧）
- [x] 登录/心跳协议联调（假客户端测试）
- [x] 分块地图 + 服务器权威移动 + 九宫格 AOI 广播 + 多玩家同屏（**MVP**）
- [x] 网页客户端 + WebSocket 桥（浏览器 ↔ 桥 ↔ 服务器，真机可玩）
- [x] 数据持久化（Redis 热数据 + MySQL 冷存档，掉线不丢档）
- [ ] 网关 + 逻辑服拆分（可选，先单进程）
- [ ] 心跳超时 + 断线重连完整处理
- [ ] GoogleTest 单测 + 压测

文档：[docs/prd.md](docs/prd.md)（产品设计） · [docs/spec.md](docs/spec.md)（技术规格） · [docs/backend-design.md](docs/backend-design.md)（后端架构） · [docs/tech-design.md](docs/tech-design.md)（移动/AOI 设计） · [docs/protocol.md](docs/protocol.md)（协议） · [docs/persistence.md](docs/persistence.md)（持久化）。

## 构建

前置：安装 Visual Studio 2026 Community 的「使用 C++ 的桌面开发」工作负载（自带 MSVC + CMake）。

```bash
cd server
cmake -S . -B build
cmake --build build --config Release
```

（Windows 上也可直接双击 [server/build.bat](server/build.bat) 完成。）

## 运行

### 1. 启动服务器

```bash
./server/build/Release/openworld_server.exe 9000
```

默认启用持久化，需要 MySQL 8 + Redis 7 运行中。连接参数走环境变量（密码不写死在代码里，见 [server/src/db_config.h](server/src/db_config.h)）：

```bash
# PowerShell
$env:OPENWORLD_MYSQL_PASS="你的MySQL密码"   # 其余用默认：127.0.0.1:3306 库 openworld 用户 openworld
./server/build/Release/openworld_server.exe 9000
```

未设置密码时 MySQL 连不上，服务器自动降级为仅内存模式（不崩）。不想依赖数据库时显式加 `--no-db`：

```bash
./server/build/Release/openworld_server.exe 9000 --no-db
```

### 2. 启动 WebSocket 桥（纯 Python 标准库，零依赖）

```bash
python bridge/ws_bridge.py 9000 8080
```

### 3. 打开网页客户端

浏览器访问 http://localhost:8080/ ，输入用户名登录。**开两个标签页**登录不同用户名，就能在同一个世界里实时看到彼此移动（九宫格 AOI：走近出现、走远消失）。

> 网页客户端不能直接连原生 TCP，所以用 bridge 做「浏览器 WebSocket ↔ 服务器 TCP」的转发，服务器本身不需要任何改动。

## 测试

```bash
python server/tests/test_protocol.py      # 协议层 + MVP 玩法（登录/限速/多玩家/九宫格 AOI/断线）
python server/tests/test_persistence.py   # 持久化（需 MySQL+Redis：掉线不丢档/重名拒绝）
python bridge/test_bridge.py              # 桥接端到端（登录/双玩家同屏/移动）
```
