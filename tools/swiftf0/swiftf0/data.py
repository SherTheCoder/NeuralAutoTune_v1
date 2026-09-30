"""Frame datasets from MIR-1K and NUS-48E.

    python -m swiftf0.data --mir1k <dir> --nus48e <dir> --out <cache>

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

import json
import sys
from pathlib import Path

import numpy as np

from .frontend import NFFT, SR, f0_to_bin, features

CACHE_VERSION = 1
HOP_S = 0.020
F0_LO, F0_HI = 50.0, 1600.0
UNVOICED_KEEP = 3


def _frames_from_track(wave16k: np.ndarray, t: np.ndarray, f0: np.ndarray,
                       voiced: np.ndarray):
    w = np.clip(wave16k, -1.0, 1.0)
    wave_i16 = (w * 32767.0).astype(np.int16)
    half = NFFT // 2
    ws, bs, vs = [], [], []
    n_unv = 0
    for ti, fi, vi in zip(t, f0, voiced):
        c = int(round(ti * SR))
        if c - half < 0 or c + half > len(wave_i16):
            continue
        if vi:
            if not (F0_LO <= fi <= F0_HI):
                continue
        else:
            n_unv += 1
            if n_unv % UNVOICED_KEEP:
                continue
        ws.append(wave_i16[c - half:c + half])
        bs.append(f0_to_bin(fi) if vi else np.nan)
        vs.append(1 if vi else 0)
    if not ws:
        return None
    return (np.stack(ws), np.asarray(bs, np.float32),
            np.asarray(vs, np.uint8))

def _resample_to_labels(t_lab, f0_lab, voiced_lab):
    t = np.arange(t_lab[0], t_lab[-1], HOP_S)
    f0 = np.interp(t, t_lab, f0_lab)
    vi = np.interp(t, t_lab, voiced_lab.astype(np.float64)) > 0.75
    return t, f0, vi

def prepare_mir1k(root: Path, out: Path):
    import soundfile as sf
    wavs = sorted((root / "Wavs").glob("*.wav"))
    if not wavs:
        sys.exit(f"MIR-1K: no wavs under {root}/Wavs")
    print(f"MIR-1K: {len(wavs)} clips")
    for i, wp in enumerate(wavs):
        pv = root / "PitchLabel" / (wp.stem + ".pv")
        if not pv.exists():
            continue
        audio, sr = sf.read(wp, dtype="float64")
        voice = audio[:, 1] if audio.ndim == 2 else audio
        if sr != SR:
            import librosa
            voice = librosa.resample(voice, orig_sr=sr, target_sr=SR)
        semitones = np.loadtxt(pv)
        dur = len(voice) / SR
        hop = dur / (len(semitones) + 1)
        if not (0.015 < hop < 0.025):
            print(f"  ! {wp.stem}: inferred label hop {hop*1000:.1f} ms, "
                  f"clamping to 20 ms")
            hop = 0.020
        t_lab = hop + hop * np.arange(len(semitones))
        voiced = semitones > 0.1
        f0_lab = 440.0 * np.exp2((semitones - 69.0) / 12.0)
        f0_lab[~voiced] = 0.0
        t, f0, vi = _resample_to_labels(t_lab, f0_lab, voiced)
        fr = _frames_from_track(voice, t, f0, vi)
        if fr is None:
            continue
        singer = wp.stem.split("_")[0]
        np.savez_compressed(out / f"mir1k_{wp.stem}.npz", version=CACHE_VERSION,
                            wave=fr[0], bin=fr[1], voiced=fr[2], singer=singer)
        if (i + 1) % 100 == 0:
            print(f"  {i + 1}/{len(wavs)}")

def prepare_nus48e(root: Path, out: Path):
    import librosa
    wavs = sorted(root.glob("*/sing/*.wav"))
    if not wavs:
        sys.exit(f"NUS-48E: no */sing/*.wav under {root}")
    print(f"NUS-48E: {len(wavs)} sung tracks (pyin pseudo-labels; slowest step)")
    for i, wp in enumerate(wavs):
        singer = wp.parts[-3]
        voice, _ = librosa.load(wp, sr=SR, mono=True)
        f0_lab, vflag, vprob = librosa.pyin(
            voice, fmin=60.0, fmax=1000.0, sr=SR,
            frame_length=NFFT, hop_length=320, center=True)
        t_lab = 320 / SR * np.arange(len(f0_lab))
        voiced = vflag & (vprob > 0.5)
        f0_lab = np.where(voiced, np.nan_to_num(f0_lab), 0.0)
        t, f0, vi = _resample_to_labels(t_lab, f0_lab, voiced)
        fr = _frames_from_track(voice, t, f0, vi)
        if fr is None:
            continue
        np.savez_compressed(out / f"nus_{singer}_{wp.stem}.npz",
                            version=CACHE_VERSION, wave=fr[0], bin=fr[1],
                            voiced=fr[2], singer=singer)
        print(f"  {i + 1}/{len(wavs)}  {singer}/{wp.stem}: {len(fr[0])} frames")

def build_index(out: Path):
    idx = []
    for p in sorted(out.glob("*.npz")):
        z = np.load(p, allow_pickle=True)
        idx.append({"file": p.name, "singer": str(z["singer"]),
                    "n": int(len(z["voiced"])),
                    "voiced": int(z["voiced"].sum())})
    (out / "index.json").write_text(json.dumps(idx, indent=1))
    nv = sum(e["voiced"] for e in idx)
    nt = sum(e["n"] for e in idx)
    print(f"cache: {len(idx)} tracks, {nt} frames ({nv} voiced, "
          f"{100 * nv / max(nt, 1):.0f}%)")


class FrameDataset:

    ROLL = 10

    def __init__(self, cache: Path, singers=None, exclude=None,
                 augment=True, seed=0):
        entries = json.loads((Path(cache) / "index.json").read_text())
        if singers is not None:
            entries = [e for e in entries if e["singer"] in singers]
        if exclude is not None:
            entries = [e for e in entries if e["singer"] not in exclude]
        waves, bins, voiced = [], [], []
        for e in entries:
            z = np.load(Path(cache) / e["file"])
            waves.append(z["wave"]); bins.append(z["bin"])
            voiced.append(z["voiced"])
        self.wave = np.concatenate(waves)
        self.bin = np.concatenate(bins)
        self.voiced = np.concatenate(voiced)
        self.augment = augment
        self.rng = np.random.default_rng(seed)
        self.singers = sorted({e["singer"] for e in entries})

    def __len__(self):
        return len(self.wave)

    def _noise(self, x):
        snr_db = self.rng.uniform(20.0, 40.0)
        n = self.rng.standard_normal(len(x)).astype(np.float32)
        if self.rng.random() < 0.5:
            spec = np.fft.rfft(n)
            spec[1:] /= np.sqrt(np.arange(1, len(spec)))
            n = np.fft.irfft(spec, len(x)).astype(np.float32)
        sig_p = float(np.mean(x * x)) + 1e-12
        n *= np.sqrt(sig_p / (10 ** (snr_db / 10)) / (np.mean(n * n) + 1e-12))
        return x + n

    def __getitem__(self, i):
        x = self.wave[i].astype(np.float32) / 32768.0
        if self.augment and self.rng.random() < 0.5:
            x = self._noise(x)
        feat = features((np.clip(x, -1, 1) * 32767).astype(np.int16))
        b = float(self.bin[i]) if self.voiced[i] else -1.0
        if self.augment:
            r = int(self.rng.integers(-self.ROLL, self.ROLL + 1))
            if r:
                feat = np.roll(feat, r)
                if r > 0:
                    feat[:r] = -1.0
                else:
                    feat[r:] = -1.0
                if b >= 0:
                    b += r
                    if not (0 <= b <= 359):
                        b = -1.0
        return feat, np.float32(b), np.float32(self.voiced[i])

def build_representative(cache: Path, out_npy: Path, n=500, seed=1):
    ds = FrameDataset(cache, augment=False, seed=seed)
    voiced_idx = np.flatnonzero(ds.voiced)
    pick = np.random.default_rng(seed).choice(voiced_idx, n, replace=False)
    feats = np.stack([ds[i][0] for i in pick])
    np.save(out_npy, feats.astype(np.float32))
    print(f"representative set: {feats.shape} -> {out_npy}")


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(prog="swiftf0.data")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("prepare")
    p.add_argument("--mir1k", type=Path, help="MIR-1K root (contains Wavs/)")
    p.add_argument("--nus48e", type=Path, help="NUS-48E root (singer dirs)")
    p.add_argument("--out", type=Path, required=True)
    r = sub.add_parser("representative")
    r.add_argument("--cache", type=Path, required=True)
    r.add_argument("--out", type=Path, required=True)
    a = ap.parse_args(argv)
    if a.cmd == "prepare":
        a.out.mkdir(parents=True, exist_ok=True)
        if a.mir1k:
            prepare_mir1k(a.mir1k, a.out)
        if a.nus48e:
            prepare_nus48e(a.nus48e, a.out)
        build_index(a.out)
    else:
        build_representative(a.cache, a.out)

if __name__ == "__main__":
    main(sys.argv[1:])
