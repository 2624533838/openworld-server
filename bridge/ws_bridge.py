#!/usr/bin/env python3
# WebSocket 桥接：浏览器（WebSocket）↔ 游戏服务器（原生 TCP + protobuf）。
# 纯 Python 标准库，零第三方依赖。同时把 demo/index.html 当静态页托管。
#
# 用法：
#   python bridge/ws_bridge.py [game_port=9000] [http_port=8080]
# 然后浏览器打开 http://localhost:8080/ 即可。
#
# 桥接只做三件事，不解析 protobuf：
#   1. WebSocket 服务端（握手 + 帧编解码，浏览器是客户端）
#   2. 静态页托管（GET / -> demo/index.html）
#   3. TCP 转发：浏览器二进制帧 <-> 游戏服务器（4 字节大端长度前缀帧）

import base64
import hashlib
import os
import socket
import socketserver
import struct
import sys
import threading

GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
DOC_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', 'demo'))

GAME_HOST = '127.0.0.1'
GAME_PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9000
HTTP_PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8080


class BridgeHandler(socketserver.BaseRequestHandler):
    def setup(self):
        self.sock = self.request
        self.buf = b''          # 读缓冲区（兼容 WS 帧紧跟 HTTP 头到达）
        self.game = None        # 到游戏服务器的 TCP 连接
        self.closed = False
        self.wlock = threading.Lock()

    def handle(self):
        try:
            head = self._read_until(b'\r\n\r\n')
            if head is None:
                return
            request_line, _, rest = head.partition(b'\r\n')
            parts = request_line.decode('latin1').split()
            if len(parts) < 2:
                return
            path = parts[1].split('?')[0]
            headers = {}
            for line in rest.split(b'\r\n'):
                if b':' in line:
                    k, v = line.split(b':', 1)
                    headers[k.strip().lower()] = v.strip()

            if headers.get(b'upgrade', b'').lower() == b'websocket' and path == '/ws':
                self._ws_handshake(headers)
                self._ws_loop()
            else:
                self._serve_file(path)
        except (ConnectionError, OSError):
            pass

    # ---- 原始字节读取（带缓冲）----
    def _read_until(self, marker):
        while marker not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                return None
            self.buf += chunk
        idx = self.buf.index(marker) + len(marker)
        data, self.buf = self.buf[:idx], self.buf[idx:]
        return data

    def _recv_exact(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError('closed')
            self.buf += chunk
        data, self.buf = self.buf[:n], self.buf[n:]
        return data

    # ---- 静态页托管 ----
    def _serve_file(self, path):
        if path in ('/', '/index.html'):
            fp = os.path.join(DOC_ROOT, 'index.html')
            try:
                data = open(fp, 'rb').read()
            except OSError:
                data = b''
            if data:
                resp = (b'HTTP/1.1 200 OK\r\n'
                        b'Content-Type: text/html; charset=utf-8\r\n'
                        b'Content-Length: ' + str(len(data)).encode() +
                        b'\r\nConnection: close\r\n\r\n' + data)
            else:
                resp = (b'HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n'
                        b'Connection: close\r\n\r\n')
        else:
            body = b'404 Not Found'
            resp = (b'HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n'
                    b'Content-Length: ' + str(len(body)).encode() +
                    b'\r\nConnection: close\r\n\r\n' + body)
        self.sock.sendall(resp)

    # ---- WebSocket 握手 + 帧编解码 ----
    def _ws_handshake(self, headers):
        key = headers.get(b'sec-websocket-key', b'').decode('latin1')
        accept = base64.b64encode(hashlib.sha1((key + GUID).encode('latin1')).digest())
        resp = (b'HTTP/1.1 101 Switching Protocols\r\n'
                b'Upgrade: websocket\r\n'
                b'Connection: Upgrade\r\n'
                b'Sec-WebSocket-Accept: ' + accept + b'\r\n\r\n')
        self.sock.sendall(resp)

    def _ws_read(self):
        b1, b2 = self._recv_exact(2)
        opcode = b1 & 0x0F
        masked = b2 & 0x80
        length = b2 & 0x7F
        if length == 126:
            length = struct.unpack('>H', self._recv_exact(2))[0]
        elif length == 127:
            length = struct.unpack('>Q', self._recv_exact(8))[0]
        mask = self._recv_exact(4) if masked else b''
        payload = self._recv_exact(length) if length else b''
        if masked:
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        return opcode, payload

    def _ws_send(self, payload, opcode=0x2):
        n = len(payload)
        head = bytearray([0x80 | opcode])
        if n < 126:
            head.append(n)
        elif n < 65536:
            head.append(126)
            head += struct.pack('>H', n)
        else:
            head.append(127)
            head += struct.pack('>Q', n)
        with self.wlock:
            self.sock.sendall(bytes(head) + payload)

    # ---- 游戏服务器 TCP（长度前缀帧）----
    def _game_connect(self):
        self.game = socket.create_connection((GAME_HOST, GAME_PORT), timeout=5)
        threading.Thread(target=self._game_reader, daemon=True).start()

    def _game_reader(self):
        try:
            while not self.closed:
                hdr = self._tcp_recv_exact(4)
                if hdr is None:
                    break
                (length,) = struct.unpack('>I', hdr)
                payload = self._tcp_recv_exact(length)
                if payload is None:
                    break
                self._ws_send(payload)   # 服务器回包 → 浏览器二进制帧
        except (ConnectionError, OSError):
            pass
        finally:
            self.closed = True
            try:
                self.sock.close()
            except OSError:
                pass

    def _tcp_recv_exact(self, n):
        data = b''
        while len(data) < n:
            chunk = self.game.recv(n - len(data))
            if not chunk:
                return None
            data += chunk
        return data

    def _ws_loop(self):
        self._game_connect()
        try:
            while not self.closed:
                opcode, payload = self._ws_read()
                if opcode == 0x8:        # close
                    break
                if opcode == 0x9:        # ping → pong
                    self._ws_send(payload, 0xA)
                    continue
                if opcode in (0x1, 0x2):  # text/binary = protobuf Envelope
                    self.game.sendall(struct.pack('>I', len(payload)) + payload)
        except (ConnectionError, OSError):
            pass
        finally:
            self.closed = True
            if self.game:
                try:
                    self.game.close()
                except OSError:
                    pass


class BridgeServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    print('OpenWorld WebSocket 桥接已启动')
    print(f'  网页:    http://localhost:{HTTP_PORT}/')
    print(f'  游戏服:  {GAME_HOST}:{GAME_PORT}')
    server = BridgeServer(('0.0.0.0', HTTP_PORT), BridgeHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
