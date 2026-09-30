#!/bin/bash
# build_rsn_ec.sh - compile a packaged RSN for the NPU's epoch controller
#
#   tools/board/build_rsn_ec.sh deploy/rsn_final_v1   -> deploy/rsn_final_v1/npu_gen_ec/
#
# Run epoch by epoch from the CPU the RSN takes 3.7 ms; as one epoch-controller
# blob it takes 2.9 ms. The blob is produced the way stedgeai does it: atonn
# writes rsn_blob_trace.c, which is built and run on the host.
#
# Then: python3 tools/board/gen_rsn_fw.py --deploy <pkg> --gen <pkg>/npu_gen_ec
#
# Copyright 2026 Sher Amir Singh Dullat
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
PKG=$(cd "$1" && pwd)
# OUT_NAME and ATONN_OPT can be overridden
OUT_NAME=${OUT_NAME:-npu_gen_ec}
ATONN_OPT=${ATONN_OPT:---Os --Oauto-sched}
ST=/Applications/ST/STEdgeAI/4.0
ATONN=$ST/Utilities/mac/atonn
CFG=$ST/Utilities/configs
ECS=$ST/scripts/epoch_controller
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MPOOL=$ROOT/tools/swiftf0/st_ai_ws/stm32n6_rsn.mpool
G=$PKG/npu_gen
O=$PKG/$OUT_NAME
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

[ -f "$G/rsn_pg.onnx" ] && [ -f "$G/rsn_pg_Q.json" ] || { echo "no phase-grid graph in $G"; exit 1; }
rm -rf "$O"; mkdir -p "$W/neural_art__rsn" "$O"

echo "[1/3] atonn --enable-epoch-controller"
(cd "$W" && "$ATONN" -i "$G/rsn_pg.onnx" --json-quant-file "$G/rsn_pg_Q.json" -g rsn.c \
  --load-mdesc "$CFG/stm32n6.mdesc" --load-mpool "$MPOOL" --load-cdesc "$CFG/cortex-m55.cdesc" \
  --out-dir-prefix "$W/neural_art__rsn/" --network-name rsn --native-float --cache-maintenance \
  --Ocache-opt --enable-virtual-mem-pools $ATONN_OPT --enable-epoch-controller \
  --output-info-file c_info.json --generate-stai > "$W/atonn.log" 2>&1) \
  || { echo "atonn FAILED"; tail -15 "$W/atonn.log"; exit 1; }
grep -E "All epochs mapped on epoch controller" "$W/atonn.log" || { echo "not fully EC-mapped"; tail -15 "$W/atonn.log"; exit 1; }
SW=$(grep -oE "ll_sw_forward_[a-z_]+" "$W/neural_art__rsn/rsn.c" | sort -u | tr '\n' ' ' || true)
[ -z "$SW" ] || { echo "GATE FAIL: software epochs: $SW"; exit 1; }

echo "[2/3] host trace -> rsn_ecblobs.h"
python3 "$ECS/prepare_ec_trace.py" --srcdir "$ECS" --atonh "$ST/Middlewares/ST/AI/Npu/Devices/STM32N6xx" \
  --outdir "$W/ectrace" --work-dir "$W/ectrace_wd" > "$W/prep.log" 2>&1 || { cat "$W/prep.log"; exit 1; }
python3 "$ECS/generate_ec_trace.py" --ectrace "$W/ectrace" \
  --inputfile "$W/neural_art__rsn/rsn_blob_trace.c" --workdir "$W/ecwd" > "$W/trace.log" 2>&1 || { cat "$W/trace.log"; exit 1; }

echo "[3/3] collect"
for f in rsn.c atonbuf.AXISRAM4.raw c_info.json stai_rsn.c stai_rsn.h; do cp "$W/neural_art__rsn/$f" "$O/"; done
cp "$W/ecwd/rsn_ecblobs.h" "$O/"
cp "$W/atonn.log" "$O/atonn.log"
EST=$(grep -oE "estimated_(npu|tot)_cycles = [0-9]+" "$O/rsn.c" | tr '\n' ' ')
echo "    $(grep -oE '_ec_blob_rsn_[0-9]+\[[0-9]+\]' "$O/rsn_ecblobs.h" | tr '\n' ' ')| weights $(wc -c < "$O/atonbuf.AXISRAM4.raw") B | $EST"
echo "$ATONN_OPT" > "$O/atonn_opt.txt"
echo "    -> $O"
