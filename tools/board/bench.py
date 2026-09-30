#!/usr/bin/env python3
"""Drive the board's test bench (bench.c) over the ST-LINK serial port.

The board must be running (board.sh load).

  bench.py cmd "mode rsn" "stat"             send commands, print the replies
  bench.py vowel 233 [--mode rsn] [--ms 600] [--vib 0] [--db -12] [--key 0 --scale chrom]
  bench.py seq 400 212 240 258 285           note sequence: dwell ms, then pitches
  bench.py latency                           input -> output delay
  bench.py mic [--ms 600]                    capture the live microphone path
  bench.py clip <wav> [--start 0.5] [--db 0] upload 1.5 s of singing and play it in
  bench.py record [--secs 30] [--both] [--name take1]
                                             record the output (and with --both the input)
  bench.py loopback, bench.py d3             analog latency, HF band flatness

Captures go to captures/<tag>_in.wav and _out.wav. Pitch is measured by
autocorrelation on 40 ms frames; "off-scale" is the distance from the nearest
note of the selected key.

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""
import argparse
import re
import sys
import time
from pathlib import Path

import numpy as np
import serial
from scipy.io import wavfile

HERE = Path(__file__).resolve().parent
CAP = HERE / "captures"
TTY_GLOB = "/dev/cu.usbmodem*"
FS = 48000


def open_port():
    import glob
    ttys = sorted(glob.glob(TTY_GLOB))
    if not ttys:
        sys.exit("no ST-LINK VCP (/dev/cu.usbmodem*)")
    s = serial.Serial(ttys[0], 2000000, timeout=0.05)
    time.sleep(0.1)
    s.write(b"\n")          # the VCP drops the first bytes after an open
    time.sleep(0.1)
    return s


def send(s, line, wait=2.0, want=("OK", "ERR"), retry=True):
    """Send one command and return the reply line, retrying once on silence."""
    r = _send(s, line, wait, want)
    if r == "(no reply)" and retry:
        r = _send(s, line, wait, want)
    return r


def _send(s, line, wait, want):
    s.reset_input_buffer()
    for ch in (line + "\n").encode():
        s.write(bytes([ch]))
        time.sleep(0.0005)
    t0 = time.time()
    buf = b""
    while time.time() - t0 < wait:
        buf += s.read(4096)
        for l in buf.split(b"\n"):
            l = l.decode("latin1").strip()
            for w in want:                     # the reply may follow binary data
                k = l.find(w)
                if k >= 0:
                    return l[k:]
    return "(no reply)"


def capture(s, ms, timeout=30.0):
    """Arm a capture and collect the D1C dump. Returns (meta, x_in, x_out)."""
    s.reset_input_buffer()
    r = send(s, f"cap {ms}", want=("OK cap", "ERR"))
    if not r.startswith("OK"):
        sys.exit(f"capture refused: {r}")
    return _collect(s, int(r.split()[2]), timeout)


def capture_existing(s, timeout=30.0):
    """Collect a dump whose capture was armed by another command (click)."""
    return _collect(s, 60 * 48, timeout)


def _collect(s, n, timeout):
    xin = np.zeros(n, np.int16)
    xout = np.zeros(n, np.int16)
    got = {"I": set(), "O": set()}
    meta, bad = {}, 0
    hops = {}
    buf = b""
    t0 = time.time()
    done = False
    while time.time() - t0 < timeout and not done:
        buf += s.read(65536)
        *lines, buf = buf.split(b"\n")
        for l in lines:
            l = l.decode("latin1").strip()
            if not l.startswith("D1C|"):
                continue
            p = l.split("|")
            if p[1] == "H":
                meta = dict(kv.split("=", 1) for kv in p[2:] if "=" in kv)
            elif p[1] in ("I", "O"):
                try:
                    off = int(p[2], 16)
                    raw = bytes.fromhex(p[3])
                    crc = int(p[4], 16)
                except (ValueError, IndexError):
                    bad += 1
                    continue
                c = 0
                for b in raw:
                    c ^= b
                    for _ in range(8):
                        c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
                if c != crc:
                    bad += 1
                    continue
                v = np.frombuffer(raw, "<i2")
                (xin if p[1] == "I" else xout)[off:off + len(v)] = v
                got[p[1]].add(off)
            elif p[1] == "F":
                try:
                    i0 = int(p[2])
                    for j, pair in enumerate(x for x in p[3].split(";") if x):
                        c, a_ = pair.split(",")
                        hops[i0 + j] = (int(c), int(a_))
                except (ValueError, IndexError):
                    bad += 1
            elif p[1] == "Z":
                done = True
    need = (n + 31) // 32
    for ch in "IO":                                   # re-fetch lines the VCP dropped
        for off in sorted(set(range(0, n, 32)) - got[ch]):
            r = send(s, f"line {ch} {off:x}", wait=1.0, want=(f"D1C|{ch}|{off:06x}",))
            p = r.split("|")
            if len(p) >= 5:
                raw = bytes.fromhex(p[3])
                v = np.frombuffer(raw, "<i2")
                (xin if ch == "I" else xout)[off:off + len(v)] = v
                got[ch].add(off)
    if len(got["I"]) < need or len(got["O"]) < need:
        miss = {ch: sorted(set(range(0, n, 32)) - got[ch])[:4] for ch in "IO"}
        print(f"  WARNING: incomplete dump (in {len(got['I'])}/{need}, out {len(got['O'])}/{need}, "
              f"bad {bad}, missing offsets {miss})")
    meta["hops"] = np.array([hops[i] for i in sorted(hops)]) if hops else np.zeros((0, 2))
    return meta, xin.astype(np.float64) / 32768.0, xout.astype(np.float64) / 32768.0


def _crc16(b):
    c = 0xFFFF
    for x in b:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def record(s, secs, both, name):
    """Stream the board's audio (bench.c "rec") into WAV files."""
    mode = 2 if both else 1
    plen = 144 if mode == 2 else 96
    flen = 5 + plen + 2
    # stop any stream a previous session left running, then start clean
    s.write(b"\nrec 0\n")
    time.sleep(0.3)
    s.reset_input_buffer()
    r = send(s, f"rec {mode}", want=("OK rec", "ERR"))
    if not r.startswith("OK rec"):
        sys.exit(f"board refused: {r}")
    frames, bad, buf = {}, 0, b""
    base, last = None, None
    t0 = time.time()
    print(f"  recording {'input + output (12-bit)' if both else 'output (16-bit, bit-exact)'} "
          f"for {secs:.0f} s -- sing! (Ctrl-C to stop early)")
    t_ka = 0.0
    try:
        while time.time() - t0 < secs:
            if time.time() - t_ka > 1.0:       # keepalive, the board stops after 3 s without one
                s.write(b"\n")
                t_ka = time.time()
            buf += s.read(65536)
            i = 0
            while True:
                j = buf.find(b"\xa5\x5a", i)
                if j < 0 or len(buf) - j < flen:
                    buf = buf[j:] if j >= 0 else buf[-1:]
                    break
                fr = buf[j:j + flen]
                if fr[4] != mode or _crc16(fr[2:flen - 2]) != (fr[flen - 2] | fr[flen - 1] << 8):
                    bad += 1
                    i = j + 1
                    continue
                seq = fr[2] | fr[3] << 8
                if last is None:
                    base, last = seq, seq
                # unwrap the 16-bit counter
                n = (last & ~0xFFFF) | seq
                if n < last - 0x8000:
                    n += 0x10000
                last = n
                frames[n - base] = bytes(fr[5:5 + plen])
                i = j + flen
    except KeyboardInterrupt:
        pass
    s.write(b"\n")
    stop = send(s, "rec 0", wait=3.0, want=("OK rec 0",))
    if not frames:
        sys.exit(f"no frames received ({stop})")
    nf = max(frames) + 1
    out = np.zeros(nf * 48, np.int16)
    inp = np.zeros(nf * 48, np.int16) if both else None
    for k, p in frames.items():
        if mode == 1:
            out[k * 48:(k + 1) * 48] = np.frombuffer(p, "<i2")
        else:
            b3 = np.frombuffer(p, np.uint8).reshape(48, 3).astype(np.int32)
            i12 = b3[:, 0] | ((b3[:, 1] & 0x0F) << 8)
            o12 = (b3[:, 1] >> 4) | (b3[:, 2] << 4)
            i12 = np.where(i12 >= 2048, i12 - 4096, i12)
            o12 = np.where(o12 >= 2048, o12 - 4096, o12)
            inp[k * 48:(k + 1) * 48] = (i12 << 4).astype(np.int16)
            out[k * 48:(k + 1) * 48] = (o12 << 4).astype(np.int16)
    missing = nf - len(frames)
    CAP.mkdir(exist_ok=True)
    name = name or time.strftime("rec_%Y%m%d_%H%M%S")
    wavfile.write(CAP / f"{name}_out.wav", FS, out)
    if both:
        wavfile.write(CAP / f"{name}_in.wav", FS, inp)
    print(f"  {nf / 1000:.2f} s recorded, {missing} of {nf} blocks missing"
          f"{' (filled with silence)' if missing else ''}, {bad} corrupt frames skipped | board: {stop}")
    print(f"  -> {CAP / (name + '_out.wav')}" + (f"\n  -> {CAP / (name + '_in.wav')}" if both else ""))


def f0_track(x, fs=FS, frame=0.040, hop=0.010, fmin=70.0, fmax=1000.0):
    """Per-frame F0 (Hz, 0 = unvoiced) by normalized autocorrelation."""
    from scipy.signal import resample_poly
    y = resample_poly(x, 1, 3)                     # 16 kHz is plenty for F0
    fs2 = fs / 3
    N, H = int(frame * fs2), int(hop * fs2)
    lo, hi = int(fs2 / fmax), int(fs2 / fmin)
    out = []
    for i in range(0, len(y) - N, H):
        f = y[i:i + N] - y[i:i + N].mean()
        e = np.dot(f, f)
        if e < 1e-7 * N:
            out.append(0.0)
            continue
        r = np.correlate(f, f, "full")[N - 1:]
        r = r / (e + 1e-12)
        k = lo + int(np.argmax(r[lo:hi]))
        if r[k] < 0.5:
            out.append(0.0)
            continue
        a, b, c = r[k - 1], r[k], r[k + 1] if k + 1 < len(r) else r[k]
        d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
        out.append(fs2 / (k + d))
    return np.array(out)


def scale_err_cents(f, key=0, mask=0xFFF):
    """Distance (cents) of f from the nearest note in key/scale."""
    if f <= 0:
        return np.nan
    st = 12 * np.log2(f / 440.0)
    best = 1e9
    for n in range(int(np.floor(st)) - 7, int(np.ceil(st)) + 8):
        deg = ((69 + n) - key) % 12
        if (mask >> deg) & 1:
            best = min(best, abs(st - n) * 100)
    return best


def tracking(meta, fo, hop=0.010, frame=0.040):
    """Output F0 frames vs the firmware's own per-hop target (f0_corr), the
    target taken at input time = output time - D_PATH. Returns cents errors."""
    h = meta.get("hops", np.zeros((0, 2)))
    if len(h) == 0:
        return np.array([])
    dpath = int(meta.get("dpath", 425)) / FS
    corr = h[:, 0].astype(float)
    err = []
    for k, f in enumerate(fo):
        if f <= 0:
            continue
        t0 = k * hop - dpath                    # frame span in input time
        j0, j1 = int(np.floor(t0 / 0.005)), int(np.ceil((t0 + frame) / 0.005))
        if j0 < 0 or j1 > len(corr):
            continue
        c = corr[j0:j1]
        c = c[c != 0]
        if len(c) < (j1 - j0) // 2:
            continue
        tgt = 440.0 * 2 ** (np.mean(c) / 1200.0)
        err.append(1200 * np.log2(f / tgt))
    return np.array(err)


def note_name(f):
    names = "C C# D D# E F F# G G# A A# B".split()
    m = int(round(69 + 12 * np.log2(f / 440.0)))
    return f"{names[m % 12]}{m // 12 - 1}"


def save(tag, xin, xout):
    CAP.mkdir(exist_ok=True)
    for nm, x in (("in", xin), ("out", xout)):
        wavfile.write(CAP / f"{tag}_{nm}.wav", FS, (np.clip(x, -1, 1) * 32767).astype(np.int16))
    return CAP / f"{tag}_out.wav"


def report_pitch(tag, meta, xin, xout, key, mask, settle_s=0.0):
    fi, fo = f0_track(xin), f0_track(xout)
    k0 = int(settle_s / 0.010)
    fi, fo = fi[k0:], fo[k0:]
    vi, vo = fi[fi > 0], fo[fo > 0]
    rms_blocks = np.sqrt(np.mean(xout[: len(xout) // 240 * 240].reshape(-1, 240) ** 2, axis=1))
    lvl_in = 20 * np.log10(np.sqrt(np.mean(xin ** 2)) + 1e-12)
    lvl_out = 20 * np.log10(np.sqrt(np.mean(xout ** 2)) + 1e-12)
    mi = np.median(vi) if len(vi) else 0
    mo = np.median(vo) if len(vo) else 0
    print(f"  [{tag}] {meta.get('mode','?')}  in {mi:7.2f} Hz ({note_name(mi) if mi else '-'}, "
          f"{scale_err_cents(mi, key, mask):5.1f} c off-scale)  ->  out {mo:7.2f} Hz "
          f"({note_name(mo) if mo else '-'}, {scale_err_cents(mo, key, mask):5.1f} c off-scale)")
    if len(vo):
        errs = np.array([scale_err_cents(f, key, mask) for f in vo])
        print(f"    out frames voiced {len(vo)}/{len(fo)} | off-scale error median {np.median(errs):.1f} c, "
              f"p90 {np.percentile(errs, 90):.1f} c | level in {lvl_in:.1f} dBFS out {lvl_out:.1f} dBFS | "
              f"5 ms blocks < -60 dBFS: {int(np.sum(rms_blocks < 1e-3))}/{len(rms_blocks)}")
    return fi, fo


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="what", required=True)
    c = sub.add_parser("cmd"); c.add_argument("lines", nargs="+")
    v = sub.add_parser("vowel"); v.add_argument("f0", type=float)
    q = sub.add_parser("seq"); q.add_argument("dwell", type=int); q.add_argument("f0s", type=float, nargs="+")
    lt = sub.add_parser("latency"); lt.add_argument("--f0", type=float, default=220.0)
    lt.add_argument("--modes", default="plain,rsn")
    rc = sub.add_parser("record"); rc.add_argument("--secs", type=float, default=30.0)
    rc.add_argument("--both", action="store_true"); rc.add_argument("--name", default=None)
    sub.add_parser("d3")
    sub.add_parser("loopback")
    m = sub.add_parser("mic")
    cl = sub.add_parser("clip"); cl.add_argument("wav"); cl.add_argument("--start", type=float, default=0.5)
    cl.add_argument("--db", type=float, default=0.0)
    for p in (v, q, m, cl):
        p.add_argument("--mode", default="rsn")
        p.add_argument("--ms", type=int, default=600)
        p.add_argument("--key", type=int, default=0)
        p.add_argument("--scale", default="chrom")
        p.add_argument("--tag", default=None)
    v.add_argument("--db", type=float, default=-12.0)
    v.add_argument("--vib", type=float, default=0.0)
    v.add_argument("--vibhz", type=float, default=5.5)
    a = ap.parse_args()

    s = open_port()
    if a.what == "cmd":
        for l in a.lines:
            print(f"> {l}\n  {send(s, l)}")
        return

    masks = {"chrom": 0xFFF, "major": 0xAB5, "minor": 0x5AD}
    if a.what == "record":
        record(s, a.secs, a.both, a.name)
        return
    if a.what == "loopback":
        # Press a headphone earcup onto the board's mic. The board clicks its
        # output and captures its own mic; the echo delay is the analog chain.
        # End-to-end latency = D_PATH + this.
        send(s, "mode bypass"); send(s, "src mic")
        lags = []
        for trial in range(12):
            send(s, "click", want=("OK click", "ERR"))
            meta, xin, xout = capture_existing(s)
            k_out = int(np.argmax(np.abs(xout)))
            e = np.abs(xin) - np.median(np.abs(xin))
            thr = 6 * (np.median(np.abs(e - np.median(e))) + 1e-9)
            hit = np.nonzero(e[k_out:] > max(thr, 0.01))[0]
            if len(hit):
                lags.append(int(hit[0]))
                print(f"  trial {trial}: echo {hit[0]} smp = {hit[0] / 48:.2f} ms (peak {e[k_out + hit[0]]:.3f})")
            else:
                print(f"  trial {trial}: no echo above noise (earcup on the mic? volume up?)")
            time.sleep(0.3)
        send(s, "mode rsn")
        if lags:
            L = np.median(lags)
            print(f"  loopback (analog I/O chain) median {L:.0f} smp = {L / 48:.2f} ms over {len(lags)} trials")
            print(f"  END-TO-END = D_PATH 8.85 ms + loopback {L / 48:.2f} ms = {8.85 + L / 48:.2f} ms "
                  f"(plain path; RSN onsets +~1.8 ms)")
        return

    if a.what == "d3":
        # Magnitude response of voice band + HF band: white noise through the
        # plain round trip, aligned by D_PATH, H = Pxy / Pxx.
        from scipy.signal import csd, welch
        res = {}
        for hf in (1, 0):
            for l in ("mode plain", "route 0", f"hf {hf}", "src noise -20"):
                send(s, l)
            time.sleep(1.0)
            meta, xin, xout = capture(s, 1400)
            save(f"d3_hf{hf}", xin, xout)
            dp = int(meta.get("dpath", 425))
            xi, xo = xin[: len(xin) - dp], xout[dp:]
            f, pxy = csd(xi, xo, fs=FS, nperseg=4096)
            _, pxx = welch(xi, fs=FS, nperseg=4096)
            res[hf] = (f, 20 * np.log10(np.abs(pxy) / pxx + 1e-12))
        send(s, "hf 1"); send(s, "route 1"); send(s, "mode rsn"); send(s, "src mic")
        f = res[1][0]
        print("  band (kHz)   |H| HF on   |H| HF off   (dB, re input, aligned by D_PATH)")
        for lo, hi in ((0.3, 1), (1, 3), (3, 5), (5, 6), (6, 6.5), (6.5, 7), (7, 7.5), (7.5, 8), (8, 9), (9, 12), (12, 16)):
            k = (f >= lo * 1000) & (f < hi * 1000)
            print(f"  {lo:4.1f}-{hi:4.1f}      {np.median(res[1][1][k]):+6.2f}      {np.median(res[0][1][k]):+6.2f}")
        k = (f >= 5000) & (f <= 9000)
        h = res[1][1][k]
        sm = np.convolve(h, np.ones(9) / 9, "same")[4:-4]
        print(f"  5-9 kHz with HF: min {sm.min():+.2f} max {sm.max():+.2f} dB -> spread {sm.max() - sm.min():.2f} dB "
              f"(spec: flat +-0.5 dB); D_PATH used {dp}")
        return
    if a.what == "latency":
        # A 220 Hz vowel gated on/off every 50 ms: coarse lag from the
        # envelopes, fine lag from the waveform within half a period.
        from scipy.signal import correlate
        res = {}
        for mode in a.modes.split(","):
            for l in (f"mode {mode}", f"src vowel {a.f0} -12 0 5.5 100", "key 0", "scale chrom"):
                send(s, l)
            time.sleep(1.5)
            meta, xin, xout = capture(s, 600)
            save(f"latency_{mode}", xin, xout)
            def env(x, w=48):
                e = np.convolve(x ** 2, np.ones(w) / w, "same")
                return np.log10(e + 1e-9)
            ei, eo = env(xin), env(xout)
            ei -= ei.mean(); eo -= eo.mean()
            r = correlate(eo, ei, "full", method="fft")
            lags = np.arange(-len(ei) + 1, len(eo))
            keep = (lags >= 0) & (lags <= 48 * 30)
            kc = int(lags[keep][np.argmax(r[keep])])
            kf = kc
            if mode == "plain":
                r2 = correlate(xout, xin, "full", method="fft")
                hp = int(24000 / a.f0)
                win = (lags >= kc - hp) & (lags <= kc + hp)
                kf = int(lags[win][np.argmax(r2[win])])
            res[mode] = (kc, kf)
            print(f"  {mode:5s}: envelope lag {kc} smp ({kc / 48:.2f} ms)"
                  + (f", waveform-refined {kf} smp ({kf / 48:.2f} ms)" if mode == "plain" else ""))
        print(f"  firmware D_PATH {meta.get('dpath','?')} smp = {int(meta.get('dpath', 0)) / 48:.2f} ms "
              f"(capture point: mic block in -> speaker block out, before the SAI/codec)")
        send(s, "mode rsn"); send(s, "src mic")
        return

    send(s, f"mode {a.mode}")
    send(s, f"key {a.key}")
    send(s, f"scale {a.scale}")
    mask = masks.get(a.scale, int(a.scale, 16) if re.fullmatch(r"[0-9a-fA-F]+", a.scale) else 0xFFF)
    if a.what == "clip":
        sr, x = wavfile.read(a.wav)
        x = x.astype(np.float64)
        if np.abs(x).max() > 2:
            x /= 32768.0
        if x.ndim > 1:
            x = x[:, 0]
        assert sr == FS, f"{a.wav}: {sr} Hz, want 48000"
        n = int(1.5 * FS)
        seg = x[int(a.start * FS): int(a.start * FS) + n]
        seg = np.concatenate([seg, np.zeros(n - len(seg))])
        q16 = np.clip(np.round(seg * 32767), -32768, 32767).astype("<i2")
        raw = q16.tobytes()
        import zlib
        want = zlib.crc32(raw) & 0xFFFFFFFF
        for attempt in range(3):
            send(s, f"clipn {n}")
            for off in range(0, n, 32):
                line = f"L {off:x} {raw[off * 2:(off + 32) * 2].hex()}\n".encode()
                s.write(line)
                time.sleep(0.0025)
            r = send(s, "clipcrc")
            got = int(r.split()[2], 16) if r.startswith("OK clipcrc") else -1
            if got == want:
                break
            print(f"  upload CRC mismatch ({r}), retrying")
        else:
            sys.exit("clip upload failed")
        print("  " + send(s, f"clip play {a.db}"))
    elif a.what == "vowel":
        print("  " + send(s, f"src vowel {a.f0} {a.db} {a.vib} {a.vibhz}"))
    elif a.what == "seq":
        print("  " + send(s, "src seq " + str(a.dwell) + " " + " ".join(str(f) for f in a.f0s)))
    else:
        print("  " + send(s, "src mic"))
    time.sleep(1.5)                                   # gate + F0 logic settle
    meta, xin, xout = capture(s, a.ms if a.what != "clip" else 1400)
    tag = a.tag or (f"clip_{Path(a.wav).stem}_{a.mode}" if a.what == "clip" else f"{a.what}_{a.mode}_{int(time.time())}")
    wav = save(tag, xin, xout)
    if a.what == "seq":
        fi, fo = f0_track(xin), f0_track(xout)
        seg = max(1, int(a.dwell / 10))
        print(f"  per ~{a.dwell} ms segment (in -> out, off-scale cents):")
        for i in range(0, len(fi) - seg + 1, seg):
            a_i = fi[i + seg // 4: i + seg][fi[i + seg // 4: i + seg] > 0]
            a_o = fo[i + seg // 4: i + seg][fo[i + seg // 4: i + seg] > 0]
            if len(a_i) and len(a_o):
                mi, mo = np.median(a_i), np.median(a_o)
                print(f"    {mi:7.2f} Hz ({scale_err_cents(mi, a.key, mask):5.1f} c) -> {mo:7.2f} Hz "
                      f"{note_name(mo):>4} ({scale_err_cents(mo, a.key, mask):5.1f} c)")
    elif a.what == "clip":
        fi, fo = f0_track(xin), f0_track(xout)
        both = (fi > 0) & (fo > 0)
        ei = np.array([scale_err_cents(f, a.key, mask) for f in fi[both]])
        eo = np.array([scale_err_cents(f, a.key, mask) for f in fo[both]])
        lvl = lambda x: 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)
        te = tracking(meta, fo)
        if len(te):
            print(f"  [{tag}] tracking out vs firmware target: median |err| {np.median(np.abs(te)):.1f} c, "
                  f"p90 {np.percentile(np.abs(te), 90):.1f} c, bias {np.median(te):+.1f} c ({len(te)} frames)")
        print(f"  [{tag}] {meta.get('mode','?')}: voiced frames {both.sum()}/{len(fi)} | off-scale cents "
              f"in median {np.median(ei):.1f} (p90 {np.percentile(ei, 90):.1f}) -> out median "
              f"{np.median(eo):.1f} (p90 {np.percentile(eo, 90):.1f}) | level in {lvl(xin):.1f} out "
              f"{lvl(xout):.1f} dBFS ({lvl(xout) - lvl(xin):+.1f} dB)")
    else:
        fi, fo = report_pitch(tag, meta, xin, xout, a.key, mask)
        te = tracking(meta, f0_track(xout))
        if len(te):
            print(f"    tracking out vs firmware target: median |err| {np.median(np.abs(te)):.1f} c, "
                  f"p90 {np.percentile(np.abs(te), 90):.1f} c, bias {np.median(te):+.1f} c")
    print(f"  wav: {wav}")
    if a.what != "mic":
        send(s, "src mic")


if __name__ == "__main__":
    main()
