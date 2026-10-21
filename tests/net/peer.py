#!/usr/bin/env python3
#
# The other end of the guest's e1000: QEMU sends its frames here as UDP
# datagrams (-netdev socket,udp=), and takes the ones sent back.
#
# usage: peer.py <our port> <QEMU's port>
#
# Plays 10.0.2.2 (the guest's gateway) and 10.0.2.3 on the guest's
# 10.0.2.0/24 (the guest is 10.0.2.15) and prints a line per check, as
# aqtest does: PASS/FAIL <name>, then "net: done".
#
# Its own checks first (ARP, pings), then it sends "go" to the guest's
# test program (tests/net/init.c) and answers it, step by step; last, it
# plays a TCP peer the guest connects to (10.0.2.2:6000), segment by
# segment: losses, a segment out of order, a zero window, a reset.

import socket
import struct
import sys
import time

GUEST_IP = bytes([10, 0, 2, 15])
PEER_IP  = bytes([10, 0, 2, 2])
OTHER_IP = bytes([10, 0, 2, 3])
FAR_IP   = bytes([192, 0, 2, 1])
PEER_MAC = bytes.fromhex("525500000202")
GUEST_MAC_QEMU = bytes.fromhex("525400123456")
BROADCAST = b"\xff" * 6

us, qemu = int(sys.argv[1]), int(sys.argv[2])
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("127.0.0.1", us))
sock.settimeout(2)

fails = 0


def check(name, ok, detail=""):
    global fails
    print(("PASS " if ok else "FAIL ") + name + (" (" + detail + ")" if detail and not ok else ""))
    if not ok:
        fails += 1


def checksum(data):
    if len(data) % 2:
        data += b"\0"
    s = sum(struct.unpack("!%dH" % (len(data) // 2), data))
    while s >> 16:
        s = (s & 0xffff) + (s >> 16)
    return ~s & 0xffff


def send(frame):
    sock.sendto(frame, ("127.0.0.1", qemu))


arp_asked = set()


def receive(want, tries=20):
    """the next frame `want' takes, None if none comes; the guest's ARP
    requests for our addresses are answered meanwhile"""
    for _ in range(tries):
        try:
            frame, _ = sock.recvfrom(4096)
        except socket.timeout:
            return None
        if want(frame):
            return frame
        if frame[12:14] == b"\x08\x06" and frame[20:22] == b"\x00\x01" and \
                frame[38:42] in (PEER_IP, OTHER_IP):
            arp_asked.add(frame[38:42])
            send(ether(frame[22:28], 0x0806, struct.pack("!HHBBH6s4s6s4s", 1, 0x0800, 6, 4, 2,
                       PEER_MAC, frame[38:42], frame[22:28], frame[28:32])))
    return None


def ether(dst, type_, payload):
    return dst + PEER_MAC + struct.pack("!H", type_) + payload


def arp_request(target):
    return ether(BROADCAST, 0x0806, struct.pack("!HHBBH6s4s6s4s", 1, 0x0800, 6, 4, 1,
                                                PEER_MAC, PEER_IP, b"\0" * 6, target))


def ipv4(dst_mac, dst_ip, proto, payload, ident=1, src=PEER_IP):
    h = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(payload), ident, 0x4000, 64, proto, 0,
                    src, dst_ip)
    h = h[:10] + struct.pack("!H", checksum(h)) + h[12:]
    return ether(dst_mac, 0x0800, h + payload)


def udp(src_ip, dst_ip, sport, dport, data):
    h = struct.pack("!HHHH", sport, dport, 8 + len(data), 0) + data
    pseudo = src_ip + dst_ip + struct.pack("!BBH", 0, 17, len(h))
    return h[:6] + struct.pack("!H", checksum(pseudo + h) or 0xffff) + h[8:]


def guest_ipv4(proto, dst=None):
    """a frame from the guest of that protocol (to `dst'), with a right
    header: (frame, IP header, payload)"""
    def want(frame):
        if len(frame) < 34 or frame[12:14] != b"\x08\x00":
            return False
        ip = frame[14:34]
        return (ip[9] == proto and ip[12:16] == GUEST_IP and checksum(ip) == 0
                and (dst is None or ip[16:20] == dst))
    return want


def split(frame):
    total = struct.unpack("!H", frame[16:18])[0]
    return frame[14:34], frame[34:14 + total]


def udp_ok(ip, u):
    """its checksum is right (or none)"""
    pseudo = ip[12:20] + struct.pack("!BBH", 0, 17, len(u))
    return u[6:8] == b"\0\0" or checksum(pseudo + u) == 0


def echo_request(ident, seq, data):
    h = struct.pack("!BBHHH", 8, 0, 0, ident, seq) + data
    return h[:2] + struct.pack("!H", checksum(h)) + h[4:]


def is_echo_reply(ident, seq, data):
    def want(frame):
        if len(frame) < 14 + 20 + 8 or frame[12:14] != b"\x08\x00":
            return False
        ip = frame[14:34]
        if ip[9] != 1 or ip[12:16] != GUEST_IP or ip[16:20] != PEER_IP or checksum(ip) != 0:
            return False
        total = struct.unpack("!H", ip[2:4])[0]
        icmp = frame[34:14 + total]
        return (icmp[0] == 0 and struct.unpack("!HH", icmp[4:8]) == (ident, seq)
                and icmp[8:] == data and checksum(icmp) == 0)
    return want


# ARP: who has 10.0.2.15
send(arp_request(GUEST_IP))
reply = receive(lambda f: f[12:14] == b"\x08\x06" and f[20:22] == b"\x00\x02")
check("arp reply", reply is not None)
guest_mac = reply[22:28] if reply else GUEST_MAC_QEMU
if reply:
    check("arp reply: its address", reply[28:32] == GUEST_IP and reply[6:12] == guest_mac)
    check("arp reply: to us", reply[0:6] == PEER_MAC and reply[32:38] == PEER_MAC and reply[38:42] == PEER_IP)

# not for it: no answer
send(arp_request(bytes([10, 0, 2, 99])))
check("no arp reply for another address",
      receive(lambda f: f[12:14] == b"\x08\x06", tries=1) is None)

# ping
data = b"aquila-ping-" + bytes(range(32))
send(ipv4(guest_mac, GUEST_IP, 1, echo_request(0x1234, 1, data)))
check("echo reply", receive(is_echo_reply(0x1234, 1, data)) is not None)

# a big one
data = bytes(i & 0xff for i in range(1400))
send(ipv4(guest_mac, GUEST_IP, 1, echo_request(0x1234, 2, data)))
check("echo reply, 1400 bytes", receive(is_echo_reply(0x1234, 2, data)) is not None)

# many: around the rings (32 descriptors each) a few times
ok = 0
for seq in range(100, 200):
    d = struct.pack("!I", seq) * 8
    send(ipv4(guest_mac, GUEST_IP, 1, echo_request(0x4321, seq, d), ident=seq))
    if receive(is_echo_reply(0x4321, seq, d)):
        ok += 1
check("100 echo replies", ok == 100, "%d" % ok)

# not for it: no answer
send(ipv4(guest_mac, bytes([10, 0, 2, 99]), 1, echo_request(1, 1, b"x")))
check("no echo reply for another address", receive(lambda f: f[12:14] == b"\x08\x00", tries=1) is None)

# a closed port: "port unreachable", with our datagram's header in it
send(ipv4(guest_mac, GUEST_IP, 17, udp(PEER_IP, GUEST_IP, 5000, 9999, b"anyone?")))
f = receive(guest_ipv4(1, PEER_IP))
ok = False
if f:
    ip, icmp = split(f)
    inner = icmp[8:]
    ok = (icmp[0] == 3 and icmp[1] == 3 and checksum(icmp) == 0 and inner[9] == 17
          and inner[12:20] == PEER_IP + GUEST_IP and struct.unpack("!H", inner[22:24])[0] == 9999)
check("port unreachable from the guest", ok)

# the guest's program: "go" until it answers (it may not be listening yet,
# then it says "port unreachable", which is ignored)
f = None
for _ in range(60):
    send(ipv4(guest_mac, GUEST_IP, 17, udp(PEER_IP, GUEST_IP, 5000, 9000, b"go")))
    f = receive(guest_ipv4(17, OTHER_IP), tries=2)
    if f:
        break
check("guest: udp to 10.0.2.3", f is not None)
check("guest: asked ARP for 10.0.2.3", OTHER_IP in arp_asked)
if f:
    ip, u = split(f)
    sport, dport = struct.unpack("!HH", u[0:4])
    check("guest: udp to 10.0.2.3, the datagram", udp_ok(ip, u) and dport == 5001 and
          u[8:] == b"aquila-udp" and f[0:6] == PEER_MAC and f[6:12] == guest_mac)
    send(ipv4(guest_mac, GUEST_IP, 17, udp(OTHER_IP, GUEST_IP, 5001, sport, u[8:]), src=OTHER_IP))

# its ping
f = receive(guest_ipv4(1, PEER_IP))
ok = False
if f:
    ip, icmp = split(f)
    ok = icmp[0] == 8 and checksum(icmp) == 0
    if ok:
        reply = b"\0\0\0\0" + icmp[4:]
        reply = reply[:2] + struct.pack("!H", checksum(reply)) + reply[4:]
        send(ipv4(guest_mac, GUEST_IP, 1, reply))
check("guest: echo request", ok)

# to a closed port here
f = receive(guest_ipv4(17, PEER_IP))
ok = False
if f:
    ip, u = split(f)
    ok = udp_ok(ip, u) and struct.unpack("!H", u[2:4])[0] == 5002
    icmp = struct.pack("!BBHI", 3, 3, 0, 0) + ip + u[:8]
    icmp = icmp[:2] + struct.pack("!H", checksum(icmp)) + icmp[4:]
    send(ipv4(guest_mac, GUEST_IP, 1, icmp))
check("guest: udp to a closed port", ok)

# through the gateway
f = receive(guest_ipv4(17, FAR_IP))
ok = False
if f:
    ip, u = split(f)
    sport, dport = struct.unpack("!HH", u[0:4])
    ok = udp_ok(ip, u) and dport == 5003 and f[0:6] == PEER_MAC
    send(ipv4(guest_mac, GUEST_IP, 17, udp(FAR_IP, GUEST_IP, 5003, sport, u[8:]), src=FAR_IP))
check("guest: udp through the gateway", ok)

# broadcast
f = receive(guest_ipv4(17, bytes([10, 0, 2, 255])))
ok = False
if f:
    ip, u = split(f)
    ok = udp_ok(ip, u) and f[0:6] == BROADCAST and u[8:] == b"everyone"
check("guest: udp broadcast", ok)

# TCP, scripted: the guest connects to 10.0.2.2:6000

F, S, R, P, A = 1, 2, 4, 8, 16


def tcp_seg(dport, seq, ack, flags, win=65535, data=b"", opts=b"", sport=6000):
    h = struct.pack("!HHIIBBHHH", sport, dport, seq & 0xffffffff, ack & 0xffffffff,
                    ((20 + len(opts)) // 4) << 4, flags, win, 0, 0) + opts + data
    pseudo = PEER_IP + GUEST_IP + struct.pack("!BBH", 0, 6, len(h))
    return h[:16] + struct.pack("!H", checksum(pseudo + h)) + h[18:]


def tcp_send(dport, seq, ack, flags, win=65535, data=b"", opts=b"", sport=6000):
    send(ipv4(guest_mac, GUEST_IP, 6, tcp_seg(dport, seq, ack, flags, win, data, opts, sport)))


class Seg:
    pass


def guest_tcp(port=6000, timeout=3.0, data=False):
    """the guest's next segment to `port' (with data, if `data')"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        f = receive(guest_ipv4(6), tries=1)
        if not f:
            continue
        ip, t = split(f)
        pseudo = ip[12:20] + struct.pack("!BBH", 0, 6, len(t))
        sp, dp, seq, ack, off, fl, win = struct.unpack("!HHIIBBH", t[:16])
        if dp != port or checksum(pseudo + t) != 0:
            continue
        s = Seg()
        hl = (off >> 4) * 4
        s.sport, s.seq, s.ack, s.flags, s.win = sp, seq, ack, fl, win
        s.opts, s.data, s.t = t[20:hl], t[hl:], time.time()
        if data and not s.data:
            continue
        return s
    return None


def mss_of(opts):
    i = 0
    while i < len(opts) and opts[i] != 0:
        if opts[i] == 1:
            i += 1
            continue
        if opts[i] == 2 and opts[i + 1] == 4:
            return struct.unpack("!H", opts[i + 2:i + 4])[0]
        i += max(2, opts[i + 1])
    return None


sock.settimeout(0.5)
OUT = bytes((i * 7) & 0xff for i in range(6500))
got = {}

syn = guest_tcp(timeout=30)
check("tcp: the guest's SYN", syn is not None and syn.flags == S)
if syn:
    check("tcp: the SYN's MSS", mss_of(syn.opts) == 1460, str(mss_of(syn.opts)))
    G, gport = syn.seq, syn.sport

    # not answered: it comes again
    syn2 = guest_tcp(timeout=4)
    check("tcp: SYN retransmitted", syn2 is not None and syn2.flags == S and syn2.seq == G and
          syn2.t - syn.t >= 0.8)

    Y = 1000000
    tcp_send(gport, Y, G + 1, S | A, opts=b"\x02\x04\x03\xe8")    # MSS 1000
    a = guest_tcp()
    check("tcp: the handshake's ACK", a is not None and a.ack == Y + 1 and a.seq == G + 1)

    # the second half first
    tcp_send(gport, Y + 101, G + 1, A | P, data=bytes(range(100, 200)))
    a = guest_tcp()
    check("tcp: out of order: a duplicate ACK", a is not None and a.ack == Y + 1)
    tcp_send(gport, Y + 1, G + 1, A | P, data=bytes(range(100)))
    a = guest_tcp()
    check("tcp: out of order: both acked", a is not None and a.ack == Y + 201)

    def take(s):
        if s:
            got[s.seq - G - 1] = s.data
        return s

    # its initial window: 4 segments of our MSS
    segs = [take(guest_tcp(data=True)) for _ in range(4)]
    check("tcp: the initial window, 4 segments of 1000",
          all(s and len(s.data) == 1000 for s in segs) and
          [s.seq - G - 1 for s in segs if s] == [0, 1000, 2000, 3000])

    # the first lost: three duplicate ACKs
    for _ in range(3):
        tcp_send(gport, Y + 201, G + 1, A)
    r = take(guest_tcp(data=True))
    check("tcp: fast retransmit", r is not None and r.seq == G + 1 and len(r.data) == 1000)

    # all acked: after recovery, ssthresh (2000)
    tcp_send(gport, Y + 201, G + 4001, A)
    n1, n2 = take(guest_tcp(data=True)), take(guest_tcp(data=True))
    check("tcp: after recovery, two segments", n1 is not None and n2 is not None and
          n1.seq == G + 4001 and n2.seq == G + 5001)

    # the second of them lost: it comes again when the timer goes off
    sent_at = time.time()
    tcp_send(gport, Y + 201, G + 5001, A)
    r = take(guest_tcp(data=True, timeout=5))
    check("tcp: retransmitted on timeout", r is not None and r.seq == G + 5001 and
          r.t - sent_at >= 0.8, str(r and (r.seq - G - 1, r.t - sent_at)))

    # acked, and the window closed: probed
    tcp_send(gport, Y + 201, G + 6001, A, win=0)
    p = guest_tcp(data=True, timeout=8)
    check("tcp: zero window probe", p is not None and p.seq == G + 6001 and len(p.data) == 1)

    # open again: the rest
    tcp_send(gport, Y + 201, G + 6001, A, win=65535)
    deadline = time.time() + 5
    while time.time() < deadline and sum(len(d) for o, d in got.items() if o >= 6000) < 500:
        take(guest_tcp(data=True, timeout=1))
    data = bytearray(6500)
    for off, d in got.items():
        data[off:off + len(d)] = d
    check("tcp: the window opened: all 6500 bytes, right", bytes(data) == OUT)
    tcp_send(gport, Y + 201, G + 6501, A)

    # reset (the guest checks ECONNRESET)
    tcp_send(gport, Y + 201, 0, R)

# a SYN to a port nobody listens on: RST
tcp_send(6999, 5000, 0, S, sport=6001)
r = guest_tcp(port=6001)
check("tcp: SYN to a closed port: RST", r is not None and r.flags == R | A and r.ack == 5001)

print("net: done, fails=%d" % fails)
sys.exit(1 if fails else 0)
