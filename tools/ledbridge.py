#!/usr/bin/env python3
"""Keeps the board's serial port open (opening it resets the ESP32) and shares it over TCP.

Every serial line is appended to logs/serial.log and broadcast to connected clients.
Lines received from clients are written to the board.

    .venv/bin/python tools/ledbridge.py [--port /dev/cu.usbserial-1140] [--listen 127.0.0.1:7777]
"""
import argparse
import asyncio
import datetime
import pathlib
import re

import serial

ROOT = pathlib.Path(__file__).resolve().parent.parent
SECRET_RE = re.compile(r'"(pass|password|token|key)"\s*:\s*"(?:[^"\\]|\\.)*"')
clients: set[asyncio.StreamWriter] = set()


def open_serial(port: str) -> serial.Serial:
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0
    s.dtr = s.rts = False
    s.open()
    return s


async def pump_serial(ser: serial.Serial, log) -> None:
    buf = b""
    while True:
        chunk = ser.read(4096)
        if not chunk:
            await asyncio.sleep(0.01)
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.rstrip(b"\r")
            stamp = datetime.datetime.now().isoformat(timespec="milliseconds")
            log.write(f"{stamp} < {line.decode(errors='replace')}\n")
            log.flush()
            for w in list(clients):
                try:
                    w.write(line + b"\n")
                except Exception:
                    clients.discard(w)


async def serve_client(reader, writer, ser: serial.Serial, log) -> None:
    clients.add(writer)
    try:
        while line := await reader.readline():
            stamp = datetime.datetime.now().isoformat(timespec="milliseconds")
            text = SECRET_RE.sub(r'"\1":"***"', line.decode(errors="replace").rstrip())
            log.write(f"{stamp} > {text}\n")
            ser.write(line if line.endswith(b"\n") else line + b"\n")
    finally:
        clients.discard(writer)
        writer.close()


async def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbserial-1140")
    ap.add_argument("--listen", default="127.0.0.1:7777")
    args = ap.parse_args()

    (ROOT / "logs").mkdir(exist_ok=True)
    log = open(ROOT / "logs" / "serial.log", "a", encoding="utf-8")
    ser = open_serial(args.port)
    host, port = args.listen.rsplit(":", 1)
    server = await asyncio.start_server(lambda r, w: serve_client(r, w, ser, log), host, int(port))
    print(f"bridge {args.port} <-> {args.listen}", flush=True)
    async with server:
        await asyncio.gather(server.serve_forever(), pump_serial(ser, log))


if __name__ == "__main__":
    asyncio.run(main())
