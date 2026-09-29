#!/usr/bin/env python3
# 心跳超时集成测试：验证服务器在客户端停止发消息后主动踢线。
# 用短超时 --heartbeat-timeout-ms 3000 快速验证，不必干等默认的 30s。
#   python tests/test_heartbeat_timeout.py

import os
import socket
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_protocol as tp

SERVER_EXE = os.path.normpath(os.path.join(
    os.path.dirname(__file__), '..', 'build', 'Release', 'openworld_server.exe'))
PORT = 19030
TIMEOUT_MS = 3000  # 服务器短超时，便于快速测试


def main():
    if not os.path.exists(SERVER_EXE):
        print(f'服务器不存在：{SERVER_EXE}，请先构建')
        return 1

    proc = subprocess.Popen(
        [SERVER_EXE, str(PORT), '--no-db', '--heartbeat-timeout-ms', str(TIMEOUT_MS)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(0.5)
        sock = socket.create_connection(('127.0.0.1', PORT), timeout=5)
        pid, spawn = tp.login(sock, 'timeout_user')
        print(f'登录成功：{pid} @ {spawn}')

        # 停止发任何消息（不心跳、不移动），等服务器超时踢线
        print(f'停止发消息，等待服务器在 {TIMEOUT_MS}ms 超时后踢线…')
        sock.settimeout(TIMEOUT_MS / 1000.0 + 5.0)  # 留 5s 余量
        try:
            data = sock.recv(4096)
            closed = (data == b'')  # 收到 EOF = 服务器主动关闭
        except socket.timeout:
            closed = False  # 服务器没踢线 → 测试失败
        except OSError:
            closed = True   # 连接被重置，也算关闭

        assert closed, f'服务器应在 {TIMEOUT_MS}ms 心跳超时后主动关闭连接'
        print('  [pass] 心跳超时：服务器主动踢掉无活动连接')
        return 0
    finally:
        proc.terminate()
        proc.wait()


if __name__ == '__main__':
    sys.exit(main())
