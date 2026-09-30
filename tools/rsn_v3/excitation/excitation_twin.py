"""Python version of the firmware's excitation oscillator (excitation.c).
Running it directly checks that there are no hop-rate sidebands.

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

import numpy as np

FS = 16000.0
HOP = 80
HOP_RATE = FS / HOP


def poly_blep(t: float, dt: float) -> float:
    if t < dt:
        t = t / dt
        return t + t - t * t - 1.0
    if t > 1.0 - dt:
        t = (t - 1.0) / dt
        return t * t + t + t + 1.0
    return 0.0


class Excitation:

    def __init__(self, norm: str = "analytic", f32: bool = True):
        self.dt_t = np.float32 if f32 else np.float64
        self.phase = self.dt_t(0.0)
        self.f0_prev = self.dt_t(0.0)
        self.norm = norm
        self.ramp = True

    def reset(self, f0: float):
        self.phase = self.dt_t(0.0)
        self.f0_prev = self.dt_t(f0)

    def hop(self, f0_new: float) -> np.ndarray:
        T = self.dt_t
        f0_new = T(f0_new)
        if self.f0_prev <= 0.0:
            self.f0_prev = f0_new
        out = np.empty(HOP, dtype=T)
        f0_0 = self.f0_prev
        df = T((f0_new - f0_0) / T(HOP)) if self.ramp else T(0.0)
        for n in range(HOP):
            f = T(f0_0 + df * T(n + 1)) if self.ramp else f0_new
            dt = T(f * T(1.0 / FS))
            self.phase = T(self.phase + dt)
            self.phase = T(self.phase - np.floor(self.phase))
            out[n] = T(T(T(2.0) * self.phase - T(1.0)) - T(poly_blep(float(self.phase), float(dt))))
        self.f0_prev = f0_new

        if self.norm == "analytic":
            out = (out * T(1.7320508)).astype(T)
        elif self.norm == "per_hop":
            r = np.sqrt(np.mean(out.astype(np.float64) ** 2))
            if r > 1e-9:
                out = (out / r).astype(T)
        return out


def sideband_score(x: np.ndarray, f0: float) -> float:
    w = np.hanning(len(x))
    X = np.abs(np.fft.rfft(x * w))
    fr = np.fft.rfftfreq(len(x), 1.0 / FS)

    def bin_of(f):
        return int(np.argmin(np.abs(fr - f)))

    def peak_near(f, halfwidth=3):
        b = bin_of(f)
        lo, hi = max(0, b - halfwidth), min(len(X), b + halfwidth + 1)
        return X[lo:hi].max()

    df = FS / len(x)
    guard = max(8.0 * df, 2.0)
    if min(abs(HOP_RATE - m * f0) for m in range(1, 12)) < guard:
        return float("nan")

    worst = -200.0
    for k in range(1, 9):
        fk = k * f0
        if fk + HOP_RATE > FS / 2 - 100:
            break
        carrier = peak_near(fk)
        if carrier <= 0:
            continue
        sb = max(peak_near(fk - HOP_RATE), peak_near(fk + HOP_RATE))
        worst = max(worst, 20.0 * np.log10(sb / carrier + 1e-20))
    return worst


def run(norm: str, f0_hz, n_hops=1000, ramp=True):
    osc = Excitation(norm=norm)
    osc.ramp = ramp
    osc.reset(f0_hz(0) if callable(f0_hz) else f0_hz)
    chunks = []
    for h in range(n_hops):
        f0 = f0_hz(h) if callable(f0_hz) else f0_hz
        chunks.append(osc.hop(f0))
    return np.concatenate(chunks)


def glide_sideband(norm: str, ramp: bool, f0_of_hop, n_hops=1000, seg_hops=50):
    x = run(norm, f0_of_hop, n_hops=n_hops, ramp=ramp)
    worst = -200.0
    for s in range(0, n_hops - seg_hops, seg_hops):
        seg = x[s * HOP:(s + seg_hops) * HOP]
        f_local = float(np.mean([f0_of_hop(h) for h in range(s, s + seg_hops)]))
        sc = sideband_score(seg, f_local)
        if not np.isnan(sc):
            worst = max(worst, sc)
    return worst


if __name__ == "__main__":
    print(f"oscillator continuity: 1000 hops @ {FS:.0f} Hz, hop {HOP} "
          f"({HOP_RATE:.0f} Hz hop rate)\n")
    print(f"{'normalization':<14} {'F0':<10} {'worst 200 Hz sideband':>22}   verdict")
    print("-" * 66)
    for norm in ("analytic", "per_hop"):
        for label, f0 in (("steady 220", 220.0), ("steady 147", 147.0)):
            x = run(norm, f0)
            s = sideband_score(x, f0)
            ok = "PASS (no sidebands)" if s < -60 else "FAIL <-- hop-rate artefact"
            print(f"  {norm:<12} {label:<10} {s:>18.1f} dB   {ok}")
    import math
    vib = lambda h: 150.0 * (2.0 ** (0.5 * math.sin(2 * math.pi * 6.0 * h * HOP / FS) / 12.0))
    print("\n  f0-ramp check -- vibrato +-50 cents @6 Hz around 150 Hz")
    print("  (instantaneous frequency from unwrapped phase; a step at a hop boundary")
    print("   is a slope discontinuity, which is what the ramp exists to remove)")
    for ramp in (True, False):
        osc = Excitation(norm="none")
        osc.ramp = ramp
        osc.reset(vib(0))
        ph = []
        for h in range(400):
            f0 = vib(h)
            if osc.f0_prev <= 0.0:
                osc.f0_prev = f0
            for n in range(HOP):
                w = (n + 1) / HOP if osc.ramp else 1.0
                f = osc.f0_prev + w * (f0 - osc.f0_prev)
                osc.phase = (osc.phase + f / FS) % 1.0
                ph.append(f / FS)
            osc.f0_prev = f0
        fi = np.array(ph) * FS
        d = np.abs(np.diff(fi))
        bnd = d[HOP - 1::HOP]
        inner = np.delete(d, np.arange(HOP - 1, len(d), HOP))
        tag = "f0 ramped across hop" if ramp else "f0 stepped at hop boundary"
        print(f"    {tag:<28} max jump at boundary {bnd.max():6.3f} Hz | "
              f"within hop {inner.max():6.3f} Hz")
    print("    -> ramped: boundary jump == within-hop jump (frequency is C0-continuous).")
    print("       stepped: the whole hop's f0 change lands on ONE sample boundary.")

    print("\n  RMS check (should be ~1.0):")
    for norm in ("analytic", "per_hop"):
        x = run(norm, 220.0)
        print(f"    {norm:<10} overall RMS = {np.sqrt(np.mean(x**2)):.4f}")
