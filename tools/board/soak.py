#!/usr/bin/env python3
"""Long-running stability test: cycle the board through different inputs and
summarise the status lines.

  soak.py [--minutes 30] [--clip deploy/rsn_final_v1/listen/general_05_source.wav]

Every --phase seconds (default 120) the input changes: live mic, a note
sequence, a gated vowel, a vowel with vibrato, a real recording, bypass.
Pass: no audio overruns, under 1 % late hops and none past the first
concealment tier, more than 256 B of stack left. The log goes to
captures/soak_<time>.log.

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""
import argparse
import re
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench  # noqa: E402

PHASES = [
    ("mic", ["src mic"]),
    ("seq", ["src seq 300 212 240 258 285 196 330"]),
    ("gated", ["src vowel 227 -12 0 5.5 100"]),
    ("vibrato", ["src vowel 305 -12 60 5.5"]),
    ("clip", None),                      # uploaded clip
    ("bypass", ["src seq 300 212 240 258", "mode bypass"]),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--minutes", type=float, default=30.0)
    ap.add_argument("--phase", type=float, default=120.0)
    ap.add_argument("--clip", default=str(Path(__file__).resolve().parents[2] /
                                          "deploy/rsn_final_v1/listen/general_05_source.wav"))
    a = ap.parse_args()

    s = bench.open_port()
    bench.CAP.mkdir(exist_ok=True)
    logp = bench.CAP / f"soak_{time.strftime('%Y%m%d_%H%M%S')}.log"
    log = open(logp, "w")
    print(f"soak {a.minutes} min -> {logp}")

    # upload the clip once, the board keeps it
    from scipy.io import wavfile
    import zlib
    sr, x = wavfile.read(a.clip)
    x = x.astype(np.float64) / (32768.0 if np.abs(x).max() > 2 else 1.0)
    n = int(1.5 * 48000)
    seg = x[int(0.5 * 48000): int(0.5 * 48000) + n]
    raw = np.clip(np.round(seg * 32767), -32768, 32767).astype("<i2").tobytes()
    bench.send(s, f"clipn {n}")
    for off in range(0, n, 32):
        s.write(f"L {off:x} {raw[off * 2:(off + 32) * 2].hex()}\n".encode())
        time.sleep(0.0025)
    r = bench.send(s, "clipcrc")
    ok = r.startswith("OK clipcrc") and int(r.split()[2], 16) == (zlib.crc32(raw) & 0xFFFFFFFF)
    print(f"clip upload {'OK' if ok else 'FAILED: ' + r}")
    print("start: " + bench.send(s, "stat"))
    log.write("# start " + bench.send(s, "stat") + "\n")

    t_end = time.time() + a.minutes * 60
    i = 0
    buf = b""
    while time.time() < t_end:
        name, cmds = PHASES[i % len(PHASES)]
        bench.send(s, "mode rsn")
        if cmds is None:
            r = bench.send(s, "clip play 0")
        else:
            for c in cmds:
                r = bench.send(s, c)
        log.write(f"# phase {name} @ {time.strftime('%H:%M:%S')}: {r}\n")
        print(f"  {time.strftime('%H:%M:%S')} phase {name}")
        t_ph = time.time() + a.phase
        while time.time() < min(t_ph, t_end):
            buf += s.read(65536)
            *lines, buf = buf.split(b"\n")
            for l in lines:
                log.write(l.decode("latin1").rstrip("\r") + "\n")
        log.flush()
        i += 1

    bench.send(s, "mode rsn")
    bench.send(s, "src mic")
    end = bench.send(s, "stat")
    log.write("# end " + end + "\n")
    log.close()
    print("end:   " + end)

    # summary
    txt = logp.read_text(errors="replace")
    V = [l for l in txt.splitlines() if l.startswith("Voice:")]
    lat = [int(m.group(1)) for l in V if (m := re.search(r"lat max (\d+) us", l))]
    mar = [int(m.group(1)) for l in V if (m := re.search(r"margin min (-?\d+) us", l))]
    miss = [int(m.group(1)) for l in V if (m := re.search(r"miss (\d+) \(", l))]
    t1 = [int(m.group(1)) for l in V if (m := re.search(r"t1 (\d+)", l))]
    t2 = [int(m.group(1)) for l in V if (m := re.search(r"t2 (\d+)", l))]
    B = [int(m.group(1)) for l in txt.splitlines() if (m := re.search(r"BUDGET worst/pass (\d+)", l))]
    st = dict(kv.split("=", 1) for kv in end.split()[2:] if "=" in kv)
    blocks = a.minutes * 60 * 1000
    print(f"\nSOAK SUMMARY ({a.minutes:.0f} min, {len(V)} status lines)")
    print(f"  hop latency max {max(lat) if lat else '?'} us | play-out margin min {min(mar) if mar else '?'} us")
    print(f"  deadline-missed blocks {st.get('misses', '?')} of ~{blocks:.0f} "
          f"({100 * int(st.get('misses', 0)) / blocks:.3f} %) | Tier-1 events {max(t1) if t1 else 0} "
          f"| Tier-2 events {max(t2) if t2 else 0}")
    print(f"  audio overruns {st.get('overruns', '?')} | UART rx_ovr {st.get('rx_ovr', '?')} "
          f"| stack free {st.get('stack_free', '?')} B | worst main-loop pass {max(B) // 600 if B else '?'} us")


if __name__ == "__main__":
    main()
