#!/usr/bin/env python3
# 集成测试：验证协议层 + MVP 玩法（登录/移动/限速/多玩家/九宫格 AOI/断线）。
# 自带极简 protobuf 编解码，零第三方依赖，直接运行：
#   python tests/test_protocol.py

import os
import socket
import struct
import subprocess
import sys
import time

# Windows 控制台默认 GBK，强制 UTF-8 输出，避免中文/特殊字符编码错误
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

SERVER_EXE = os.path.normpath(os.path.join(
    os.path.dirname(__file__), '..', 'build', 'Release', 'openworld_server.exe'))
PORT = 19000

# 消息类型（与 proto Envelope.Type 一致）
T_LOGIN_REQ = 1
T_LOGIN_ACK = 2
T_MOVE_REQ = 3
T_MOVE_BROADCAST = 4
T_HEARTBEAT = 5
T_PLAYER_ENTER = 6
T_PLAYER_LEAVE = 7

# 服务器常量（与 world.h 保持一致）
MAX_SPEED = 20.0


# ---- 极简 protobuf 编解码（只支持本项目用到的 wire type）----

def varint_encode(value):
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        if value:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def varint_decode(data, pos):
    result = 0
    shift = 0
    while True:
        b = data[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, pos
        shift += 7


def field_varint(num, value):
    return varint_encode((num << 3) | 0) + varint_encode(value)


def field_bytes(num, value):
    return varint_encode((num << 3) | 2) + varint_encode(len(value)) + value


def field_float(num, value):
    return varint_encode((num << 3) | 5) + struct.pack('<f', value)


def parse_fields(data):
    fields = []
    pos = 0
    while pos < len(data):
        tag, pos = varint_decode(data, pos)
        num, wt = tag >> 3, tag & 0x7
        if wt == 0:
            val, pos = varint_decode(data, pos)
        elif wt == 2:
            ln, pos = varint_decode(data, pos)
            val = data[pos:pos + ln]
            pos += ln
        elif wt == 5:
            val = data[pos:pos + 4]
            pos += 4
        else:
            raise ValueError(f'unsupported wire type {wt}')
        fields.append((num, wt, val))
    return fields


# ---- 帧编解码（长度前缀，与 C++ 端 frame.cpp 一致）----

def encode_frame(payload):
    return struct.pack('>I', len(payload)) + payload


def recv_exact(sock, n):
    data = b''
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError('connection closed')
        data += chunk
    return data


def recv_frame(sock):
    header = recv_exact(sock, 4)
    length = struct.unpack('>I', header)[0]
    return recv_exact(sock, length)


def envelope_type(payload):
    for num, wt, val in parse_fields(payload):
        if num == 1 and wt == 0:
            return val
    return 0


def recv_until_type(sock, type_num, timeout=3.0, max_frames=400):
    """读取直到收到指定类型的消息，返回其 payload；超时返回 None。"""
    sock.settimeout(timeout)
    try:
        for _ in range(max_frames):
            payload = recv_frame(sock)
            if envelope_type(payload) == type_num:
                return payload
    except socket.timeout:
        return None
    return None


# ---- 消息构造 / 解析 ----

def encode_login_req(username, token=''):
    login = field_bytes(1, username.encode()) + field_bytes(2, token.encode())
    return field_varint(1, T_LOGIN_REQ) + field_bytes(2, login)


def encode_move_req(dx, dy, speed, seq=0):
    vec2 = field_float(1, dx) + field_float(2, dy)
    move = field_bytes(1, vec2) + field_float(2, speed) + field_varint(3, seq)
    return field_varint(1, T_MOVE_REQ) + field_bytes(4, move)


def encode_heartbeat(client_time):
    hb = field_varint(1, client_time)
    return field_varint(1, T_HEARTBEAT) + field_bytes(6, hb)


def decode_vec2(data):
    x, y = 0.0, 0.0
    for num, wt, val in parse_fields(data):
        if wt != 5:
            continue
        if num == 1:
            x = struct.unpack('<f', val)[0]
        elif num == 2:
            y = struct.unpack('<f', val)[0]
    return (x, y)


def decode_login_ack(payload):
    for num, wt, val in parse_fields(payload):
        if num == 3 and wt == 2:  # login_ack
            ok, pid = False, ''
            spawn = (0.0, 0.0)
            for anum, awt, aval in parse_fields(val):
                if anum == 1:
                    ok = bool(aval)
                elif anum == 2:
                    pid = aval.decode()
                elif anum == 3:  # spawn = Vec2
                    spawn = decode_vec2(aval)
            return ok, pid, spawn
    raise ValueError('no login_ack in envelope')


def decode_move_broadcast(payload):
    for num, wt, val in parse_fields(payload):
        if num == 5 and wt == 2:  # move_broadcast
            pid, pos, vel = '', (0.0, 0.0), (0.0, 0.0)
            for bnum, bwt, bval in parse_fields(val):
                if bnum == 1:
                    pid = bval.decode()
                elif bnum == 2:
                    pos = decode_vec2(bval)
                elif bnum == 3:
                    vel = decode_vec2(bval)
            return pid, pos, vel
    raise ValueError('no move_broadcast in envelope')


def decode_player_enter(payload):
    for num, wt, val in parse_fields(payload):
        if num == 7 and wt == 2:  # player_enter
            pid, name, pos = '', '', (0.0, 0.0)
            for anum, awt, aval in parse_fields(val):
                if anum == 1:
                    pid = aval.decode()
                elif anum == 2:
                    pos = decode_vec2(aval)
                elif anum == 3:
                    name = aval.decode()
            return pid, name, pos
    raise ValueError('no player_enter in envelope')


def decode_player_leave(payload):
    for num, wt, val in parse_fields(payload):
        if num == 8 and wt == 2:  # player_leave
            for lnum, lwt, lval in parse_fields(val):
                if lnum == 1:
                    return lval.decode()
    raise ValueError('no player_leave in envelope')


def decode_heartbeat(payload):
    for num, wt, val in parse_fields(payload):
        if num == 6 and wt == 2:  # heartbeat
            for hnum, _hwt, hval in parse_fields(val):
                if hnum == 2:
                    return hval
    raise ValueError('no heartbeat in envelope')


# ---- 连接 / 登录助手 ----

def connect():
    return socket.create_connection(('127.0.0.1', PORT), timeout=5)


def login(sock, username):
    sock.sendall(encode_frame(encode_login_req(username)))
    ok, pid, spawn = decode_login_ack(recv_frame(sock))
    assert ok, f'login({username}) should be ok'
    return pid, spawn


# ---- 测试用例 ----

def test_login(sock):
    sock.sendall(encode_frame(encode_login_req('yuzheng')))
    ok, pid, spawn = decode_login_ack(recv_frame(sock))
    assert ok, f'login should be ok, got {ok}'
    assert pid == 'player_1', f'player_id mismatch: {pid}'
    assert spawn == (100.0, 100.0), f'spawn mismatch: {spawn}'
    print(f'  [pass] 登录：ok={ok}, player_id={pid}, spawn={spawn}')


def test_sticky_packet(sock):
    # 粘包：一次发两个完整帧（两个心跳），应拆成两条心跳回包。
    # 登录已异步化，用无状态的心跳（同步回包）来测粘包拆分，不依赖登录语义。
    sock.sendall(encode_frame(encode_heartbeat(1111)) + encode_frame(encode_heartbeat(2222)))
    hb1 = recv_frame(sock)
    hb2 = recv_frame(sock)
    assert envelope_type(hb1) == T_HEARTBEAT and envelope_type(hb2) == T_HEARTBEAT
    print('  [pass] 粘包：一次发两帧，正确拆成两条响应')


def test_half_packet(sock):
    # 半包：一帧分两次发
    frame = encode_frame(encode_login_req('half'))
    sock.sendall(frame[:5])          # 先发 4 字节长度头 + 1 字节 payload
    time.sleep(0.05)
    sock.sendall(frame[5:])          # 再发剩余
    ok, pid, _ = decode_login_ack(recv_frame(sock))
    assert ok and pid == 'player_1'
    print('  [pass] 半包：一帧分两次发，正确拼回')


def test_heartbeat(sock):
    sock.sendall(encode_frame(encode_heartbeat(12345)))
    server_time = decode_heartbeat(recv_frame(sock))
    assert server_time > 0, f'server_time should be positive, got {server_time}'
    print(f'  [pass] 心跳：server_time={server_time}')


def test_move():
    sock = connect()
    pid, spawn = login(sock, 'mover')
    sock.sendall(encode_frame(encode_move_req(1.0, 0.0, 2.0, 1)))
    p1 = recv_until_type(sock, T_MOVE_BROADCAST)
    assert p1 is not None, 'MoveReq 后应收到 MoveBroadcast'
    _, pos1, vel1 = decode_move_broadcast(p1)
    assert vel1[0] == 2.0, f'vel.x 应为 2.0，实际 {vel1[0]}'
    p2 = recv_until_type(sock, T_MOVE_BROADCAST)
    _, pos2, _ = decode_move_broadcast(p2)
    assert pos2[0] > pos1[0], f'位置应沿 +x 增长：{pos1[0]} -> {pos2[0]}'
    sock.close()
    print('  [pass] 移动：MoveReq → MoveBroadcast，位置沿 +x 增长，vel=2.0')


def test_speed_clamp():
    sock = connect()
    login(sock, 'speeder')
    sock.sendall(encode_frame(encode_move_req(1.0, 0.0, 999.0, 1)))
    p = recv_until_type(sock, T_MOVE_BROADCAST)
    assert p is not None, '应收到 MoveBroadcast'
    _, _, vel = decode_move_broadcast(p)
    assert abs(vel[0] - MAX_SPEED) < 1e-3, f'速度应截断为 {MAX_SPEED}，实际 {vel[0]}'
    sock.close()
    print(f'  [pass] 限速：speed=999 → 服务器截断为 {MAX_SPEED}')


def test_two_players():
    a, b = connect(), connect()
    aid, _ = login(a, 'alice')
    _, _ = login(b, 'bob')
    # B 与 A 同屏出生，B 应收到 A 的 PlayerEnter
    enter = recv_until_type(b, T_PLAYER_ENTER)
    assert enter is not None, 'B 应收到 A 的 PlayerEnter'
    eid, ename, _ = decode_player_enter(enter)
    assert eid == aid, f'PlayerEnter id={eid}，期望 {aid}'
    assert ename == 'alice', f'PlayerEnter name={ename}，期望 alice'
    # A 移动 → B 收到 A 的 MoveBroadcast
    a.sendall(encode_frame(encode_move_req(1.0, 0.0, 5.0, 1)))
    mb = recv_until_type(b, T_MOVE_BROADCAST)
    assert mb is not None, 'B 应收到 A 的 MoveBroadcast'
    mid, _, _ = decode_move_broadcast(mb)
    assert mid == aid, f'MoveBroadcast id={mid}，期望 {aid}'
    a.close(); b.close()
    print('  [pass] 双玩家：A 移动 → B 收到 A 的 PlayerEnter + MoveBroadcast')


def test_enter_leave():
    a, b = connect(), connect()
    aid, _ = login(a, 'alice')
    _, _ = login(b, 'bob')
    _ = recv_until_type(b, T_PLAYER_ENTER)  # 清掉 B 收到的 A 入场
    # A 一直向右走，直到离开 B 的九宫格
    a.sendall(encode_frame(encode_move_req(1.0, 0.0, MAX_SPEED, 1)))
    leave = recv_until_type(b, T_PLAYER_LEAVE, timeout=8)
    assert leave is not None, 'A 走出视野后 B 应收到 PlayerLeave'
    assert decode_player_leave(leave) == aid
    # A 反向走回 → B 重新收到 PlayerEnter
    a.sendall(encode_frame(encode_move_req(-1.0, 0.0, MAX_SPEED, 2)))
    enter = recv_until_type(b, T_PLAYER_ENTER, timeout=8)
    assert enter is not None, 'A 走回后 B 应收到 PlayerEnter'
    eid, _, _ = decode_player_enter(enter)
    assert eid == aid
    a.close(); b.close()
    print('  [pass] AOI 进入/离开：走远 → PlayerLeave，走回 → PlayerEnter')


def test_disconnect():
    a, b = connect(), connect()
    aid, _ = login(a, 'alice')
    bid, _ = login(b, 'bob')
    _ = recv_until_type(b, T_PLAYER_ENTER)
    _ = recv_until_type(a, T_PLAYER_ENTER)
    b.close()  # B 断开
    leave = recv_until_type(a, T_PLAYER_LEAVE, timeout=3)
    assert leave is not None, 'B 断开后 A 应收到 PlayerLeave'
    assert decode_player_leave(leave) == bid
    # 服务器不崩：A 仍能正常移动
    a.sendall(encode_frame(encode_move_req(1.0, 0.0, 5.0, 1)))
    assert recv_until_type(a, T_MOVE_BROADCAST) is not None, 'B 断开后 A 应仍能移动'
    a.close()
    print('  [pass] 断线清理：B 断开 → A 收到 PlayerLeave 且仍可移动')


def main():
    if not os.path.exists(SERVER_EXE):
        print(f'服务器不存在：{SERVER_EXE}，请先构建')
        return 1

    # --no-db：协议/玩法测试保持封闭，不依赖 MySQL/Redis
    proc = subprocess.Popen([SERVER_EXE, str(PORT), '--no-db'],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(0.5)  # 等服务器就绪
        sock = socket.create_connection(('127.0.0.1', PORT), timeout=5)
        print('服务器已连接，开始测试：')
        # 原有 4 场景（单连接复用，协议层不回归）
        test_login(sock)
        test_sticky_packet(sock)
        test_half_packet(sock)
        test_heartbeat(sock)
        sock.close()
        # 新增玩法场景（各自独立连接）
        test_move()
        test_speed_clamp()
        test_two_players()
        test_enter_leave()
        test_disconnect()
        print('\n全部测试通过')
        return 0
    finally:
        proc.terminate()
        proc.wait()


if __name__ == '__main__':
    sys.exit(main())
