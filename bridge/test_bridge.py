#!/usr/bin/env python3
# 桥接端到端测试：手写 WebSocket 客户端（带掩码），验证
#   浏览器 ↔ ws_bridge.py ↔ 游戏服务器 这条链路的 protobuf 转发。
# 纯标准库，零第三方依赖。复用 server/tests/test_protocol.py 的编解码助手。
#   python bridge/test_bridge.py

import base64
import os
import socket
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, '..', 'server', 'tests'))
import test_protocol as tp  # 复用 Envelope 编解码

SERVER_EXE = os.path.normpath(os.path.join(
    ROOT, '..', 'server', 'build', 'Release', 'openworld_server.exe'))
GAME_PORT = 19001
HTTP_PORT = 8090


def recv_exact(sock, n):
    data = b''
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError('closed')
        data += chunk
    return data


def ws_connect(host='127.0.0.1', port=HTTP_PORT, path='/ws'):
    sock = socket.create_connection((host, port), timeout=5)
    key = base64.b64encode(os.urandom(16)).decode()
    req = (f'GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\n'
           f'Upgrade: websocket\r\nConnection: Upgrade\r\n'
           f'Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n')
    sock.sendall(req.encode())
    resp = b''
    while b'\r\n\r\n' not in resp:
        resp += sock.recv(4096)
    assert b'101' in resp.split(b'\r\n', 1)[0], f'握手失败: {resp}'
    return sock


def ws_send(sock, payload, opcode=0x2):
    mask = os.urandom(4)
    n = len(payload)
    head = bytearray([0x80 | opcode])
    if n < 126:
        head.append(0x80 | n)
    elif n < 65536:
        head.append(0x80 | 126)
        head += struct.pack('>H', n)
    else:
        head.append(0x80 | 127)
        head += struct.pack('>Q', n)
    head += mask
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    sock.sendall(bytes(head) + masked)


def ws_recv(sock):
    b1, b2 = recv_exact(sock, 2)
    opcode = b1 & 0x0F
    length = b2 & 0x7F
    if length == 126:
        length = struct.unpack('>H', recv_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack('>Q', recv_exact(sock, 8))[0]
    return opcode, recv_exact(sock, length)


def ws_recv_until_type(sock, type_num, timeout=3.0):
    sock.settimeout(timeout)
    try:
        for _ in range(400):
            opcode, payload = ws_recv(sock)
            if opcode == 0x8:
                return None
            if opcode == 0x2 and tp.envelope_type(payload) == type_num:
                return payload
    except socket.timeout:
        return None
    return None


def main():
    if not os.path.exists(SERVER_EXE):
        print(f'服务器不存在：{SERVER_EXE}，请先构建')
        return 1

    server = subprocess.Popen([SERVER_EXE, str(GAME_PORT)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    bridge = subprocess.Popen([sys.executable, os.path.join(ROOT, 'ws_bridge.py'),
                               str(GAME_PORT), str(HTTP_PORT)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(0.6)  # 等两者就绪

        a = ws_connect()
        ws_send(a, tp.encode_login_req('alice'))
        ack = ws_recv_until_type(a, tp.T_LOGIN_ACK)
        assert ack is not None, 'A 未收到 LoginAck'
        ok, aid, spawn = tp.decode_login_ack(ack)
        assert ok and aid == 'player_1' and spawn == (100.0, 100.0), (ok, aid, spawn)
        print(f'  [pass] 桥接登录：A={aid}, spawn={spawn}')

        b = ws_connect()
        ws_send(b, tp.encode_login_req('bob'))
        back = ws_recv_until_type(b, tp.T_LOGIN_ACK)
        assert back is not None, 'B 未收到 LoginAck'
        _, bid, _ = tp.decode_login_ack(back)
        assert bid == 'player_2', bid
        enter = ws_recv_until_type(b, tp.T_PLAYER_ENTER)
        assert enter is not None, 'B 未收到 A 的 PlayerEnter'
        eid, ename, _ = tp.decode_player_enter(enter)
        assert eid == aid, f'PlayerEnter id={eid}，期望 {aid}'
        assert ename == 'alice', f'PlayerEnter name={ename}，期望 alice'
        print(f'  [pass] 桥接双玩家：B={bid} 看到 {ename}({eid}) 进入视野')

        ws_send(a, tp.encode_move_req(1.0, 0.0, 5.0, 1))
        mb = ws_recv_until_type(b, tp.T_MOVE_BROADCAST)
        assert mb is not None, 'B 未收到 A 的 MoveBroadcast'
        mid, _, _ = tp.decode_move_broadcast(mb)
        assert mid == aid, f'MoveBroadcast id={mid}，期望 {aid}'
        print('  [pass] 桥接移动：A 移动 → B 收到 MoveBroadcast')

        # 回归：空闲存活。A 停下后服务器安静，B 静止 >6s 不应被桥接的 5s 超时误断。
        ws_send(a, tp.encode_move_req(0.0, 0.0, 0.0, 2))
        time.sleep(6.5)
        ws_send(b, tp.encode_heartbeat(12345))
        hb = ws_recv_until_type(b, tp.T_HEARTBEAT, timeout=2)
        assert hb is not None, '空闲 6.5s 后 B 应仍在线（心跳有回包）'
        print('  [pass] 桥接空闲存活：静止 >6s 连接不断')

        a.close(); b.close()
        print('\n桥接端到端测试通过')
        return 0
    finally:
        bridge.terminate()
        server.terminate()
        bridge.wait()
        server.wait()


if __name__ == '__main__':
    sys.exit(main())
