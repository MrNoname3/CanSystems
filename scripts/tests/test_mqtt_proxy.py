"""Unit tests for scripts/mqtt_proxy.py: framing the stream, and choosing what to lose.

Framing is where a proxy like this goes wrong silently. TCP hands over whatever arrived, not whole
packets, so a reader that assumes otherwise mislabels everything after the first short read - and
the trace still looks plausible, which is the worst kind of wrong for a tool whose whole job is to
be believed. The tests feed the same bytes in every awkward split and expect the same packets out.

The rest is the fault-injection rules: a spec that silently means something other than it says
would quietly turn a test of the firmware into a test of nothing.
"""

import argparse
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # scripts/ for `import mqtt_proxy`
import mqtt_proxy

CONNECT = bytes.fromhex("101800044d5154540402000f000c636c69656e745f7465737431")
PUBLISH_QOS0 = bytes.fromhex("300e0005746f7069637061796c6f6164")
PINGREQ = bytes.fromhex("c000")
SUBACK = bytes.fromhex("9003000101")


def drain(chunks: list[bytes]) -> list[bytes]:
    """Every packet that comes out of feeding `chunks` in, one at a time."""
    buffer = bytearray()
    packets: list[bytes] = []
    for chunk in chunks:
        buffer.extend(chunk)
        while True:
            packet = mqtt_proxy.take_packet(buffer)
            if packet is None:
                break
            packets.append(packet)
    return packets


def test_a_whole_packet_comes_back_unchanged() -> None:
    assert drain([CONNECT]) == [CONNECT]


def test_packets_arriving_together_are_framed_apart() -> None:
    assert drain([CONNECT + PINGREQ + PUBLISH_QOS0]) == [CONNECT, PINGREQ, PUBLISH_QOS0]


def test_a_packet_split_at_every_point_frames_the_same() -> None:
    stream = CONNECT + PINGREQ + PUBLISH_QOS0
    for at in range(1, len(stream)):
        assert drain([stream[:at], stream[at:]]) == [CONNECT, PINGREQ, PUBLISH_QOS0], f"split at {at}"


def test_one_byte_at_a_time_frames_the_same() -> None:
    stream = CONNECT + PINGREQ + PUBLISH_QOS0
    assert drain([stream[at:at + 1] for at in range(len(stream))]) == [CONNECT, PINGREQ, PUBLISH_QOS0]


def test_an_incomplete_packet_is_left_in_the_buffer() -> None:
    buffer = bytearray(CONNECT[:-1])
    assert mqtt_proxy.take_packet(buffer) is None
    assert len(buffer) == len(CONNECT) - 1
    buffer.extend(CONNECT[-1:])
    assert mqtt_proxy.take_packet(buffer) == CONNECT
    assert not buffer


def test_a_multi_byte_remaining_length_is_read_whole() -> None:
    # 200 bytes of payload needs two length bytes, which is the case a single-byte reader gets wrong.
    body = b"\x00\x05topic" + (b"x" * 193)
    packet = b"\x30\xc8\x01" + body
    assert drain([packet]) == [packet]
    assert mqtt_proxy.describe(packet) == "topic=topic qos=0 retain=0 payload=193B"


def test_a_length_field_longer_than_the_standard_allows_is_refused() -> None:
    with pytest.raises(ValueError, match="remaining length"):
        mqtt_proxy.take_packet(bytearray(b"\x30\xff\xff\xff\xff\xff"))


def test_packet_names_cover_both_directions() -> None:
    assert mqtt_proxy.packet_name(CONNECT) == "CONNECT"
    assert mqtt_proxy.packet_name(PINGREQ) == "PINGREQ"
    assert mqtt_proxy.packet_name(SUBACK) == "SUBACK"


def test_connect_is_described_by_the_fields_a_test_turns_on() -> None:
    assert mqtt_proxy.describe(CONNECT) == "id=client_test1 keepalive=15s clean=True will=False user=False"


def test_a_packet_that_carries_nothing_is_described_as_nothing() -> None:
    assert mqtt_proxy.describe(PINGREQ) == ""


def test_a_packet_too_short_for_its_type_is_said_to_be_so() -> None:
    assert mqtt_proxy.describe(b"\x90\x00") == "<short for its type>"


def test_an_empty_packet_is_its_type_and_a_zero_length() -> None:
    assert mqtt_proxy.empty_packet("PINGREQ") == PINGREQ
    assert mqtt_proxy.empty_packet("DISCONNECT") == bytes.fromhex("e000")


def test_a_drop_spec_without_occurrences_loses_every_one() -> None:
    rule = mqtt_proxy.parse_drop("client:PINGREQ")
    assert rule == mqtt_proxy.DropRule(sender="client", packet="PINGREQ", occurrences=None)


def test_a_drop_spec_names_the_occurrences_it_loses() -> None:
    rule = mqtt_proxy.parse_drop("broker:pingresp:1,3")
    assert rule.packet == "PINGRESP"
    assert rule.occurrences == frozenset({1, 3})


@pytest.mark.parametrize("spec", ["client", "nobody:PINGREQ", "client:NOTAPACKET", "client:PINGREQ:x",
                                  "client:PINGREQ:0", "client:PINGREQ:1:2"])
def test_a_drop_spec_that_says_nothing_usable_is_refused(spec: str) -> None:
    with pytest.raises(argparse.ArgumentTypeError):
        mqtt_proxy.parse_drop(spec)


def test_an_inject_spec_carries_its_delay() -> None:
    assert mqtt_proxy.parse_inject("broker:PINGREQ:12.5") == mqtt_proxy.InjectRule(
        sender="broker", packet="PINGREQ", delay_s=12.5)


def test_only_the_packets_that_carry_nothing_may_be_injected() -> None:
    with pytest.raises(argparse.ArgumentTypeError, match="carry nothing"):
        mqtt_proxy.parse_inject("broker:PUBLISH:1")


def test_a_target_is_split_at_its_last_colon() -> None:
    assert mqtt_proxy.parse_target("192.168.1.10:1883") == ("192.168.1.10", 1883)


@pytest.mark.parametrize("spec", ["1883", ":1883", "host:port"])
def test_a_target_that_is_not_host_and_port_is_refused(spec: str) -> None:
    with pytest.raises(argparse.ArgumentTypeError):
        mqtt_proxy.parse_target(spec)


def test_every_occurrence_is_dropped_when_none_are_named() -> None:
    rules = [mqtt_proxy.parse_drop("client:PINGREQ")]
    assert all(mqtt_proxy.should_drop(rules, "client", "PINGREQ", each) for each in (1, 2, 99))


def test_only_the_named_occurrences_are_dropped() -> None:
    rules = [mqtt_proxy.parse_drop("client:PINGREQ:2")]
    assert not mqtt_proxy.should_drop(rules, "client", "PINGREQ", 1)
    assert mqtt_proxy.should_drop(rules, "client", "PINGREQ", 2)
    assert not mqtt_proxy.should_drop(rules, "client", "PINGREQ", 3)


def test_a_rule_binds_to_its_sender_and_its_packet_type() -> None:
    rules = [mqtt_proxy.parse_drop("client:PINGREQ")]
    assert not mqtt_proxy.should_drop(rules, "broker", "PINGREQ", 1)
    assert not mqtt_proxy.should_drop(rules, "client", "PUBLISH", 1)


def test_rules_are_taken_together() -> None:
    rules = [mqtt_proxy.parse_drop("client:PINGREQ:1"), mqtt_proxy.parse_drop("broker:PINGRESP")]
    assert mqtt_proxy.should_drop(rules, "client", "PINGREQ", 1)
    assert mqtt_proxy.should_drop(rules, "broker", "PINGRESP", 4)


def test_nothing_is_dropped_without_a_rule() -> None:
    assert not mqtt_proxy.should_drop([], "client", "PINGREQ", 1)


def test_occurrences_are_counted_per_sender_and_type() -> None:
    counters = mqtt_proxy.Counters()
    assert counters.count("client PINGREQ") == 1
    assert counters.count("broker PINGRESP") == 1
    assert counters.count("client PINGREQ") == 2


def test_the_command_line_becomes_the_configuration_it_describes() -> None:
    config = mqtt_proxy.parse_args(["--listen", "1885", "--target", "10.0.0.2:8883",
                                    "--drop", "client:PINGREQ:3", "--inject", "broker:PINGREQ:12",
                                    "--after", "30", "--cut", "45"])
    assert (config.listen_port, config.target_host, config.target_port) == (1885, "10.0.0.2", 8883)
    assert config.drops == (mqtt_proxy.DropRule(sender="client", packet="PINGREQ", occurrences=frozenset({3})),)
    assert config.injects == (mqtt_proxy.InjectRule(sender="broker", packet="PINGREQ", delay_s=12.0),)
    assert (config.drop_after_s, config.cut_after_s) == (30.0, 45.0)


def test_the_defaults_forward_everything_untouched() -> None:
    config = mqtt_proxy.parse_args([])
    assert (config.drops, config.injects, config.cut_after_s) == ((), (), None)
