#include "world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace openworld {

namespace {

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

World::World(asio::io_context& io, PersistenceService& persistence)
    : io_(io), tick_(io), persistence_(persistence) {
    next_save_ms_ = now_ms() + 10000;
    start_tick();
}

void World::start_tick() {
    tick_.expires_after(std::chrono::milliseconds(kTickMs));
    tick_.async_wait([this](std::error_code ec) {
        if (ec) return;  // timer 被取消
        tick();
        start_tick();
    });
}

void World::handle(Connection& conn, Envelope& env) {
    switch (env.type()) {
        case Envelope::LOGIN_REQ:
            on_login(conn, env.login_req());
            break;
        case Envelope::MOVE_REQ:
            on_move(conn, env.move_req());
            break;
        case Envelope::HEARTBEAT:
            on_heartbeat(conn);
            break;
        default:
            break;
    }
}

void World::on_login(Connection& conn, const LoginReq& req) {
    // 同连接重复登录：幂等，返回已有会话（避免同一连接创建多个玩家）
    auto existing = by_conn_.find(&conn);
    if (existing != by_conn_.end()) {
        send_login_ack(*existing->second);
        return;
    }
    // 已在登录流程中：忽略重复帧
    if (pending_login_.count(&conn)) return;

    const std::string name = req.username();
    // 用户名（账号身份）已在线或正在登录：拒绝
    if (by_name_.count(name) || pending_names_.count(name)) {
        send_login_denied(conn, "用户名已在线");
        return;
    }

    // 登记 pending，异步加载存档；回调里再创建玩家（见 complete_login）
    pending_login_[&conn] = name;
    pending_names_.insert(name);
    auto self = conn.shared_from_this();  // 保活：加载期间连接不断，回调安全
    persistence_.async_load(name, [this, self](std::optional<std::pair<float, float>> saved) {
        complete_login(*self, saved);
    });
}

void World::complete_login(Connection& conn, std::optional<std::pair<float, float>> saved) {
    auto it = pending_login_.find(&conn);
    if (it == pending_login_.end()) return;  // 加载期间连接已断开，放弃
    const std::string name = it->second;
    pending_login_.erase(it);
    pending_names_.erase(name);

    const int n = next_id_++;
    const std::string id = "player_" + std::to_string(n);
    // 有存档则恢复最后位置，否则用默认出生点 (100,100)
    const float sx = saved ? saved->first : 100.0f;
    const float sy = saved ? saved->second : 100.0f;

    auto player = std::make_shared<Player>(id, name, sx, sy, conn.shared_from_this());
    player->set_cell(static_cast<int>(sx / kCellSize), static_cast<int>(sy / kCellSize));

    players_[id] = player;
    by_conn_[&conn] = player;
    by_name_[name] = player.get();
    grid_add(player.get());

    send_login_ack(*player);

    // 入场同步：与九宫格内已有玩家互发 PlayerEnter
    for_each_in_aoi(*player, [&](Player* q) {
        send_enter_to(*q, *player);  // 已有玩家看到 P 进入
        send_enter_to(*player, *q);  // P 看到已有玩家
    });
}

void World::send_login_denied(Connection& conn, const std::string& error) {
    Envelope ack;
    ack.set_type(Envelope::LOGIN_ACK);
    auto* a = ack.mutable_login_ack();
    a->set_ok(false);
    a->set_error(error);
    conn.send(ack);
}

void World::on_move(Connection& conn, const MoveReq& req) {
    auto it = by_conn_.find(&conn);
    if (it == by_conn_.end()) return;  // 未登录，忽略

    Player& p = *it->second;
    // 归一化方向 + 限速；客户端坐标一概不信
    float dx = req.dir().x();
    float dy = req.dir().y();
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) {
        p.set_velocity(0.0f, 0.0f);  // 停下
    } else {
        dx /= len;
        dy /= len;
        const float speed = std::clamp(req.speed(), 0.0f, kMaxSpeed);
        p.set_velocity(dx * speed, dy * speed);
    }
    p.mark_dirty();  // 停下也要广播一次
}

void World::on_heartbeat(Connection& conn) {
    Envelope resp;
    resp.set_type(Envelope::HEARTBEAT);
    resp.mutable_heartbeat()->set_server_time(now_ms());
    conn.send(resp);
}

void World::on_disconnect(Connection* conn) {
    // 登录尚未完成就断开：清 pending 即可
    if (auto pit = pending_login_.find(conn); pit != pending_login_.end()) {
        pending_names_.erase(pit->second);
        pending_login_.erase(pit);
        return;
    }

    auto it = by_conn_.find(conn);
    if (it == by_conn_.end()) return;  // 未登录或已清理，幂等

    auto player = it->second;  // 持有一份，防止清理期间被析构
    by_conn_.erase(it);
    players_.erase(player->id());
    by_name_.erase(player->name());
    grid_remove(player.get());

    // 最终位置落盘（离线存档，移除热缓存）
    persistence_.save_offline(player->name(), player->x(), player->y());

    // 通知九宫格内其他玩家：P 离开
    Envelope env;
    env.set_type(Envelope::PLAYER_LEAVE);
    env.mutable_player_leave()->set_player_id(player->id());
    for_each_in_aoi(*player, [&](Player* q) {
        q->send(env);
    });
}

void World::tick() {
    const float dt = static_cast<float>(kTickMs) / 1000.0f;

    // 1) 积分所有玩家位置，跨格子则更新网格并做进入/离开
    for (auto& [id, p] : players_) {
        const int old_cx = p->cell_x();
        const int old_cy = p->cell_y();
        if (p->vx() != 0.0f || p->vy() != 0.0f) {
            p->integrate(dt, kWorldSize, kWorldSize);
            p->mark_dirty();
        }
        const int new_cx = static_cast<int>(p->x() / kCellSize);
        const int new_cy = static_cast<int>(p->y() / kCellSize);
        if (new_cx != old_cx || new_cy != old_cy) {
            grid_remove(p.get());
            p->set_cell(new_cx, new_cy);
            grid_add(p.get());
            on_cell_changed(*p, old_cx, old_cy);
        }
    }

    // 2) 广播 dirty 玩家的位置到其九宫格（含自身，作为位置回传）
    for (auto& [id, p] : players_) {
        if (!p->dirty()) continue;
        Envelope env;
        env.set_type(Envelope::MOVE_BROADCAST);
        auto* b = env.mutable_move_broadcast();
        b->set_player_id(p->id());
        b->mutable_pos()->set_x(p->x());
        b->mutable_pos()->set_y(p->y());
        b->mutable_vel()->set_x(p->vx());
        b->mutable_vel()->set_y(p->vy());
        broadcast_to_aoi(*p, env);
        p->clear_dirty();
    }

    // 3) 定时落盘：每 10s 把在线玩家位置写入 MySQL + Redis（fire-and-forget）
    const std::int64_t now = now_ms();
    if (now >= next_save_ms_) {
        next_save_ms_ = now + 10000;
        for (auto& [id, p] : players_) {
            persistence_.save_online(p->name(), p->x(), p->y());
        }
    }
}

std::int64_t World::cell_key(int cx, int cy) {
    return (static_cast<std::int64_t>(cx) << 32) | static_cast<std::uint32_t>(cy);
}

void World::grid_add(Player* p) {
    grid_[cell_key(p->cell_x(), p->cell_y())].push_back(p);
}

void World::grid_remove(Player* p) {
    auto it = grid_.find(cell_key(p->cell_x(), p->cell_y()));
    if (it == grid_.end()) return;
    auto& v = it->second;
    v.erase(std::remove(v.begin(), v.end(), p), v.end());
    if (v.empty()) grid_.erase(it);
}

void World::broadcast_to_aoi(const Player& p, Envelope& env) {
    for (int cx = p.cell_x() - 1; cx <= p.cell_x() + 1; ++cx) {
        for (int cy = p.cell_y() - 1; cy <= p.cell_y() + 1; ++cy) {
            auto it = grid_.find(cell_key(cx, cy));
            if (it == grid_.end()) continue;
            for (Player* q : it->second) {
                q->send(env);
            }
        }
    }
}

void World::on_cell_changed(Player& p, int old_cx, int old_cy) {
    const int cx = p.cell_x();
    const int cy = p.cell_y();

    // 进入：新九宫格有、旧九宫格没有的格子里的玩家
    for (int gx = cx - 1; gx <= cx + 1; ++gx) {
        for (int gy = cy - 1; gy <= cy + 1; ++gy) {
            if (std::abs(gx - old_cx) <= 1 && std::abs(gy - old_cy) <= 1) continue;
            auto it = grid_.find(cell_key(gx, gy));
            if (it == grid_.end()) continue;
            for (Player* q : it->second) {
                if (q == &p) continue;
                send_enter_to(*q, p);   // Q 看到 P 进入
                send_enter_to(p, *q);   // P 看到 Q
            }
        }
    }

    // 离开：旧九宫格有、新九宫格没有的格子里的玩家
    for (int gx = old_cx - 1; gx <= old_cx + 1; ++gx) {
        for (int gy = old_cy - 1; gy <= old_cy + 1; ++gy) {
            if (std::abs(gx - cx) <= 1 && std::abs(gy - cy) <= 1) continue;
            auto it = grid_.find(cell_key(gx, gy));
            if (it == grid_.end()) continue;
            for (Player* q : it->second) {
                if (q == &p) continue;
                send_leave_to(*q, p);   // Q 看到 P 离开
                send_leave_to(p, *q);   // P 看到 Q 离开
            }
        }
    }
}

void World::send_login_ack(Player& p) {
    Envelope ack;
    ack.set_type(Envelope::LOGIN_ACK);
    auto* a = ack.mutable_login_ack();
    a->set_ok(true);
    a->set_player_id(p.id());
    a->mutable_spawn()->set_x(p.spawn_x());
    a->mutable_spawn()->set_y(p.spawn_y());
    p.send(ack);
}

void World::send_enter_to(Player& viewer, Player& subject) {
    Envelope env;
    env.set_type(Envelope::PLAYER_ENTER);
    auto* e = env.mutable_player_enter();
    e->set_player_id(subject.id());
    e->set_name(subject.name());
    e->mutable_pos()->set_x(subject.x());
    e->mutable_pos()->set_y(subject.y());
    viewer.send(env);
}

void World::send_leave_to(Player& viewer, Player& subject) {
    Envelope env;
    env.set_type(Envelope::PLAYER_LEAVE);
    env.mutable_player_leave()->set_player_id(subject.id());
    viewer.send(env);
}

}  // namespace openworld
