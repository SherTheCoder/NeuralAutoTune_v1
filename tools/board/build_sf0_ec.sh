#!/bin/bash
# build_sf0_ec.sh - compile the pitch network for the NPU's epoch controller
# and write its firmware sources (network.c, network_ecblobs.h,
# swiftf0_weights.c) through gen_sf0_fw.py
#
# Epoch by epoch the network held the NPU for up to 2.7 ms; as one blob it
# takes 0.88 ms.
#
# Copyright 2026 Sher Amir Singh Dullat
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ST=/Applications/ST/STEdgeAI/4.0
ATONN=$ST/Utilities/mac/atonn
CFG=$ST/Utilities/configs
ECS=$ST/scripts/epoch_controller
T=$ROOT/tools/swiftf0
O=$T/npu_build_ec
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/neural_art__network"; rm -rf "$O"; mkdir -p "$O"

echo "[1/3] atonn --enable-epoch-controller (SwiftF0)"
(cd "$W" && "$ATONN" -i "$T/npu_build/swiftf0_int8_phasegrid.onnx" \
  --json-quant-file "$T/npu_build/swiftf0_int8_phasegrid_Q.json" -g network.c \
  --load-mdesc "$CFG/stm32n6.mdesc" --load-mpool "$T/st_ai_ws/stm32n6_sf0.mpool" \
  --load-cdesc "$CFG/cortex-m55.cdesc" --out-dir-prefix "$W/neural_art__network/" \
  --network-name network --native-float --cache-maintenance --Ocache-opt \
  --enable-virtual-mem-pools --Os --Oauto-sched --enable-epoch-controller \
  --output-info-file c_info.json --generate-stai > "$W/atonn.log" 2>&1) \
  || { echo "atonn FAILED"; tail -15 "$W/atonn.log"; exit 1; }
grep -E "All epochs mapped on epoch controller" "$W/atonn.log" || { echo "not fully EC-mapped"; tail -15 "$W/atonn.log"; exit 1; }
SW=$(grep -oE "ll_sw_forward_[a-z_]+" "$W/neural_art__network/network.c" | sort -u | tr '\n' ' ' || true)
[ -z "$SW" ] || { echo "GATE FAIL: software epochs: $SW"; exit 1; }

echo "[2/3] host trace -> network_ecblobs.h"
python3 "$ECS/prepare_ec_trace.py" --srcdir "$ECS" --atonh "$ST/Middlewares/ST/AI/Npu/Devices/STM32N6xx" \
  --outdir "$W/ectrace" --work-dir "$W/ectrace_wd" > "$W/prep.log" 2>&1 || { cat "$W/prep.log"; exit 1; }
python3 "$ECS/generate_ec_trace.py" --ectrace "$W/ectrace" \
  --inputfile "$W/neural_art__network/network_blob_trace.c" --workdir "$W/ecwd" > "$W/trace.log" 2>&1 || { cat "$W/trace.log"; exit 1; }

echo "[3/3] collect + firmware sources"
for f in network.c atonbuf.AXISRAM3.raw c_info.json stai_network.c stai_network.h; do cp "$W/neural_art__network/$f" "$O/"; done
cp "$W/ecwd/network_ecblobs.h" "$W/atonn.log" "$O/"
python3 "$ROOT/tools/board/gen_sf0_fw.py" --gen "$O"
