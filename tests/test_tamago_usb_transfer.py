"""Exercise the desktop USB bridge's chunk framing and error handling."""
from __future__ import annotations

import importlib.util
import io
import json
import struct
import threading
import unittest
import zlib
from http.server import HTTPServer
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "tamago_usb_transfer", ROOT / "tools/tamago_usb_transfer.py")
transfer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(transfer)


class FakePort:
    def __init__(self):
        self.replies = bytearray()
        self.size = 0
        self.written = 0
        self.awaiting = 0
        self.present = False
        self.chunks = 0
        self.save_mode = False
        self.payload = bytearray()
        self.save_data = sample_save()

    def read(self, n):
        data = bytes(self.replies[:n])
        del self.replies[:n]
        return data

    def write(self, data):
        if self.awaiting:
            assert len(data) == self.awaiting
            self.written += len(data)
            if self.save_mode:
                self.payload.extend(data)
            self.chunks += 1
            self.awaiting = 0
            self.replies.extend(f"TG1 ACK {self.written}\n".encode())
        else:
            command = data.decode().strip().split()
            if command[:2] == ["TG1", "HELLO"]:
                self.replies.extend(b"TG1 READY 0001\n" if self.present
                                    else b"TG1 READY 0000\n")
            elif command[:2] == ["TG1", "BEGIN"]:
                self.save_mode = False
                self.size = int(command[3])
                self.written = 0
                self.replies.extend(b"TG1 OK BEGIN\n")
            elif command[:3] == ["TG1", "SAVE", "GET"]:
                self.replies.extend(f"TG1 SAVE {len(self.save_data)}\n".encode())
                self.replies.extend(self.save_data)
                self.replies.extend(b"TG1 OK SAVE\n")
            elif command[:3] == ["TG1", "SAVE", "BEGIN"]:
                self.save_mode = True
                self.payload.clear()
                self.size = int(command[3])
                self.written = 0
                self.replies.extend(b"TG1 OK BEGIN\n")
            elif command[:2] == ["TG1", "DATA"]:
                self.awaiting = int(command[2])
            elif command[:2] == ["TG1", "END"]:
                assert self.written == self.size
                if self.save_mode:
                    self.save_data = bytes(self.payload)
                else:
                    self.present = True
                self.replies.extend(b"TG1 OK END\n")
            elif command[:2] == ["TG1", "FINISH"]:
                self.replies.extend(b"TG1 OK FINISH\n" if self.present
                                    else b"TG1 ERR MAIN\n")
            else:
                raise AssertionError(command)
        return len(data)

    def flush(self):
        pass


def sample_save():
    data = bytearray(28 + 4096)
    struct.pack_into("<4sHHIIIHHI", data, 0,
                     b"TGSV", 1, 0, len(data), 0x12345678, 0, 1, 0, 0)
    struct.pack_into("<I", data, 24, zlib.crc32(data))
    return bytes(data)


class UsbTransferTests(unittest.TestCase):
    def test_streaming_main_rom_and_status(self):
        port = FakePort()
        link = transfer.UsbLink(port)
        self.assertFalse(link.status()[0])
        image = io.BytesIO(b"x" * transfer.SIZES[0])
        link.upload(0, image)
        self.assertGreater(port.chunks, 100)
        self.assertEqual(port.written, transfer.SIZES[0])
        self.assertTrue(link.status()[0])
        link.finish()

    def test_missing_main_and_short_stream(self):
        link = transfer.UsbLink(FakePort())
        with self.assertRaises(transfer.TransferError):
            link.finish()
        with self.assertRaisesRegex(transfer.TransferError, "中断"):
            link.upload(0, io.BytesIO(b"too short"))

    def test_save_roundtrip_and_corruption_rejection(self):
        port = FakePort()
        link = transfer.UsbLink(port)
        backup = link.export_save()
        self.assertEqual(backup, sample_save())
        corrupt = bytearray(backup)
        corrupt[-1] ^= 1
        with self.assertRaisesRegex(transfer.TransferError, "损坏"):
            link.import_save(corrupt)
        link.import_save(backup)
        self.assertEqual(port.save_data, backup)
        self.assertEqual(link.export_save(), backup)

    def test_local_page_status_and_upload_rejection(self):
        link = transfer.UsbLink(FakePort())
        with HTTPServer(("127.0.0.1", 0), transfer.make_handler(link)) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            base = f"http://127.0.0.1:{server.server_port}"
            try:
                with urlopen(base + "/") as response:
                    self.assertIn(b"Tama-Go", response.read())
                with urlopen(base + "/api/status") as response:
                    self.assertEqual(json.load(response)["slots"], [False] * 13)
                with urlopen(base + "/api/save") as response:
                    backup = response.read()
                    self.assertEqual(backup, sample_save())
                restore = Request(base + "/api/save", data=backup,
                                  headers={"Origin": base}, method="POST")
                with urlopen(restore) as response:
                    self.assertEqual(response.status, 200)
                request = Request(base + "/api/upload?id=0", data=b"short",
                                  headers={"Origin": base}, method="POST")
                with self.assertRaises(HTTPError) as error:
                    urlopen(request)
                self.assertEqual(error.exception.code, 400)
            finally:
                server.shutdown()
                thread.join(timeout=2)


if __name__ == "__main__":
    unittest.main()
