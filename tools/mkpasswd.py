#!/usr/bin/env python3
"""mkpasswd.py: generate KAM.INI password lines.
Stores only a salted SHA-256 hash: the password itself is never written
anywhere, so it cannot be recovered from the config (one-way).
Usage: mkpasswd.py --password kamboot [--salt COST164] [--timeout 5]
"""
import argparse
import hashlib
import secrets


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--password", required=True)
    ap.add_argument("--salt", default=None)
    ap.add_argument("--timeout", type=int, default=5)
    a = ap.parse_args()
    salt = a.salt if a.salt is not None else f"C{secrets.randbelow(1 << 63):016x}"
    digest = hashlib.sha256((salt + a.password).encode()).hexdigest()
    print(f"timeout {a.timeout}")
    print("default 1")
    print(f"password_salt {salt}")
    print(f"password_hash {digest}")


if __name__ == "__main__":
    main()
