# Listening examples

Rendered through the INT8 graph one hop at a time. For each clip:
`_source` is the singer, `_target` the LP-PSOLA pitch shift the network was
trained towards, and `_out` the RSN output (what the board plays). rho is
the correlation between output and target.

## general
| file | F0 (Hz) | shift (st) | rho |
|---|---|---|---|
| general_00_*.wav | 147 | +3.00 | 0.941 |
| general_01_*.wav | 294 | -2.00 | 0.852 |
| general_02_*.wav | 502 | -1.22 | 0.590 |
| general_03_*.wav | 129 | +0.00 | 0.800 |
| general_04_*.wav | 318 | -2.00 | 0.464 |
| general_05_*.wav | 440 | -1.00 | 0.863 |

## sustained
| file | F0 (Hz) | shift (st) | rho |
|---|---|---|---|
| sustained_00_*.wav | 147 | +3.00 | 0.941 |
| sustained_01_*.wav | 117 | -1.00 | 0.755 |
| sustained_02_*.wav | 129 | +0.00 | 0.800 |
| sustained_03_*.wav | 307 | +2.30 | 0.920 |
| sustained_04_*.wav | 278 | +0.00 | 0.952 |

## breathy
| file | F0 (Hz) | shift (st) | rho |
|---|---|---|---|
| breathy_00_*.wav | 185 | -1.00 | 0.938 |
| breathy_01_*.wav | 159 | -0.72 | 0.908 |
| breathy_02_*.wav | 185 | +0.00 | 0.866 |
| breathy_03_*.wav | 196 | +1.00 | 0.940 |
| breathy_04_*.wav | 393 | +1.00 | 0.852 |
| breathy_05_*.wav | 388 | +1.57 | 0.654 |

## synthetic vowels (for checking hop-rate buzz)
probe_sustain_*_out.wav
