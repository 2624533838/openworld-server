# 数据持久化：Redis 热数据 + MySQL 冷存档

## 为什么要有持久化

MVP 阶段每次登录都发新号（`next_id_++`）、固定出生在 (100,100)，断开即丢。这一层补上「掉线不丢档」：

- **用户名 = 账号身份**：同名在线互斥，重登恢复最后位置
- **退出 → 下次登录数据仍在**：位置存进 MySQL 冷存档

## 分层设计

```
内存（玩家对象，实时状态）
   │  读穿（miss 回填）/ 写穿（双写）
   ▼
Redis 热数据：在线缓存，key `openworld:player:<用户名>` → value `<x>,<y>`
   │
   ▼
MySQL 冷存档：离线存档，表 players（username 主键 + x,y + updated_at）
```

| 层 | 角色 | 生命周期 |
|---|---|---|
| 内存 | 实时状态，tick 直接读写 | 在线期间 |
| Redis | 热缓存，加速重登读 | 在线时 SET，下线 DEL |
| MySQL | 冷存档，唯一持久落盘的层 | 每次落盘 upsert |

## 为什么不能在主循环里同步写库

服务器是**单线程 io_context**，30Hz tick 跑在同一个线程上。MySQL/Redis 都是阻塞 IO，若在 tick 里同步调 `SELECT/INSERT`，一次网络往返（哪怕几毫秒）就会冻结整个 tick，导致掉帧、所有玩家卡顿。

解法：**独立持久化工作线程** + 任务队列（`mutex` + `condition_variable` + `deque<std::function<void()>>`）：

- 工作线程持有**唯一**的 MySQL 连接和 Redis 连接，串行消费队列
- **登录用回调续传**：worker 查完库 `asio::post(io_context, cb)` 回到主线程创建玩家
- **保存 fire-and-forget**：不阻塞 tick，落盘失败只打日志不崩

## 落盘时机

| 时机 | 动作 |
|---|---|
| 登录 | 读穿：先 `GET` Redis，miss 再 `SELECT` MySQL 并回填 Redis |
| 断开 | `save_offline`：MySQL upsert 最终位置 + Redis `DEL`（下线移除热缓存） |
| 定时（每 10s，tick 内） | `save_online`：MySQL upsert + Redis `SET`（在线刷新热缓存） |

## MySQL 表结构

```sql
CREATE TABLE IF NOT EXISTS players (
  username  VARCHAR(64) PRIMARY KEY,
  x FLOAT NOT NULL, y FLOAT NOT NULL,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

保存用预处理语句 + upsert：

```sql
INSERT INTO players (username, x, y) VALUES (?, ?, ?)
ON DUPLICATE KEY UPDATE x = VALUES(x), y = VALUES(y);
```

## 登录流程（异步）

```
on_login
  ├─ 连接已登录 → 重发 ack（幂等）
  ├─ 登录中 → 忽略（防重复）
  ├─ 用户名在线/登录中 → 拒绝 LoginAck{ok:false, error:"用户名已在线"}
  └─ 否则 → 登记 pending，async_load(name)
            └─ 回调(回到主线程)：仍 pending 才创建玩家
               （spawn = 存档位置或默认 (100,100)），发 ack + 九宫格入场广播
```

## 优雅降级

- 连接参数走环境变量（`OPENWORLD_MYSQL_HOST/_PORT/_USER/_PASS/_DB`、`OPENWORLD_REDIS_HOST/_PORT`），密码无默认值、不写死在代码里（防随源码入库）；未设置密码时 MySQL 连不上，自动降级为仅内存
- `openworld_server [port] [--no-db]`：`--no-db` 不启用持久化（登录走统一异步路径、`async_load` 立即回空，保存 no-op），供协议测试与无库环境使用
- 默认启用：worker 启动时连 MySQL/Redis，连不上则 `available=false` 降级（加载回空→默认出生点、保存 no-op、打 warning），**库挂了服务器不崩**
- 退出：`SIGINT`/`SIGTERM` 停 io_context → `persistence.stop()` drain 剩余保存任务，退出前写完最后一批掉线存档

## 客户端库选择

- **MySQL**：libmysqlclient C API + 预处理语句（`mysql_stmt_*`），参数绑定防 SQL 注入
- **Redis**：手写极简 RESP 客户端（约百行，只做 `SET/GET/DEL`，解析 `+OK`/`$-1`/`$<len>`/`:N`），零依赖

## 或「WAL」
- 预处理语句防注入 → 用户名走 `?` 占位绑定，不是字符串拼接
