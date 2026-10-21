#!/usr/bin/env python3
#
# The other end of the guest's e1000: QEMU sends its frames here as UDP
# datagrams (-netdev socket,udp=), and takes the ones sent back.
#
# usage: peer.py <our port> <QEMU's port>
#
# Plays 10.0.2.2 on the guest's 10.0.2.0/24 (the guest is 10.0.2.15) and
# prints a line per check, as aqtest does: PASS/FAIL <name>, then
# "net: done".

import socket
import struct
import sys

GUEST_IP = bytes([10, 0, 2, 15])
PEER_IP  = bytes([10, 0, 2, 2])
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


def receive(want, tries=20):
    """the next frame `want' takes, None if none comes"""
    for _ in range(tries):
        try:
            frame, _ = sock.recvfrom(4096)
        except socket.timeout:
            return None
        if want(frame):
            return frame
    return None


def ether(dst, type_, payload):
    return dst + PEER_MAC + struct.pack("!H", type_) + payload


def arp_request(target):
    return ether(BROADCAST, 0x0806, struct.pack("!HHBBH6s4s6s4s", 1, 0x0800, 6, 4, 1,
                                                PEER_MAC, PEER_IP, b"\0" * 6, target))


def ipv4(dst_mac, dst_ip, proto, payload, ident=1):
    h = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(payload), ident, 0x4000, 64, proto, 0,
                    PEER_IP, dst_ip)
    h = h[:10] + struct.pack("!H", checksum(h)) + h[12:]
    return ether(dst_mac, 0x0800, h + payload)


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

print("net: done, fails=%d" % fails)
sys.exit(1 if fails else 0)
