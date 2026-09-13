#!/usr/bin/env python3
"""Sit between an MQTT client and its broker, print every packet, and lose the ones asked for.

A broker's log says what it received; it cannot say what a device sent and never arrived, and that
is the difference between a client that stopped talking and a link that ate the packet. This frames
both directions of the TCP stream into MQTT 3.1.1 packets, prints one line each with the gap since
the previous packet of that kind, and drops, injects or cuts on command - so a loss that happens
once a week on a roof can be produced on a bench in a second.

Losing a packet here is not the same as closing the connection: neither end is told anything, which
is what makes it a test of the timeouts rather than of the reconnect path.

It speaks no TLS. Point the client at a plaintext listener, or the records are opaque and the only
thing that can be dropped is "some bytes".

Injection is limited to the packets that carry nothing: PINGREQ, PINGRESP and DISCONNECT. They are
enough to test what a peer does with a packet it should never receive.

Usage:
  python scripts/mqtt_proxy.py                                  # trace, forward everything
  python scripts/mqtt_proxy.py --target 192.168.1.10:1883
  python scripts/mqtt_proxy.py --drop client:PINGREQ:3          # lose the third ping
  python scripts/mqtt_proxy.py --drop client:PINGREQ --after 30 # lose every ping from 30 s on
  python scripts/mqtt_proxy.py --drop broker:PINGRESP:1,2       # lose the first two answers
  python scripts/mqtt_proxy.py --inject broker:PINGREQ:12       # 12 s in, ping the client
  python scripts/mqtt_proxy.py --cut 45                         # drop the connection at 45 s
"""

import argparse
import socket
import sys
import threading
import time
from collections.abc import Sequence
from dataclasses import dataclass, field
from typing import TextIO

PACKET_NAMES: dict[int, str] = {
    1: "CONNECT", 2: "CONNACK", 3: "PUBLISH", 4: "PUBACK", 5: "PUBREC", 6: "PUBREL",
    7: "PUBCOMP", 8: "SUBSCRIBE", 9: "SUBACK", 10: "UNSUBSCRIBE", 11: "UNSUBACK",
    12: "PINGREQ", 13: "PINGRESP", 14: "DISCONNECT",
}
PACKET_CODES: dict[str, int] = {name: code for code, name in PACKET_NAMES.items()}
# The packets whose whole content is their type: a fixed header and a remaining length of zero.
EMPTY_PACKETS = frozenset({"PINGREQ", "PINGRESP", "DISCONNECT"})
SENDERS = ("client", "broker")
MAX_REMAINING_LENGTH_BYTES = 4   # what the standard allows the length field to take
READ_CHUNK = 4096


@dataclass(frozen=True)
class DropRule:
    """Which packets to lose: every one of a type from a sender, or only chosen occurrences."""

    sender: str
    packet: str
    occurrences: frozenset[int] | None   # None loses every one of them


@dataclass(frozen=True)
class InjectRule:
    """A packet to put on the wire as if the named sender had sent it, once, after a delay."""

    sender: str
    packet: str
    delay_s: float


@dataclass(frozen=True)
class Config:
    listen_port: int
    target_host: str
    target_port: int
    drops: tuple[DropRule, ...]
    injects: tuple[InjectRule, ...]
    drop_after_s: float
    cut_after_s: float | None


@dataclass
class Counters:
    """How many of each packet type a sender has produced on one connection."""

    seen: dict[str, int] = field(default_factory=dict[str, int])

    def count(self, packet: str) -> int:
        self.seen[packet] = self.seen.get(packet, 0) + 1
        return self.seen[packet]


class Tracer:
    """Prints one line per packet, timestamped from when the proxy started."""

    def __init__(self, out: TextIO) -> None:
        self._out = out
        self._started = time.monotonic()
        self._lock = threading.Lock()
        self._last_seen: dict[str, float] = {}

    def elapsed(self) -> float:
        return time.monotonic() - self._started

    def note(self, text: str) -> None:
        with self._lock:
            print(f"[{self.elapsed():8.3f}] {text}", file=self._out, flush=True)

    def packet(self, arrow: str, data: bytes, *, dropped: bool = False, injected: bool = False) -> None:
        name = packet_name(data)
        key = f"{arrow} {name}"
        with self._lock:
            now = self.elapsed()
            previous = self._last_seen.get(key)
            self._last_seen[key] = now
            gap = "" if previous is None else f"  (+{now - previous:6.2f}s since previous {name})"
            mark = "   <<< DROPPED" if dropped else ("   <<< INJECTED" if injected else "")
            print(f"[{now:8.3f}] {arrow} {name:<11} {len(data):4d}B {describe(data)}{gap}{mark}",
                  file=self._out, flush=True)


def packet_name(data: bytes) -> str:
    return PACKET_NAMES.get(data[0] >> 4, f"?{data[0] >> 4}")


def body_of(data: bytes) -> bytes:
    """Everything past the fixed header, which is the type byte plus the remaining-length field."""
    index = 1
    while index < len(data) and (data[index] & 0x80):
        index += 1
    return data[index + 1:]


def describe(data: bytes) -> str:
    """The fields worth seeing for this packet type, or an empty string when it has none."""
    name = packet_name(data)
    flags = data[0] & 0x0F
    body = body_of(data)
    try:
        if name == "CONNECT":
            protocol_len = (body[0] << 8) | body[1]
            at = 2 + protocol_len + 1
            connect_flags = body[at]
            keepalive = (body[at + 1] << 8) | body[at + 2]
            id_len = (body[at + 3] << 8) | body[at + 4]
            client_id = body[at + 5:at + 5 + id_len].decode("utf-8", "replace")
            return (f"id={client_id} keepalive={keepalive}s clean={bool(connect_flags & 0x02)} "
                    f"will={bool(connect_flags & 0x04)} user={bool(connect_flags & 0x80)}")
        if name == "CONNACK":
            return f"sessionPresent={body[0] & 1} rc={body[1]}"
        if name == "PUBLISH":
            qos = (flags >> 1) & 0x03
            topic_len = (body[0] << 8) | body[1]
            topic = body[2:2 + topic_len].decode("utf-8", "replace")
            payload_len = len(body) - 2 - topic_len - (2 if qos else 0)
            return f"topic={topic} qos={qos} retain={flags & 1} payload={payload_len}B"
        if name in ("PUBACK", "PUBREC", "PUBREL", "PUBCOMP", "UNSUBACK"):
            return f"id={(body[0] << 8) | body[1]}"
        if name == "SUBSCRIBE":
            topic_len = (body[2] << 8) | body[3]
            filter_ = body[4:4 + topic_len].decode("utf-8", "replace")
            return f"id={(body[0] << 8) | body[1]} filter={filter_} qos={body[4 + topic_len]}"
        if name == "SUBACK":
            return f"id={(body[0] << 8) | body[1]} granted={list(body[2:])}"
    except IndexError:
        return "<short for its type>"
    return ""


def take_packet(buffer: bytearray) -> bytes | None:
    """Removes one whole MQTT packet from the front of `buffer`, or leaves it alone if none is in.

    The remaining-length field is what says where a packet ends, and it arrives in the same stream
    as the rest: until enough of it is here the length itself is unknown, which is why this reports
    "not yet" rather than reading ahead.
    """
    if len(buffer) < 2:
        return None
    index, multiplier, remaining = 1, 1, 0
    while True:
        if index >= len(buffer):
            return None
        digit = buffer[index]
        remaining += (digit & 0x7F) * multiplier
        multiplier *= 128
        index += 1
        if not (digit & 0x80):
            break
        if index > MAX_REMAINING_LENGTH_BYTES:
            raise ValueError("remaining length runs past the four bytes the standard allows")
    total = index + remaining
    if len(buffer) < total:
        return None
    packet = bytes(buffer[:total])
    del buffer[:total]
    return packet


def empty_packet(name: str) -> bytes:
    """The two bytes of a packet that carries nothing: its type, and a remaining length of zero."""
    return bytes((PACKET_CODES[name] << 4, 0))


def parse_drop(spec: str) -> DropRule:
    """`sender:PACKET[:all|n[,n...]]`, e.g. client:PINGREQ:3 - the client's third ping."""
    parts = spec.split(":")
    if len(parts) not in (2, 3):
        raise argparse.ArgumentTypeError(f"{spec!r}: expected sender:PACKET or sender:PACKET:which")
    sender, packet = parts[0], parts[1].upper()
    if sender not in SENDERS:
        raise argparse.ArgumentTypeError(f"{spec!r}: sender is one of {', '.join(SENDERS)}")
    if packet not in PACKET_CODES:
        raise argparse.ArgumentTypeError(f"{spec!r}: {packet} is not an MQTT packet type")
    if len(parts) == 2 or parts[2] == "all":
        return DropRule(sender=sender, packet=packet, occurrences=None)
    try:
        wanted = frozenset(int(each) for each in parts[2].split(","))
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"{spec!r}: which is 'all' or comma-separated numbers") from exc
    if any(each < 1 for each in wanted):
        raise argparse.ArgumentTypeError(f"{spec!r}: occurrences are counted from one")
    return DropRule(sender=sender, packet=packet, occurrences=wanted)


def parse_inject(spec: str) -> InjectRule:
    """`sender:PACKET:seconds`, e.g. broker:PINGREQ:12 - ping the client twelve seconds in."""
    parts = spec.split(":")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError(f"{spec!r}: expected sender:PACKET:seconds")
    sender, packet = parts[0], parts[1].upper()
    if sender not in SENDERS:
        raise argparse.ArgumentTypeError(f"{spec!r}: sender is one of {', '.join(SENDERS)}")
    if packet not in EMPTY_PACKETS:
        raise argparse.ArgumentTypeError(f"{spec!r}: only {', '.join(sorted(EMPTY_PACKETS))} carry nothing")
    try:
        delay = float(parts[2])
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"{spec!r}: seconds is a number") from exc
    return InjectRule(sender=sender, packet=packet, delay_s=delay)


def parse_target(spec: str) -> tuple[str, int]:
    host, separator, port = spec.rpartition(":")
    if not separator or not host:
        raise argparse.ArgumentTypeError(f"{spec!r}: expected host:port")
    try:
        return host, int(port)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"{spec!r}: port is a number") from exc


def should_drop(rules: Sequence[DropRule], sender: str, packet: str, occurrence: int) -> bool:
    """Whether this packet is one a rule asked to lose, `occurrence` counting from one."""
    return any(rule.sender == sender and rule.packet == packet
               and (rule.occurrences is None or occurrence in rule.occurrences)
               for rule in rules)


class Proxy:
    """One listening socket, and a pair of pumps for every connection that arrives on it."""

    def __init__(self, config: Config, tracer: Tracer) -> None:
        self._config = config
        self._tracer = tracer

    def serve(self) -> None:
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("0.0.0.0", self._config.listen_port))   # a bench tool: the device has to reach it
        listener.listen(4)
        self._tracer.note(f"listening on :{self._config.listen_port} -> "
                          f"{self._config.target_host}:{self._config.target_port}")
        connection_no = 0
        while True:
            client, address = listener.accept()
            connection_no += 1
            self._tracer.note(f"---- connection {connection_no} from {address[0]}:{address[1]} ----")
            self._start(client)

    def _start(self, client: socket.socket) -> None:
        try:
            broker = socket.create_connection((self._config.target_host, self._config.target_port), 5)
        except OSError as exc:
            self._tracer.note(f"the broker would not take the connection: {exc}")
            client.close()
            return
        # The connect timeout would otherwise stay on and cut an idle session short.
        client.settimeout(None)
        broker.settimeout(None)
        for each in (client, broker):
            each.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        counters = Counters()
        for source, sink, sender, arrow in ((client, broker, "client", "c->b"),
                                            (broker, client, "broker", "b->c")):
            threading.Thread(target=self._pump, args=(source, sink, sender, arrow, counters),
                             daemon=True).start()
        for rule in self._config.injects:
            target = broker if rule.sender == "client" else client
            arrow = "c->b" if rule.sender == "client" else "b->c"
            threading.Thread(target=self._inject, args=(target, arrow, rule), daemon=True).start()
        if self._config.cut_after_s is not None:
            threading.Thread(target=self._cut, args=(client, broker, self._config.cut_after_s),
                             daemon=True).start()

    def _pump(self, source: socket.socket, sink: socket.socket, sender: str, arrow: str,
              counters: Counters) -> None:
        buffer = bytearray()
        try:
            while True:
                packet = take_packet(buffer)
                if packet is None:
                    chunk = source.recv(READ_CHUNK)
                    if not chunk:
                        break
                    buffer.extend(chunk)
                    continue
                name = packet_name(packet)
                occurrence = counters.count(f"{sender} {name}")
                dropped = (self._tracer.elapsed() >= self._config.drop_after_s
                           and should_drop(self._config.drops, sender, name, occurrence))
                self._tracer.packet(arrow, packet, dropped=dropped)
                if not dropped:
                    sink.sendall(packet)
        except (OSError, ValueError) as exc:
            self._tracer.note(f"{arrow} stream ended: {exc}")
        finally:
            close_both(source, sink)

    def _inject(self, target: socket.socket, arrow: str, rule: InjectRule) -> None:
        time.sleep(rule.delay_s)
        packet = empty_packet(rule.packet)
        try:
            target.sendall(packet)
        except OSError as exc:
            self._tracer.note(f"{rule.packet} could not be injected: {exc}")
            return
        self._tracer.packet(arrow, packet, injected=True)

    def _cut(self, client: socket.socket, broker: socket.socket, after_s: float) -> None:
        time.sleep(after_s)
        self._tracer.note("cutting the connection")
        close_both(client, broker)


def close_both(first: socket.socket, second: socket.socket) -> None:
    for each in (first, second):
        try:
            each.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass


class _Args(argparse.Namespace):
    """What the parser fills in; annotated so the checker knows what comes back out of it."""

    listen: int
    target: tuple[str, int]
    drop: list[DropRule]
    inject: list[InjectRule]
    after: float
    cut: float | None


def parse_args(argv: Sequence[str] | None = None) -> Config:
    parser = argparse.ArgumentParser(
        description="Trace an MQTT connection packet by packet, and lose the packets asked for.",
        epilog="Plaintext only: TLS records cannot be framed, let alone chosen between.")
    parser.add_argument("--listen", type=int, default=1884, metavar="PORT",
                        help="port the client connects to (default: 1884)")
    parser.add_argument("--target", type=parse_target, default=("127.0.0.1", 1883), metavar="HOST:PORT",
                        help="the broker to forward to (default: 127.0.0.1:1883)")
    parser.add_argument("--drop", type=parse_drop, action="append", default=[], metavar="SPEC",
                        help="sender:PACKET[:all|n,n] - repeatable, e.g. client:PINGREQ:3")
    parser.add_argument("--inject", type=parse_inject, action="append", default=[], metavar="SPEC",
                        help="sender:PACKET:seconds - repeatable, e.g. broker:PINGREQ:12")
    parser.add_argument("--after", type=float, default=0.0, metavar="SECONDS",
                        help="let everything through for this long before dropping starts")
    parser.add_argument("--cut", type=float, default=None, metavar="SECONDS",
                        help="close the connection this long after it opens")
    parsed = parser.parse_args(argv, namespace=_Args())
    return Config(
        listen_port=parsed.listen,
        target_host=parsed.target[0],
        target_port=parsed.target[1],
        drops=tuple(parsed.drop),
        injects=tuple(parsed.inject),
        drop_after_s=parsed.after,
        cut_after_s=parsed.cut,
    )


def main(argv: Sequence[str] | None = None) -> int:
    config = parse_args(argv)
    tracer = Tracer(sys.stdout)
    try:
        Proxy(config, tracer).serve()
    except KeyboardInterrupt:
        tracer.note("stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
