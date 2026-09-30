#!/usr/bin/env python3
"""Turn a console log with "C1|" lines from the board into capture.npz for
c1_crossval.py.

    python c1_parse.py console.log -o capture.npz

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""
import argparse
import json
import struct
import sys
import zlib

import numpy as np

SECTION_IDS = "XDGRdECMFT"

HOPMETA_DTYPE = np.dtype([
    ("f0_corr", "<f4"), ("ph0", "<f4"), ("f0prev", "<f4"),
    ("flags", "u1"), ("pad", "u1", (3,)),
])


def f32_from_bits(u32: int) -> float:
    return struct.unpack("<f", struct.pack("<I", u32))[0]


def parse(path: str):
    header = None
    footer = None
    chunks = {s: [] for s in SECTION_IDS}
    crc_all = 0

    with open(path, "r", errors="replace") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            pos = line.find("C1|")
            if pos < 0:
                continue
            line = line[pos:]
            parts = line.split("|")
            tag = parts[1]
            if tag == "H":
                kv = {}
                for item in parts[2].split(";"):
                    if "=" in item:
                        k, v = item.split("=", 1)
                        kv[k] = v
                header = kv
            elif tag == "Z":
                footer = (int(parts[2], 16), int(parts[3], 16))
            elif tag in SECTION_IDS and len(parts) == 5:
                off = int(parts[2], 16)
                payload = bytes.fromhex(parts[3])
                crc = int(parts[4], 16)
                calc = zlib.crc32(payload) & 0xFF
                if crc != calc:
                    raise SystemExit(
                        f"{path}:{lineno}: CRC mismatch on section {tag} "
                        f"offset {off:#x} (got {crc:02x}, want {calc:02x}) "
                        f"-- corrupted or dropped UART data; recapture.")
                chunks[tag].append((off, payload))

    if header is None:
        raise SystemExit("no C1|H header line found -- is this a C1 dump?")
    if footer is None:
        raise SystemExit(
            "no C1|Z footer -- the dump was still streaming when logging "
            "stopped. Keep the logger attached until the board prints "
            "'C1: dump complete' (~20 s after the capture), then re-run.")

    sections = {}
    for s in SECTION_IDS:
        parts_s = sorted(chunks[s], key=lambda t: t[0])
        pos = 0
        buf = bytearray()
        for off, payload in parts_s:
            if off != pos:
                raise SystemExit(
                    f"section {s}: gap/overlap at offset {pos:#x} "
                    f"(next line starts {off:#x}) -- dropped UART line; recapture.")
            buf += payload
            pos += len(payload)
        sections[s] = bytes(buf)
        crc_all = zlib.crc32(sections[s], crc_all)

    total = sum(len(v) for v in sections.values())
    if total != footer[0]:
        raise SystemExit(f"total bytes {total} != footer {footer[0]} -- truncated.")
    if (crc_all & 0xFFFFFFFF) != footer[1]:
        raise SystemExit("whole-dump CRC32 mismatch -- recapture.")

    hops = int(header["hops"])
    rawn = int(header["raw"])
    decn = int(header["dec"])
    ticks = int(header["ticks"])
    nbins = int(header["nbins"])
    ver = int(header.get("v", "1"))
    feat_dtype = "<f4" if ver < 2 else "i1"

    def as_np(sec, dtype, count):
        a = np.frombuffer(sections[sec], dtype=dtype)
        if len(a) != count:
            raise SystemExit(f"section {sec}: {len(a)} items, expected {count}")
        return a

    data = {
        "xp240":  as_np("X", "<f4", 240),
        "dstate": as_np("D", "<f4", 47),
        "ring":   as_np("G", "<i2", 1024),
        "raw":    as_np("R", "<i2", rawn),
        "dec":    as_np("d", "<i2", decn),
        "resid":  as_np("E", "<f4", hops * 80).reshape(hops, 80),
        "exc":    as_np("C", "<f4", hops * 80).reshape(hops, 80),
        "hopmeta": np.frombuffer(sections["M"], dtype=HOPMETA_DTYPE),
        "feat":   as_np("F", feat_dtype, ticks * nbins).reshape(ticks, nbins),
        "tickc":  as_np("T", "<u4", ticks),
    }
    if len(data["hopmeta"]) != hops:
        raise SystemExit("hopmeta count mismatch")

    meta = {
        "hops": hops, "rawn": rawn, "decn": decn, "ticks": ticks,
        "nbins": nbins, "version": ver,
        "mode": int(header.get("mode", "0")),
        "diag": int(header.get("diag", "0")),
        "pre_state": f32_from_bits(int(header["pre"], 16)),
        "d_audio_overruns": int(header.get("dovr", "0")),
        "d_rb_overruns": int(header.get("drbo", "0")),
        "d_fifo_underruns": int(header.get("dfif", "0")),
    }
    return data, meta


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("-o", "--out", default="capture.npz")
    ap.add_argument("--wav", default=None,
                    help="also write the raw 48 kHz capture as a WAV")
    args = ap.parse_args()

    data, meta = parse(args.log)
    np.savez_compressed(args.out, meta=json.dumps(meta), **data)
    print(f"parsed OK: {meta['hops']} hops, {meta['ticks']} ticks, "
          f"{meta['rawn']} raw samples -> {args.out}")
    if meta["diag"]:
        print("WARNING: captured with voice_diag_matched=1 -- the residual is "
              "the DIAGNOSTIC lattice residual, not the production one. "
              "c1_crossval.py will refuse it.")
    for k in ("d_audio_overruns", "d_rb_overruns", "d_fifo_underruns"):
        if meta[k]:
            print(f"WARNING: {k} = {meta[k]} during capture -- stream may "
                  f"have holes; the SNR gate is not trustworthy. Recapture.")

    if args.wav:
        import wave
        with wave.open(args.wav, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(48000)
            w.writeframes(data["raw"].tobytes())
        print(f"wrote {args.wav} ({meta['rawn']/48000:.2f} s @48 kHz)")


if __name__ == "__main__":
    main()
