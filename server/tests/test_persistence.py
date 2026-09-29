#!/usr/bin/env python3
# 持久化集成测试：验证「掉线不丢档」—— Redis 热数据 + MySQL 冷存档分层。
# 前置：MySQL + Redis 运行中（否则测试会失败，属预期）。复用 test_protocol 的编解码助手。
#   python tests/test_persistence.py

import os
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_protocol as tp

SERVER_EXE = os.path.normpath(os.path.join(
    os.path.dirname(__file__), '..', 'build', 'Release', 'openworld_server.exe'))
PORT = 19010


def connect():
    return socket.create_connection(('127.0.0.1', PORT), timeout=5)


def login_ack_error(payload):
    """取 LoginAck.error（字段 4），没有则返回空串。"""
    for num, wt, val in tp.parse_fields(payload):
        if num == 3 and wt == 2:  # login_ack
            for anum, awt, aval in tp.parse_fields(val):
                if anum == 4 and awt == 2:
                    return aval.decode()
    return ''


def main():
    if not os.path.exists(SERVER_EXE):
        print(f'服务器不存在：{SERVER_EXE}，请先构建')
        return 1

    # MySQL 密码走环境变量，不写死在代码里（见 server/src/db_config.h）
    if not os.environ.get('OPENWORLD_MYSQL_PASS'):
        print('持久化测试需设置环境变量 OPENWORLD_MYSQL_PASS（MySQL 密码），例如：')
        print('  PowerShell:  $env:OPENWORLD_MYSQL_PASS="<你的密码>"')
        print('  CMD:         set OPENWORLD_MYSQL_PASS=<你的密码>')
        return 2

    # 不带 --no-db：启用持久化
    proc = subprocess.Popen([SERVER_EXE, str(PORT)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(1.0)  # 等服务器 + 持久化 worker 连库
        name = f'u{int(time.time() * 1000)}'  # 唯一用户名，避免历史存档干扰
        print(f'测试用户：{name}')

        # 1) 首登：无存档 → 默认出生 (100,100)、player_1
        a = connect()
        pid, spawn = tp.login(a, name)
        assert pid == 'player_1', f'首登应为 player_1，实际 {pid}'
        assert spawn == (100.0, 100.0), f'首登应出生 (100,100)，实际 {spawn}'
        print(f'  [pass] 首登：{name} → {pid} @ {spawn}')

        # 2) 移动后断开 → 服务器落盘最终位置；同名重登应恢复该位置
        a.sendall(tp.encode_frame(tp.encode_move_req(1.0, 0.0, tp.MAX_SPEED, 1)))
        time.sleep(2.0)   # 沿 +x 走约 2s
        a.close()
        time.sleep(1.0)   # 等 save_offline 落盘

        b = connect()
        _, spawn2 = tp.login(b, name)
        assert spawn2[0] > 110.0, f'重登应恢复断开前位置，实际 {spawn2}'
        assert abs(spawn2[1] - 100.0) < 1e-3, f'y 不应变化，实际 {spawn2[1]}'
        print(f'  [pass] 重登：位置恢复为 {spawn2}（掉线不丢档）')
        b.close()
        time.sleep(0.5)

        # 3) 用户名唯一：同名已在线时，第二个登录被拒（error 非空）
        c1 = connect()
        tp.login(c1, name)  # c1 在线占用 name
        c2 = connect()
        c2.sendall(tp.encode_frame(tp.encode_login_req(name)))
        ack = tp.recv_frame(c2)
        ok, _, _ = tp.decode_login_ack(ack)
        assert not ok, f'重名应被拒绝，实际 ok={ok}'
        err = login_ack_error(ack)
        assert err, '拒绝应带 error 原因'
        print(f'  [pass] 重名拒绝：ok=False，error="{err}"')
        c1.close()
        c2.close()
        time.sleep(0.5)  # 等 save_offline 把 Redis 热缓存 DEL 干净

        print('\n持久化集成测试通过（Redis 热数据 + MySQL 冷存档）')
        return 0
    finally:
        proc.terminate()
        proc.wait()


if __name__ == '__main__':
    sys.exit(main())
