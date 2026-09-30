"""Pitch from the logits: weighted mean around the argmax. swiftf0_decode.c
must match it.

Copyright 2026 Sher Amir Singh Dullat
SPDX-License-Identifier: Apache-2.0
"""

import numpy as np
from .frontend import FMIN, NBINS_OUT

def decode(logits: np.ndarray):
    logits = np.asarray(logits, dtype=np.float64)
    z = logits - logits.max(axis=-1, keepdims=True)
    p = np.exp(z)
    p /= p.sum(axis=-1, keepdims=True)

    amax = p.argmax(axis=-1)
    idx = np.clip(amax[..., None] + np.arange(-2, 3), 0, NBINS_OUT - 1)
    pw = np.take_along_axis(p, idx, axis=-1)
    b_hat = (pw * idx).sum(axis=-1) / pw.sum(axis=-1)

    f0 = FMIN * np.exp2(b_hat / 60.0)
    ent = -(p * np.log(p + 1e-12)).sum(axis=-1) / np.log(NBINS_OUT)
    return f0, b_hat, ent
