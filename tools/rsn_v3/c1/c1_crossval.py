#!/usr/bin/env python3
"""Compare the board's C pipeline with the Python models on a capture from
c1_capture.c: decimation, LPC residual, excitation and pitch-tracker
features, frame by frame. Also provides levinson() and the decimation filter
used by rsn.dsp.

    python c1_crossval.py capture.npz

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""
import argparse
import json
import os
import sys

import numpy as np
from scipy.signal import lfilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "excitation"))
sys.path.insert(0, os.path.join(HERE, "..", "swiftf0"))

from excitation_twin import Excitation          # noqa: E402
from swiftf0.frontend import features           # noqa: E402

GATE_DB = 35.0
P = 18
HOP = 80
WIN = 320
PRE = 0.97

S_IN = 0.00784313771873713
Z_IN = -1


def q_sat(feat_f32):
    q = np.rint(feat_f32.astype(np.float64) / S_IN) + Z_IN
    return np.clip(q, -128, 127).astype(np.int8)

H_DEC = np.array([
    -6.41059910e-04, -1.94995082e-04, +1.34449662e-03, +3.23004795e-03,
    +3.20467342e-03, -1.34167329e-04, -4.81215143e-03, -5.98887631e-03,
    -5.37089545e-04, +8.16384412e-03, +1.12326623e-02, +2.52597763e-03,
    -1.28121930e-02, -1.96579938e-02, -6.49117006e-03, +1.97746530e-02,
    +3.42050965e-02, +1.46817641e-02, -3.21926236e-02, -6.55265471e-02,
    -3.66705981e-02, +6.90613163e-02, +2.10423274e-01, +3.11008791e-01,
    +3.11008791e-01, +2.10423274e-01, +6.90613163e-02, -3.66705981e-02,
    -6.55265471e-02, -3.21926236e-02, +1.46817641e-02, +3.42050965e-02,
    +1.97746530e-02, -6.49117006e-03, -1.96579938e-02, -1.28121930e-02,
    +2.52597763e-03, +1.12326623e-02, +8.16384412e-03, -5.37089545e-04,
    -5.98887631e-03, -4.81215143e-03, -1.34167329e-04, +3.20467342e-03,
    +3.23004795e-03, +1.34449662e-03, -1.94995082e-04, -6.41059910e-04,
], dtype=np.float64)


def levinson(R, p=P):
    a = np.zeros(p)
    k = np.zeros(p)
    E = R[0]
    if E <= 0.0:
        return a, k
    for i in range(p):
        acc = R[i + 1] - sum(a[j] * R[i - j] for j in range(i))
        ki = acc / E if E > 1e-30 else 0.0
        ki = float(np.clip(ki, -0.99, 0.99))
        k[i] = ki
        a_new = a.copy()
        a_new[i] = ki
        a_new[:i] = a[:i] - ki * a[i - 1::-1][:i]
        a = a_new
        E *= (1.0 - ki * ki)
    return a, k


def frame_snr_db(ref, diff_from):
    ref = ref.astype(np.float64)
    d = ref - diff_from.astype(np.float64)
    num = float(np.sum(ref * ref))
    den = float(np.sum(d * d))
    if den <= 1e-30:
        return 200.0
    return 10.0 * np.log10(max(num, 1e-30) / den)


def summarize(name, snrs, quiet_mask=None):
    snrs = np.asarray(snrs)
    considered = snrs if quiet_mask is None else snrs[~quiet_mask]
    nq = 0 if quiet_mask is None else int(quiet_mask.sum())
    if len(considered) == 0:
        print(f"  {name:<22} ALL {len(snrs)} frames silent/empty -- the "
              f"capture is degenerate (arena not written?). FAIL")
        return False, np.array([-999.0])
    ok = bool((considered > GATE_DB).all()) and len(considered) > 0
    print(f"  {name:<22} frames {len(snrs):4d}"
          f"{f' (-{nq} silent)' if nq else '':14s}"
          f" min {considered.min():7.1f} dB   median"
          f" {np.median(considered):7.1f} dB   <{GATE_DB:.0f} dB:"
          f" {int((considered <= GATE_DB).sum()):3d}   "
          f"{'PASS' if ok else 'FAIL'}")
    if not ok:
        worst = np.argsort(considered)[:3]
        idx = np.arange(len(snrs))[~quiet_mask] if quiet_mask is not None \
            else np.arange(len(snrs))
        for w in worst:
            print(f"      worst: frame {idx[w]}  {considered[w]:.1f} dB")
    return ok, considered


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("npz")
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    z = np.load(args.npz, allow_pickle=False)
    meta = json.loads(str(z["meta"]))
    if meta["diag"]:
        raise SystemExit("capture was taken with voice_diag_matched=1 -- the "
                         "C1 twin models the PRODUCTION residual. Set it to 0 "
                         "and recapture.")
    if meta["d_audio_overruns"] or meta["d_rb_overruns"]:
        raise SystemExit("audio overruns during capture -- the streams have "
                         "holes and frame alignment is broken. Recapture.")

    hops = meta["hops"]
    raw = z["raw"]
    dec_c = z["dec"]
    hm = z["hopmeta"]
    results = {}

    print(f"cross-check: {hops} hops ({hops*5} ms), "
          f"{meta['ticks']} SwiftF0 ticks, gate > {GATE_DB:.0f} dB\n")

    x_full = np.concatenate([z["dstate"].astype(np.float64),
                             raw.astype(np.float64) / 32768.0])
    y_full = lfilter(H_DEC, 1.0, x_full)
    dec_py = y_full[47::3][:meta["decn"]]
    dec_py_i16 = np.clip(np.rint(dec_py * 32768.0), -32768, 32767).astype(np.int16)
    dmax = int(np.abs(dec_py_i16.astype(np.int32) -
                      dec_c.astype(np.int32)).max())
    dsnr = frame_snr_db(dec_c.astype(np.float64), dec_py_i16.astype(np.float64))
    print(f"  decimated stream       max |py-C| = {dmax} LSB, stream SNR "
          f"{dsnr:.1f} dB  (info: floor set by the int16 raw capture)")

    xp = np.empty_like(dec_py)
    xp[0] = dec_py[0] - PRE * meta["pre_state"]
    xp[1:] = dec_py[1:] - PRE * dec_py[:-1]
    XP = np.concatenate([z["xp240"].astype(np.float64), xp])
    win = np.hanning(WIN)

    resid_snr = np.empty(hops)
    quiet = np.zeros(hops, dtype=bool)
    for h in range(hops):
        seg = XP[h * HOP: h * HOP + WIN]
        wseg = seg * win
        R = np.array([np.dot(wseg[: WIN - i], wseg[i:]) for i in range(P + 1)])
        if R[0] < 1e-12:
            a = np.zeros(P)
        else:
            R = R.copy()
            R[0] *= 1.0001
            a, _ = levinson(R)
        blk = XP[h * HOP + WIN - HOP - P: h * HOP + WIN]
        e_py = lfilter(np.concatenate(([1.0], -a)), [1.0], blk)[P:]
        ref = z["resid"][h]
        quiet[h] = float(np.sum(ref.astype(np.float64) ** 2)) < 1e-12
        resid_snr[h] = frame_snr_db(ref, e_py)
    ok_res, cons = summarize("residual (C vs py)", resid_snr, quiet)
    results["residual_min_db"] = float(cons.min())

    osc = Excitation(norm="analytic", f32=True)
    osc.phase = np.float32(hm["ph0"][0])
    osc.f0_prev = np.float32(hm["f0prev"][0])
    exc_snr_free = np.empty(hops)
    for h in range(hops):
        if h > 0 and (int(hm["flags"][h]) & 2):
            osc.reset(float(hm["f0prev"][h]))
        out = osc.hop(float(hm["f0_corr"][h]))
        exc_snr_free[h] = frame_snr_db(z["exc"][h], out)
    ok_exc, cons = summarize("excitation free-run", exc_snr_free)
    results["excitation_min_db"] = float(cons.min())

    exc_snr_seed = np.empty(hops)
    for h in range(hops):
        o = Excitation(norm="analytic", f32=True)
        o.phase = np.float32(hm["ph0"][h])
        o.f0_prev = np.float32(hm["f0prev"][h])
        exc_snr_seed[h] = frame_snr_db(z["exc"][h],
                                       o.hop(float(hm["f0_corr"][h])))
    summarize("excitation seeded", exc_snr_seed)

    W = np.concatenate([z["ring"], dec_c])
    nt = meta["ticks"]
    feat_is_q8 = z["feat"].dtype == np.int8
    feat_snr = np.empty(nt)
    n_exact = 0
    for k in range(nt):
        c = int(z["tickc"][k])
        wave = W[c: c + 1024]
        feat_py = features(wave)
        if feat_is_q8:
            q_py = q_sat(feat_py)
            n_exact += int(np.array_equal(q_py, z["feat"][k]))
            feat_snr[k] = frame_snr_db(
                (z["feat"][k].astype(np.float64) - Z_IN) * S_IN,
                (q_py.astype(np.float64) - Z_IN) * S_IN)
        else:
            feat_snr[k] = frame_snr_db(z["feat"][k], feat_py)
    ok_feat, cons = summarize("SwiftF0 front-end", feat_snr)
    if feat_is_q8:
        print(f"      (int8 NPU-input domain; {n_exact}/{nt} frames bit-exact)")
    results["frontend_min_db"] = float(cons.min())

    ok = ok_res and ok_exc and ok_feat
    print(f"\nresult: {'PASS' if ok else 'FAIL'}")
    results["pass"] = ok
    if args.json:
        with open(args.json, "w") as f:
            json.dump(results, f, indent=2)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
