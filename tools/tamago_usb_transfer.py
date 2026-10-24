#!/usr/bin/env python3
"""Serve the Tama-Go import page locally and bridge uploads to USB Serial/JTAG."""
from __future__ import annotations

import argparse
import json
import struct
import time
import webbrowser
import zlib
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parents[1]
PAGE = ROOT / "main/tamago_transfer.html"
SIZES = [655360] + [349536] * 6 + [524288] * 6
USB_VID_PID = (0x303A, 0x1001)
SAVE_MIN = 28 + 4096
SAVE_MAX = SAVE_MIN + 4096


class TransferError(Exception):
    pass


class UsbLink:
    def __init__(self, port):
        self.port = port
        self.pending = bytearray()

    def _reply(self, seconds=20):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            # pyserial read(256) waits for all 256 bytes or the timeout. ACKs
            # are short, so read one byte first and drain what's available.
            data = self.port.read(max(1, min(256, getattr(self.port, "in_waiting", 0))))
            if data:
                self.pending.extend(data)
            while b"\n" in self.pending:
                raw, _, rest = self.pending.partition(b"\n")
                self.pending = bytearray(rest)
                line = raw.decode("ascii", "replace").strip()
                if line.startswith("TG1 "):
                    if line.startswith("TG1 ERR "):
                        raise TransferError(f"设备拒绝操作：{line[8:]}")
                    return line
        raise TransferError("设备无响应；确认屏幕处于 USB 有线传输界面")

    def _command(self, line, expected, seconds=20):
        self.port.write((line + "\n").encode("ascii"))
        self.port.flush()
        reply = self._reply(seconds)
        if not reply.startswith(expected):
            raise TransferError(f"设备回复异常：{reply}")
        return reply

    def _read_exact(self, count):
        result = bytearray()
        if self.pending:
            take = min(count, len(self.pending))
            result.extend(self.pending[:take])
            del self.pending[:take]
        deadline = time.monotonic() + 20
        while len(result) < count:
            part = self.port.read(min(256, count - len(result)))
            if part:
                result.extend(part)
                deadline = time.monotonic() + 20
            elif time.monotonic() >= deadline:
                raise TransferError("存档下载中断")
        return bytes(result)

    def connect(self):
        # Opening a native USB serial port can reset the C3. The selected
        # transfer mode survives the reset, so retry until it boots again.
        deadline = time.monotonic() + 18
        while time.monotonic() < deadline:
            try:
                self.port.write(b"TG1 HELLO\n")
                self.port.flush()
                reply = self._reply(2)
                if reply.startswith("TG1 READY "):
                    return
            except TransferError:
                pass
            time.sleep(0.5)
        raise TransferError("未找到 USB 传输固件；请在设备传输界面按 DOWN 切换到 USB")

    def status(self):
        reply = self._command("TG1 HELLO", "TG1 READY ")
        try:
            mask = int(reply.split()[2], 16)
        except (IndexError, ValueError) as exc:
            raise TransferError("设备状态格式错误") from exc
        return [bool(mask & (1 << slot)) for slot in range(len(SIZES))]

    def upload(self, slot, source):
        if not 0 <= slot < len(SIZES):
            raise TransferError("ROM 编号错误")
        size = SIZES[slot]
        self._command(f"TG1 BEGIN {slot} {size}", "TG1 OK BEGIN", 30)
        written = 0
        while written < size:
            chunk = source.read(min(1024, size - written))
            if not chunk:
                raise TransferError("文件传输中断")
            self.port.write(f"TG1 DATA {len(chunk)}\n".encode("ascii"))
            self.port.write(chunk)
            self.port.flush()
            written += len(chunk)
            reply = self._reply(20)
            if reply != f"TG1 ACK {written}":
                raise TransferError(f"写入进度异常：{reply}")
        self._command("TG1 END", "TG1 OK END", 30)

    def export_save(self):
        header = self._command("TG1 SAVE GET", "TG1 SAVE ")
        try:
            size = int(header.split()[2])
        except (ValueError, IndexError) as exc:
            raise TransferError("存档长度格式错误") from exc
        if not SAVE_MIN <= size <= SAVE_MAX:
            raise TransferError("存档长度不合理")
        data = self._read_exact(size)
        reply = self._reply(20)
        if reply != "TG1 OK SAVE":
            raise TransferError(f"存档下载未完成：{reply}")
        validate_save(data)
        return data

    def import_save(self, data):
        validate_save(data)
        self._command(f"TG1 SAVE BEGIN {len(data)}", "TG1 OK BEGIN")
        written = 0
        while written < len(data):
            chunk = data[written:written + 1024]
            self.port.write(f"TG1 DATA {len(chunk)}\n".encode("ascii"))
            self.port.write(chunk)
            self.port.flush()
            written += len(chunk)
            reply = self._reply(20)
            if reply != f"TG1 ACK {written}":
                raise TransferError(f"存档写入进度异常：{reply}")
        self._command("TG1 END", "TG1 OK END")

    def finish(self):
        self._command("TG1 FINISH", "TG1 OK FINISH")


def validate_save(data):
    if not SAVE_MIN <= len(data) <= SAVE_MAX:
        raise TransferError("存档文件大小不符")
    magic, version, flags, total, rom_hash, session_size, _pages, reserved, crc = \
        struct.unpack_from("<4sHHIIIHHI", data)
    if (magic != b"TGSV" or version != 1 or flags or reserved or not rom_hash or
        total != len(data) or session_size != len(data) - SAVE_MIN or
        zlib.crc32(data[:24] + b"\0\0\0\0" + data[28:]) != crc):
        raise TransferError("存档损坏或版本不支持")


def discover_port():
    from serial.tools import list_ports

    matches = [port.device for port in list_ports.comports()
               if (port.vid, port.pid) == USB_VID_PID]
    if len(matches) != 1:
        raise TransferError("未找到唯一的已知 USB 编号设备；请用 --port 指定 AI Passport 串口")
    return matches[0]


def make_handler(link):
    class Handler(BaseHTTPRequestHandler):
        def _send(self, status, content, content_type="text/plain; charset=utf-8"):
            data = content.encode("utf-8") if isinstance(content, str) else content
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def _same_origin(self):
            origin = self.headers.get("Origin", "")
            return origin in (f"http://127.0.0.1:{self.server.server_port}",
                              f"http://localhost:{self.server.server_port}")

        def do_GET(self):
            path = urlsplit(self.path).path
            try:
                if path == "/":
                    self._send(200, PAGE.read_bytes(), "text/html; charset=utf-8")
                elif path == "/api/status":
                    self._send(200, json.dumps({"slots": link.status()}),
                               "application/json; charset=utf-8")
                elif path == "/api/save":
                    self._send(200, link.export_save(), "application/octet-stream")
                else:
                    self._send(404, "页面不存在")
            except (TransferError, OSError) as exc:
                self._send(502, str(exc))

        def do_POST(self):
            if not self._same_origin():
                self._send(403, "只接受本地传输网页")
                return
            self.connection.settimeout(30)
            parsed = urlsplit(self.path)
            try:
                if parsed.path == "/api/upload":
                    slot = int(parse_qs(parsed.query).get("id", [""])[0])
                    if (not 0 <= slot < len(SIZES) or
                        int(self.headers.get("Content-Length", "-1")) != SIZES[slot]):
                        self._send(400, "ROM 编号或文件大小不符")
                        return
                    link.upload(slot, self.rfile)
                    self._send(200, "导入成功")
                elif parsed.path == "/api/finish":
                    link.finish()
                    self._send(200, "设备即将启动游戏")
                elif parsed.path == "/api/save":
                    length = int(self.headers.get("Content-Length", "-1"))
                    if not SAVE_MIN <= length <= SAVE_MAX:
                        self._send(400, "存档文件大小不符")
                        return
                    data = self.rfile.read(length)
                    if len(data) != length:
                        self._send(400, "存档传输中断")
                        return
                    link.import_save(data)
                    self._send(200, "存档已导入；点击完成并启动游戏")
                else:
                    self._send(404, "接口不存在")
            except (ValueError, TransferError, OSError) as exc:
                self._send(502, str(exc))

        def log_message(self, format, *args):
            pass

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="AI Passport 串口；默认按已知 USB 编号查找")
    parser.add_argument("--listen-port", type=int, default=8765)
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()
    try:
        import serial

        port_name = args.port or discover_port()
        with serial.Serial(port_name, 115200, timeout=0.2, write_timeout=10) as port:
            port.dtr = False
            port.rts = False
            link = UsbLink(port)
            link.connect()
            with HTTPServer(("127.0.0.1", args.listen_port), make_handler(link)) as server:
                url = f"http://127.0.0.1:{args.listen_port}/"
                print(f"Tama-Go USB 传输已就绪：{url}", flush=True)
                if not args.no_browser:
                    webbrowser.open(url)
                server.serve_forever()
    except (ImportError, TransferError, OSError) as exc:
        parser.exit(1, f"USB 传输未启动：{exc}\n")


if __name__ == "__main__":
    main()
