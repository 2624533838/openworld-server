#!/usr/bin/env python3
# 压测假客户端：模拟 N 个并发连接，测「登录延迟 + AOI 广播吞吐」。
# 复用 test_protocol.py 的编解码/帧助手。连到一个已启动的服务器：
#   openworld_server.exe 9000 --no-db     # 先起服务器（--no-db 测纯逻辑负载）
#   python tests/bench_client.py --conns 50 --duration 15 --port 9000
# 说明：所有客户端默认出生 (100,100) 同格，构成 O(N²) 广播最坏情况，
#       最能体现 AOI 广播负载。作为 Java 压测客户端就绪前的 Python 替身。

import argparse
import os
import random
import socket
import sys
import threading
import time

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_protocol as tp


def run_client(idx, host, port, deadline, stats):
    """单个压测客户端：登录 → 读线程收广播计数 + 主线程定时发移动/心跳。"""
    try:
        t0 = time.perf_counter()
        sock = socket.create_connection((host, port), timeout=5)
        pid, spawn = tp.login(sock, f'bench{idx}')
        login_ms = (time.perf_counter() - t0) * 1000.0
        with stats['lock']:
            stats['logins'] += 1
            stats['login_lat'].append(login_ms)

        # 读线程：阻塞收帧、只数 MoveBroadcast（不干扰发送，避免粘包状态丢失）
        sock.settimeout(None)  # 完全阻塞，交给读线程；发送端 sendall 不依赖超时
        stop = threading.Event()

        def reader():
            bc = 0
            try:
                while not stop.is_set():
                    payload = tp.recv_frame(sock)
                    if tp.envelope_type(payload) == tp.T_MOVE_BROADCAST:
                        bc += 1
            except OSError:
                pass  # 连接关闭，结束
            with stats['lock']:
                stats['broadcasts'] += bc

        rt = threading.Thread(target=reader, daemon=True)
        rt.start()

        # 主线程：每 0.2s 发一个随机低速移动意图，偶尔补心跳保活
        seq = 0
        while time.perf_counter() < deadline:
            seq += 1
            dx, dy = random.uniform(-1, 1), random.uniform(-1, 1)
            ln = (dx * dx + dy * dy) ** 0.5
            if ln < 1e-4:
                dx, dy, ln = 1.0, 0.0, 1.0
            try:
                sock.sendall(tp.encode_frame(tp.encode_move_req(
                    dx / ln, dy / ln, random.uniform(0, 5), seq)))
                if seq % 5 == 0:
                    sock.sendall(tp.encode_frame(
                        tp.encode_heartbeat(int(time.time() * 1000))))
            except OSError:
                break
            time.sleep(0.2)  # ~5 次移动/s/客户端

        stop.set()
        try:
            sock.close()
        except OSError:
            pass
        rt.join(timeout=1.0)
    except Exception:
        with stats['lock']:
            stats['failures'] += 1


def main():
    ap = argparse.ArgumentParser(description='openworld 压测假客户端')
    ap.add_argument('--conns', type=int, default=50, help='并发连接数')
    ap.add_argument('--duration', type=int, default=15, help='压测时长（秒）')
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=9000)
    args = ap.parse_args()

    stats = {'logins': 0, 'failures': 0, 'broadcasts': 0,
             'login_lat': [], 'lock': threading.Lock()}
    deadline = time.perf_counter() + args.duration
    threads = [threading.Thread(target=run_client,
                                args=(i, args.host, args.port, deadline, stats))
               for i in range(args.conns)]

    t0 = time.perf_counter()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    wall = time.perf_counter() - t0

    lat = sorted(stats['login_lat'])
    avg = sum(lat) / len(lat) if lat else 0.0
    p99 = lat[int(len(lat) * 0.99)] if lat else 0.0

    print(f'连接数: {args.conns}, 时长: {wall:.1f}s')
    print(f'登录成功: {stats["logins"]}/{args.conns}, 失败: {stats["failures"]}')
    print(f'登录延迟: avg={avg:.1f}ms, p99={p99:.1f}ms')
    per_client = stats['broadcasts'] / wall / args.conns if args.conns else 0
    print(f'收到 MoveBroadcast: {stats["broadcasts"]}, '
          f'吞吐: {stats["broadcasts"] / wall:.0f} msg/s '
          f'(每客户端 {per_client:.0f} msg/s)')


if __name__ == '__main__':
    main()
