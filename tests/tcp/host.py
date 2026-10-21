#!/usr/bin/env python3
#
# The host's end of the tcp test (tests/tcp/init.c), through QEMU's user
# network: its TCP, between us and the guest's.
#
# usage: host.py <listen port> <guest's port, forwarded>
#
# Takes the guest's connection (2 MiB, its pattern), says "ok" if they
# were right; then connects to the guest's echo server: 4 MiB, then 5
# connections at once. A line per check (PASS/FAIL), then "tcp: done".
#
# The guest's connection comes through QEMU's guestfwd, a character
# device: QEMU connects once when it starts ("listening" says when it
# may), and the guest's FIN doesn't come through, the size says the end.

import socket
import sys
import threading
import time

listen_port, guest_port = int(sys.argv[1]), int(sys.argv[2])
fails = 0


def check(name, ok, detail=""):
    global fails
    print(("PASS " if ok else "FAIL ") + name + (" (" + detail + ")" if detail and not ok else ""), flush=True)
    if not ok:
        fails += 1


def pattern(n, start=0):
    return bytes(((i * 13) + (i >> 11)) & 0xff for i in range(start, start + n))


def recv_n(sock, n):
    chunks, got = [], 0
    while got < n:
        data = sock.recv(min(65536, n - got))
        if not data:
            break
        chunks.append(data)
        got += len(data)
    return b"".join(chunks)


def recv_all(sock):
    chunks = []
    while True:
        data = sock.recv(65536)
        if not data:
            return b"".join(chunks)
        chunks.append(data)


srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", listen_port))
srv.listen(1)
srv.settimeout(60)
print("listening", flush=True)

try:
    conn, _ = srv.accept()
    conn.settimeout(60)
    data = recv_n(conn, 2 * 1024 * 1024)
    ok = data == pattern(2 * 1024 * 1024)
    check("2 MiB from the guest", ok, "%d bytes" % len(data))
    conn.sendall(b"ok" if ok else b"no")
    conn.close()
except OSError as e:
    check("2 MiB from the guest", False, str(e))


def echo(size, seed):
    """size bytes to the echo server and back"""
    s = socket.create_connection(("127.0.0.1", guest_port), timeout=60)
    payload = bytes((i * seed + 7) & 0xff for i in range(size))
    received = []

    def reader():
        received.append(recv_all(s))

    t = threading.Thread(target=reader)
    t.start()
    s.sendall(payload)
    s.shutdown(socket.SHUT_WR)
    t.join(90)
    s.close()
    return bool(received) and received[0] == payload, len(received[0]) if received else -1


# the guest listens before it connects to us: it's there
try:
    ok, n = echo(4 * 1024 * 1024, 3)
    check("4 MiB echoed", ok, "%d bytes" % n)
except OSError as e:
    check("4 MiB echoed", False, str(e))

results = [None] * 5


def one(i):
    try:
        results[i] = echo(200 * 1024, 11 + i)
    except OSError as e:
        results[i] = (False, str(e))


threads = [threading.Thread(target=one, args=(i,)) for i in range(5)]
for t in threads:
    t.start()
for t in threads:
    t.join(120)

check("5 connections at once, echoed", all(r and r[0] for r in results), str(results))

print("tcp: done, fails=%d" % fails, flush=True)
sys.exit(1 if fails else 0)
