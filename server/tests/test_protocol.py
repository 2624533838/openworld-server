#!/usr/bin/env python3
# 协议层集成测试：验证长度前缀帧（粘包/拆包）+ protobuf 编解码（登录/心跳）。
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


# ---- 消息构造 / 解析 ----

def encode_login_req(username, token=''):
    login = field_bytes(1, username.encode()) + field_bytes(2, token.encode())
    return field_varint(1, 1) + field_bytes(2, login)  # type=LOGIN_REQ


def encode_heartbeat(client_time):
    hb = field_varint(1, client_time)
    return field_varint(1, 5) + field_bytes(6, hb)  # type=HEARTBEAT


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
                    for vnum, _vwt, vval in parse_fields(aval):
                        if vnum == 1:
                            spawn = (struct.unpack('<f', vval)[0], spawn[1])
                        elif vnum == 2:
                            spawn = (spawn[0], struct.unpack('<f', vval)[0])
            return ok, pid, spawn
    raise ValueError('no login_ack in envelope')


def decode_heartbeat(payload):
    for num, wt, val in parse_fields(payload):
        if num == 6 and wt == 2:  # heartbeat
            for hnum, _hwt, hval in parse_fields(val):
                if hnum == 2:
                    return hval
    raise ValueError('no heartbeat in envelope')


# ---- 测试用例 ----

def test_login(sock):
    sock.sendall(encode_frame(encode_login_req('yuzheng')))
    ok, pid, spawn = decode_login_ack(recv_frame(sock))
    assert ok, f'login should be ok, got {ok}'
    assert pid == 'player_1', f'player_id mismatch: {pid}'
    assert spawn == (100.0, 100.0), f'spawn mismatch: {spawn}'
    print(f'  [pass] 登录：ok={ok}, player_id={pid}, spawn={spawn}')


def test_sticky_packet(sock):
    # 粘包：一次发两个完整帧
    sock.sendall(encode_frame(encode_login_req('a')) + encode_frame(encode_login_req('b')))
    ok1, pid1, _ = decode_login_ack(recv_frame(sock))
    ok2, pid2, _ = decode_login_ack(recv_frame(sock))
    assert ok1 and ok2 and pid1 == 'player_1' and pid2 == 'player_1'
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


def main():
    if not os.path.exists(SERVER_EXE):
        print(f'服务器不存在：{SERVER_EXE}，请先构建')
        return 1

    proc = subprocess.Popen([SERVER_EXE, str(PORT)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(0.5)  # 等服务器就绪
        sock = socket.create_connection(('127.0.0.1', PORT), timeout=5)
        print('服务器已连接，开始测试：')
        test_login(sock)
        test_sticky_packet(sock)
        test_half_packet(sock)
        test_heartbeat(sock)
        sock.close()
        print('\n全部测试通过')
        return 0
    finally:
        proc.terminate()
        proc.wait()


if __name__ == '__main__':
    sys.exit(main())
