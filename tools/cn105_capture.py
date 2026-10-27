#!/usr/bin/env python3
"""Convert raw CN105 captures into annotated frame files.

Inputs (any combination):
  * ESPHome logs from examples/sniffer/cn105-sniffer.yaml
    (lines tagged ``cn105sniff``: ``HP <millis> <hex>`` / ``MOD <millis> <hex>`` / ``MARK <millis>``)
  * sigrok-cli UART annotations, one file per direction, decoded with
    ``--protocol-decoder-samplenum`` (see docs/sniffing.md)

Output: one frame per line, time-ordered, both directions merged:

  <t_ms> <DIR> <hex bytes>  # <TYPE>[:<code name>] [BAD_CHECKSUM]

``DIR`` is ``HP`` (sent by the indoor unit) or ``MOD`` (sent by the official
module / controller, or by this component when sniffing it). ``--format jsonl``
emits one JSON object per frame instead, and ``--stats`` prints a summary
(frame counts, checksum errors, poll period and request→response latency).

Standard library only.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from collections import defaultdict
from dataclasses import dataclass, field

# ── Protocol ────────────────────────────────────────────────────────────────

HEADER_LEN = 5  # FC <type> 01 30 <data_len>
MAX_DATA_LEN = 32

PACKET_TYPES = {
    0x5A: "CONNECT",
    0x5B: "CONNECT_INSTALLER",
    0x7A: "CONNECT_ACK",
    0x7B: "CONNECT_INSTALLER_ACK",
    0x41: "SET",
    0x61: "SET_ACK",
    0x42: "GET",
    0x62: "GET_RESP",
}

# data[0] (frame byte 5) of GET / GET_RESP frames
INFO_CODES = {
    0x02: "settings",
    0x03: "room_temp",
    0x04: "error_info",
    0x05: "timers",
    0x06: "status",
    0x09: "standby",
    0x10: "unknown_0x10",
    0x20: "functions_1",
    0x22: "functions_2",
    0x42: "hvac_options",
}

# data[0] of SET frames
SET_CODES = {
    0x01: "settings",
    0x07: "remote_temp",
    0x08: "run_state",
    0x1F: "functions_1",
    0x21: "functions_2",
}


def checksum(data: bytes) -> int:
    """Same algorithm as cn105_protocol::checksum: over every byte but the last."""
    return (0xFC - sum(data)) & 0xFF


@dataclass
class Frame:
    t_ms: float
    direction: str
    raw: bytes
    checksum_ok: bool = True
    note: str = ""

    @property
    def ptype(self) -> int:
        return self.raw[1] if len(self.raw) > 1 else -1

    @property
    def code(self) -> int | None:
        return self.raw[5] if len(self.raw) > HEADER_LEN and self.ptype in (0x41, 0x42, 0x62) else None

    def label(self) -> str:
        if self.note:
            return self.note
        name = PACKET_TYPES.get(self.ptype, f"TYPE_0x{self.ptype:02X}")
        code = self.code
        if code is not None:
            table = SET_CODES if self.ptype == 0x41 else INFO_CODES
            name += ":" + table.get(code, f"0x{code:02X}")
        if not self.checksum_ok:
            name += " BAD_CHECKSUM"
        return name


# ── Framing ─────────────────────────────────────────────────────────────────


@dataclass
class Reassembler:
    """Rebuilds frames from one direction's byte stream (chunks may split or merge frames)."""

    direction: str
    buf: bytearray = field(default_factory=bytearray)
    times: list = field(default_factory=list)  # timestamp of each byte in buf

    def feed(self, t_ms: float, data: bytes) -> list[Frame]:
        self.buf += data
        self.times += [t_ms] * len(data)
        return self._extract(final=False)

    def flush(self) -> list[Frame]:
        return self._extract(final=True)

    def _drop(self, n: int) -> None:
        del self.buf[:n]
        del self.times[:n]

    def _extract(self, final: bool) -> list[Frame]:
        out: list[Frame] = []
        while self.buf:
            if self.buf[0] != 0xFC:
                # Garbage until the next start byte.
                n = self.buf.find(0xFC)
                n = len(self.buf) if n < 0 else n
                out.append(Frame(self.times[0], self.direction, bytes(self.buf[:n]), False, "GARBAGE"))
                self._drop(n)
                continue
            if len(self.buf) < HEADER_LEN:
                break
            data_len = self.buf[4]
            if data_len > MAX_DATA_LEN:
                out.append(Frame(self.times[0], self.direction, bytes(self.buf[:1]), False, "GARBAGE"))
                self._drop(1)
                continue
            total = HEADER_LEN + data_len + 1
            if len(self.buf) < total:
                break
            raw = bytes(self.buf[:total])
            out.append(Frame(self.times[0], self.direction, raw, checksum(raw[:-1]) == raw[-1]))
            self._drop(total)
        if final and self.buf:
            out.append(Frame(self.times[0], self.direction, bytes(self.buf), False, "TRUNCATED"))
            self._drop(len(self.buf))
        return out


# ── Input parsers ───────────────────────────────────────────────────────────

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
SNIFF_RE = re.compile(r"cn105sniff[^\]]*\]:\s*(HP|MOD|MARK)\s+(\d+)\s*([0-9A-Fa-f ]*)")
HEX_TOKEN_RE = re.compile(r"\b[0-9A-Fa-f]{2}\b")


def parse_esphome_log(path: str):
    """Yield (t_ms, direction, bytes) chunks and (t_ms, 'MARK', text) markers."""
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = ANSI_RE.sub("", line)
            m = SNIFF_RE.search(line)
            if not m:
                continue
            kind, t, rest = m.group(1), int(m.group(2)), m.group(3)
            if kind == "MARK":
                yield t, "MARK", line.strip()
            else:
                yield t, kind, bytes(int(x, 16) for x in rest.split())


SIGROK_RE = re.compile(r"^\s*(\d+)-(\d+)\s+\S+:\s+(?:[\w -]+:\s+)?([0-9A-Fa-f]{2})\s*$")


def parse_sigrok(path: str, direction: str, samplerate: float):
    """Yield one-byte chunks from `sigrok-cli -A uart=rx-data --protocol-decoder-samplenum` output."""
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = SIGROK_RE.match(line)
            if m:
                start = int(m.group(1))
                yield start * 1000.0 / samplerate, direction, bytes([int(m.group(3), 16)])


# ── Output ──────────────────────────────────────────────────────────────────


def format_text(f: Frame) -> str:
    hexs = " ".join(f"{b:02X}" for b in f.raw)
    return f"{f.t_ms:.1f} {f.direction} {hexs}  # {f.label()}"


def format_json(f: Frame) -> str:
    return json.dumps(
        {
            "t_ms": round(f.t_ms, 1),
            "dir": f.direction,
            "hex": f.raw.hex(" ").upper(),
            "type": f.label(),
            "checksum_ok": f.checksum_ok,
        }
    )


def print_stats(frames: list[Frame], out) -> None:
    counts: dict[tuple[str, str], int] = defaultdict(int)
    for f in frames:
        counts[(f.direction, f.label())] += 1
    print("\n== Frame counts ==", file=out)
    for (d, lbl), n in sorted(counts.items()):
        print(f"  {d:4} {lbl:40} {n}", file=out)

    # Request → response latency: a GET (or SET) from one side answered by
    # GET_RESP (or SET_ACK) from the other side.
    latencies: dict[str, list[float]] = defaultdict(list)
    periods: dict[str, list[float]] = defaultdict(list)
    last_req_time: dict[str, float] = {}
    pending: tuple[Frame, str] | None = None
    for f in frames:
        if not f.checksum_ok:
            continue
        if f.ptype in (0x42, 0x41):
            key = f.label()
            if key in last_req_time:
                periods[key].append(f.t_ms - last_req_time[key])
            last_req_time[key] = f.t_ms
            pending = (f, key)
        elif pending and f.direction != pending[0].direction:
            req, key = pending
            if (req.ptype == 0x42 and f.ptype == 0x62 and f.code == req.code) or (
                req.ptype == 0x41 and f.ptype == 0x61
            ):
                latencies[key].append(f.t_ms - req.t_ms)
                pending = None

    def summary(values: list[float]) -> str:
        return f"n={len(values)} min={min(values):.0f} avg={statistics.mean(values):.0f} max={max(values):.0f} ms"

    if latencies:
        print("\n== Request → response latency (time base: chunk flush, see docs) ==", file=out)
        for k in sorted(latencies):
            print(f"  {k:40} {summary(latencies[k])}", file=out)
    if periods:
        print("\n== Period between identical requests ==", file=out)
        for k in sorted(periods):
            print(f"  {k:40} {summary(periods[k])}", file=out)
    bad = sum(1 for f in frames if not f.checksum_ok)
    print(f"\nFrames: {len(frames)}  bad/garbage/truncated: {bad}", file=out)


# ── Main ────────────────────────────────────────────────────────────────────


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="*", help="ESPHome log files from the cn105-sniffer config")
    ap.add_argument("--sigrok-hp", help="sigrok-cli annotations for CN105 pin 4 (indoor unit → module)")
    ap.add_argument("--sigrok-mod", help="sigrok-cli annotations for CN105 pin 5 (module → indoor unit)")
    ap.add_argument("--samplerate", type=float, default=1e6, help="sigrok capture samplerate in Hz (default 1e6)")
    ap.add_argument("--format", choices=("text", "jsonl"), default="text")
    ap.add_argument("--stats", action="store_true", help="print a summary to stderr")
    ap.add_argument("-o", "--output", help="output file (default stdout)")
    args = ap.parse_args(argv)

    if not args.logs and not args.sigrok_hp and not args.sigrok_mod:
        ap.error("give at least one ESPHome log or a --sigrok-hp/--sigrok-mod file")

    chunks = []
    for path in args.logs:
        chunks += parse_esphome_log(path)
    if args.sigrok_hp:
        chunks += parse_sigrok(args.sigrok_hp, "HP", args.samplerate)
    if args.sigrok_mod:
        chunks += parse_sigrok(args.sigrok_mod, "MOD", args.samplerate)
    chunks.sort(key=lambda c: c[0])  # stable: keeps file order for equal timestamps

    reassemblers = {"HP": Reassembler("HP"), "MOD": Reassembler("MOD")}
    frames: list[Frame] = []
    markers: list[tuple[float, str]] = []
    for t, kind, payload in chunks:
        if kind == "MARK":
            markers.append((t, payload))
        else:
            frames += reassemblers[kind].feed(t, payload)
    for r in reassemblers.values():
        frames += r.flush()
    frames.sort(key=lambda f: f.t_ms)

    fmt = format_json if args.format == "jsonl" else format_text
    lines = [(f.t_ms, fmt(f)) for f in frames]
    lines += [(t, f"# MARK {t} {text}") for t, text in markers]
    lines.sort(key=lambda x: x[0])

    out = open(args.output, "w", encoding="utf-8") if args.output else sys.stdout
    try:
        for _, line in lines:
            print(line, file=out)
    finally:
        if args.output:
            out.close()
    if args.stats:
        print_stats(frames, sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
