#!/usr/bin/env python3
"""Send commands to the LED board through ledbridge.py.

    ledctl.py get
    ledctl.py rgb 255 0 0
    ledctl.py '{"v":1,"cmd":"set","effect":"rainbow"}'
    ledctl.py --watch 10          # print everything the board says for 10 s
    ledctl.py wifi-setup          # asks SSID + password (hidden) and stores them on the board
"""
import argparse
import getpass
import json
import socket
import sys
import time


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("command", nargs="*")
    ap.add_argument("--bridge", default="127.0.0.1:7777")
    ap.add_argument("--timeout", type=float, default=2.0)
    ap.add_argument("--watch", type=float, default=0, help="just print board output for N seconds")
    args = ap.parse_args()

    host, port = args.bridge.rsplit(":", 1)
    sock = socket.create_connection((host, int(port)), timeout=3)
    sock.settimeout(0.2)

    line = " ".join(args.command)
    if line == "wifi-setup":
        # Typed by the user in their own terminal; never printed or logged.
        ssid = input("SSID: ").strip()
        password = getpass.getpass("Password (hidden): ")
        line = json.dumps({"v": 1, "cmd": "wifi", "ssid": ssid, "pass": password})
        args.timeout = max(args.timeout, 3)
    if line:
        sock.sendall(line.encode() + b"\n")

    deadline = time.time() + (args.watch or args.timeout)
    buf = b""
    while time.time() < deadline:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue
        if not chunk:
            break
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            text = raw.decode(errors="replace")
            print(text)
            if args.watch or not line:
                continue
            # A reply to our command ends the wait.
            try:
                msg = json.loads(text)
            except ValueError:
                continue
            if "cmd" in msg or "error" in msg:
                return 0 if "error" not in msg else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
