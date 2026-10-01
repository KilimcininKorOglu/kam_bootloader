#!/usr/bin/env python3
"""shot_boot.py: boot QEMU, screenshot the display over QMP, and assert the
KAM GOP header pixels. The framebuffer persists after ExitBootServices, so
the shot can land any time after the header is drawn.
Usage: shot_boot.py LOG SHOT_AT SECS -- qemu... [args...]
Asserts: (5,5) orange header, (w-5,h-5) dark background, (25,80) white bar,
(0,0) white frame. Resolution comes from the serial log 'GOP WxH' line.
"""
import json
import os
import re
import socket
import subprocess
import sys
import time

# Must match the constants and geometry in src/uefi/gop.c.
ORANGE = (0xB4, 0x5A, 0x00)
BG = (0x0A, 0x14, 0x1E)
WHITE = (0xFF, 0xFF, 0xFF)


def qmp(sock_path, cmd, timeout=20):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(sock_path)
    f = s.makefile("rwb")
    f.readline()  # greeting
    f.write(json.dumps({"execute": "qmp_capabilities"}).encode() + b"\n")
    f.flush()
    f.readline()  # ack
    f.write(json.dumps(cmd).encode() + b"\n")
    f.flush()
    resp = json.loads(f.readline().decode())
    f.close()
    s.close()
    return resp


def read_ppm(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        dims = f.readline().split()
        while dims[0].startswith(b"#"):
            dims = f.readline().split()
        w, h = int(dims[0]), int(dims[1])
        maxv = f.readline().split()
        assert int(maxv[0]) == 255
        px = f.read(w * h * 3)
    assert len(px) == w * h * 3
    return w, h, px


def main():
    log, shot_at, secs = sys.argv[1:4]
    assert sys.argv[4] == "--"
    qemu = sys.argv[5:]
    shot_at = float(shot_at)
    secs = int(secs)
    sock = log + ".qmp"
    ppm = log + ".ppm"
    for p in (sock, ppm):
        try:
            os.unlink(p)
        except OSError:
            pass

    out = open(log, "wb")
    proc = subprocess.Popen(qemu + ["-qmp", f"unix:{sock},server=on,wait=off"],
                            stdin=subprocess.DEVNULL, stdout=out,
                            stderr=subprocess.STDOUT)
    try:
        time.sleep(shot_at)
        resp = qmp(sock, {"execute": "screendump",
                          "arguments": {"filename": ppm, "format": "ppm"}})
        assert "return" in resp, resp
        time.sleep(2)
    finally:
        proc.kill()
        proc.wait()
    out.close()

    with open(log, "rb") as f:
        data = f.read().replace(b"\0", b"")
    m = re.search(rb"GOP (\d+)x(\d+)", data)
    if not m:
        print("FAIL: no GOP mode line (fallback text mode?)")
        return 1
    w, h = int(m.group(1)), int(m.group(2))
    pw, ph, px = read_ppm(ppm)
    assert (pw, ph) == (w, h), ((pw, ph), (w, h))

    def at(x, y):
        o = (y * w + x) * 3
        return (px[o], px[o + 1], px[o + 2])

    checks = [("header", (5, 5), ORANGE), ("background", (w - 5, h - 5), BG),
              ("bar", (25, 80), WHITE), ("frame", (0, 0), WHITE)]
    ok = True
    for name, (x, y), want in checks:
        got = at(x, y)
        status = "ok" if got == want else "MISMATCH"
        if got != want:
            ok = False
        print(f"{name} ({x},{y}): got {got} want {want} {status}")
    print("PASS: GOP header pixels verified" if ok else "FAIL: pixel mismatch")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
