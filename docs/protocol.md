# 协议与消息时序（前后端合同）

> 本文件是前后端的公共合同。任何一方修改协议，必须先通知对方，一起改 `proto/openworld.proto` 和本文件。

## 基本约定

- **传输**：原生 TCP 长连接（客户端不能是纯浏览器，浏览器只能 WebSocket）
- **序列化**：protobuf（proto3）
- **帧格式**：`[4 字节长度前缀][protobuf Envelope]`（粘包/拆包靠长度前缀解决）
- **权威**：服务端权威——移动/属性以服务器校验为准，客户端只发**意图**，不报坐标

## 消息信封

所有消息都包在 `Envelope` 里，`type` 标识类型，`oneof body` 承载具体消息：

```protobuf
message Envelope {
    enum Type {
        NONE = 0;
        LOGIN_REQ = 1;
        LOGIN_ACK = 2;
        MOVE_REQ = 3;
        MOVE_BROADCAST = 4;
        HEARTBEAT = 5;
        PLAYER_ENTER = 6;
        PLAYER_LEAVE = 7;
    }
    Type type = 1;
    oneof body {
        LoginReq login_req = 2;
        LoginAck login_ack = 3;
        MoveReq move_req = 4;
        MoveBroadcast move_broadcast = 5;
        Heartbeat heartbeat = 6;
        PlayerEnter player_enter = 7;
        PlayerLeave player_leave = 8;
    }
}
```

## 消息时序表

| 消息 | 方向 | 时机 | 服务器处理 |
|---|---|---|---|
| `LoginReq` | 客户端 → 服务器 | 连上后立刻发 | 校验用户名/token，回 `LoginAck`（含出生点） |
| `LoginAck` | 服务器 → 客户端 | 登录成功后 | —— |
| `MoveReq` | 客户端 → 服务器 | 按方向键时 | 服务器校验、计算新位置，广播 `MoveBroadcast` 给附近玩家 |
| `MoveBroadcast` | 服务器 → 附近玩家 | 有人移动后 | ——（客户端只接收渲染） |
| `Heartbeat` | 双向 | 定时 | 更新在线状态；超时判定掉线 |
| `PlayerEnter` | 服务器 → 附近玩家 | 有人进入你的九宫格 | 广播其 `player_id` + 位置，客户端据此创建/显示该玩家 |
| `PlayerLeave` | 服务器 → 附近玩家 | 有人离开你的九宫格 | 广播其 `player_id`，客户端据此移除该玩家 |

## 服务器权威原则（防作弊）

- 移动：客户端发**方向/速度**，服务器算**新坐标**，不允许客户端直接报坐标
- 若客户端上报位置与服务器推算不符，以服务器为准并广播纠正

## protobuf 向后兼容规则

- 只加字段，**不改号、不删号、不重用** field number
- 这样一方先升级协议，另一方不升级也不会崩
