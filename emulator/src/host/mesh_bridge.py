#!/usr/bin/env python3
"""Host-only mesh bridge for the Phase-1 emulator.

Connects to a Meshtastic node over USB serial (the Muzi R1 Neo) using the
canonical `meshtastic` library and speaks a tiny tab-separated protocol to the
C++ BridgeMesh backend over stdio:

  bridge -> emu (stdout, one record per line):
    READY <our_id> <short> <long>
    NODE  <id> <short> <long> <snr> <lastHeard>
    RX    <from_id> <from_name> <dest> <channel> <ts> <text...>
    LOG   <free text>            (diagnostics; ignored by the UI)
  emu -> bridge (stdin):
    SEND  <dest> <channel> <text...>     (dest 0 or 4294967295 = broadcast)
    QUIT

Library chatter is forced to stderr so stdout carries only protocol lines.
This bridge exists purely so the emulator can use a real RF node without
reimplementing the Meshtastic stream protocol in C++.
"""
import argparse
import sys

from pubsub import pub
import meshtastic
import meshtastic.serial_interface

BROADCAST = 0xFFFFFFFF

_iface = None


def out(line: str) -> None:
    sys.stdout.write(line + "\n")
    sys.stdout.flush()


def san(s) -> str:
    if s is None:
        return ""
    return str(s).replace("\t", " ").replace("\r", " ").replace("\n", " ")


def emit_node(n: dict) -> None:
    try:
        num = n.get("num")
        u = n.get("user", {}) or {}
        out("NODE\t%s\t%s\t%s\t%s\t%s" % (
            num, san(u.get("shortName")), san(u.get("longName")),
            n.get("snr", 0) or 0, n.get("lastHeard", 0) or 0))
    except Exception as e:  # noqa: BLE001
        out("LOG\tnode err %s" % san(e))


def on_receive(packet=None, interface=None):
    try:
        d = (packet or {}).get("decoded", {}) or {}
        if d.get("portnum") != "TEXT_MESSAGE_APP":
            return
        frm = packet.get("from", 0)
        to = packet.get("to", 0)
        ch = packet.get("channel", 0) or 0
        name = str(frm)
        nodes_by_num = getattr(interface, "nodesByNum", None) or {}
        node = nodes_by_num.get(frm)
        if node:
            u = node.get("user", {}) or {}
            name = u.get("shortName") or u.get("longName") or str(frm)
        out("RX\t%s\t%s\t%s\t%s\t0\t%s" % (frm, san(name), to, ch, san(d.get("text", ""))))
    except Exception as e:  # noqa: BLE001
        out("LOG\trx err %s" % san(e))


def read_commands() -> None:
    for line in sys.stdin:
        line = line.rstrip("\n")
        if not line:
            continue
        parts = line.split("\t")
        cmd = parts[0]
        if cmd == "SEND" and len(parts) >= 4:
            dest = int(parts[1]); ch = int(parts[2]); text = "\t".join(parts[3:])
            try:
                if dest in (0, BROADCAST):
                    _iface.sendText(text, channelIndex=ch)
                else:
                    _iface.sendText(text, destinationId=dest, channelIndex=ch)
                out("LOG\tsent ch%d" % ch)
            except Exception as e:  # noqa: BLE001
                out("LOG\tsend err %s" % san(e))
        elif cmd == "QUIT":
            break


def main() -> None:
    global _iface
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM0")
    args = ap.parse_args()

    pub.subscribe(on_receive, "meshtastic.receive")

    # Keep library prints off our protocol stdout during connect.
    real_stdout = sys.stdout
    sys.stdout = sys.stderr
    try:
        _iface = meshtastic.serial_interface.SerialInterface(devPath=args.port)
    finally:
        sys.stdout = real_stdout

    try:
        my = _iface.getMyNodeInfo() or {}
        u = my.get("user", {}) or {}
        out("READY\t%s\t%s\t%s" % (_iface.myInfo.my_node_num, san(u.get("shortName")), san(u.get("longName"))))
    except Exception as e:  # noqa: BLE001
        out("READY\t0\t?\t%s" % san(e))

    try:
        for _, n in (getattr(_iface, "nodesByNum", None) or {}).items():
            emit_node(n)
    except Exception as e:  # noqa: BLE001
        out("LOG\tnodes err %s" % san(e))

    out("LOG\tbridge up")
    read_commands()
    try:
        _iface.close()
    except Exception:  # noqa: BLE001
        pass


if __name__ == "__main__":
    main()
