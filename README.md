# openworld-server

2D 开放世界 RPG 服务器—— 一个人能完成的开放世界RPG服务端微缩版。

**目标**：把开放世界RPG服务端的四个核心概念——**分线、AOI 视野、状态同步、数据持久化**——在可完成的尺度下完整跑通一遍，作为游戏服务端的作品。。

## 技术栈

| 端 | 技术 |
|---|---|
| 服务器 | C++20 · Asio · 自实现 Reactor · protobuf · spdlog · GoogleTest · Redis/MySQL |
| 客户端 | Java（自行决定框架，硬约束：原生 TCP + protobuf） |
| 存储 | Redis（在线热数据）+ MySQL（离线存档） |

## 目录结构

```
openworld-server/
├── server/        C++ 服务器
├── client/        Java 客户端
├── proto/         protobuf 协议定义（前后端公共合同）
└── docs/          项目规格 + 协议文档
```

## 当前进度

- [x] 仓库初始化
- [x] 协议草案 + 消息时序表（docs/protocol.md）
- [x] Asio echo 服务器（编译通过）
- [x] protobuf 编解码 + 粘包/拆包（长度前缀帧）
- [x] 登录/心跳协议联调（假客户端测试）
- [ ] Reactor 模型精化
- [ ] 分块地图 + AOI
- [ ] 状态同步 + 持久化
- [ ] 网关 + 逻辑服
- [ ] 断线重连 + 压测

文档：[docs/prd.md](docs/prd.md)（产品设计） · [docs/tech-design.md](docs/tech-design.md)（技术方案） · [docs/spec.md](docs/spec.md)（技术规格） · [docs/protocol.md](docs/protocol.md)（协议）。

## 构建

前置：安装 Visual Studio 2026 Community 的「使用 C++ 的桌面开发」工作负载（自带 MSVC + CMake）。

```bash
cd server
cmake -S . -B build
cmake --build build --config Release
./build/Release/openworld_server.exe 9000
```

（Windows 上也可直接双击 [server/build.bat](server/build.bat) 完成前两步。）
