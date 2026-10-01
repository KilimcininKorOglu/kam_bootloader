#!/usr/bin/env python3
"""drive_boot.py: run QEMU, hold menu keys on the serial console, and pass
only if EXPECT appears. The firmware auto-boots our partitioned ESP
(Boot0001), so no shell driving is needed: keys go out from t=1s, paced
0.5s, covering the whole menu window. Digits beyond the entry count and
keys outside the menu are ignored by the app.
Usage: drive_boot.py LOG SECS EXPECT KEYS KEYSECS -- qemu... [args...]
KEYS empty = let the menu time out (default entry).
"""
import subprocess
import sys
import threading
import time


def main():
    log, secs, expect, keys, keysecs = sys.argv[1:6]
    assert sys.argv[6] == "--"
    qemu = sys.argv[7:]
    secs = int(secs)
    keysecs = float(keysecs)
    # Backslash escapes in KEYS: \r = carriage return (submit).
    keys = keys.replace("\\r", "\r")

    out = open(log, "wb")
    proc = subprocess.Popen(qemu, stdin=subprocess.PIPE, stdout=out,
                            stderr=subprocess.STDOUT)

    def feed():
        try:
            time.sleep(1)
            end = time.time() + keysecs
            while time.time() < end:
                if keys:
                    proc.stdin.write(keys.encode("ascii"))
                    proc.stdin.flush()
                time.sleep(0.5)
        except (BrokenPipeError, ValueError):
            pass

    t = threading.Thread(target=feed, daemon=True)
    t.start()
    try:
        proc.wait(timeout=secs)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    out.close()
    with open(log, "rb") as f:
        data = f.read()
    if expect.encode("ascii") in data:
        print(f"PASS: '{expect}' found in {log}")
        return 0
    print(f"FAIL: '{expect}' missing in {log}")
    tail = data.replace(b"\0", b"")[-2000:].decode("utf-8", "replace")
    print("\n".join(tail.splitlines()[-12:]))
    return 1


if __name__ == "__main__":
    sys.exit(main())
