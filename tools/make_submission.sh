#!/bin/bash
# make_submission.sh - build the source archive: firmware, sample app, docs,
# host tools, training code and the deployed network (no build outputs,
# captures or checkpoints)
#
#   tools/make_submission.sh [out_dir]    -> <out_dir>/NeuralAutoTune_uTK_<date>.zip
#
# Copyright 2026 Sher Amir Singh Dullat
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(cd "${1:-$ROOT/..}" && pwd)"
NAME="NeuralAutoTune_uTK_$(date +%Y%m%d)"
STAGE="$(mktemp -d)/$NAME"
mkdir -p "$STAGE"

EXCL=(--exclude '.DS_Store' --exclude '__pycache__' --exclude '.venv' --exclude '*.o' --exclude '*.d'
      --exclude '*.su' --exclude '*.cyclo' --exclude '*-bak')
rs() { rsync -a --prune-empty-dirs "${EXCL[@]}" "$@"; }
files() { (cd "$ROOT" && rsync -aR "$@" "$STAGE/"); }

files README.md LICENSE NOTICE
rs --exclude 'PLAN_*' "$ROOT/docs/" "$STAGE/docs/"

# application image
mkdir -p "$STAGE/AppliNonSecure"
for d in Core natune app mtk3_bsp2 Middlewares X-CUBE-AI; do
  rs --exclude '*.md' "$ROOT/AppliNonSecure/$d" "$STAGE/AppliNonSecure/"
done
rs "$ROOT/AppliNonSecure/natune/README.md" "$STAGE/AppliNonSecure/natune/"
rs --include '*/' --include '*.md' --exclude '*' "$ROOT/AppliNonSecure/mtk3_bsp2" "$STAGE/AppliNonSecure/"
files AppliNonSecure/Makefile AppliNonSecure/STM32N657X0HXQ_LRUN.ld

# secure boot stage: sources and the CubeIDE makefiles
rs --include 'objects.list' --exclude 'Debug/*.elf' --exclude 'Debug/*.map' --exclude 'Debug/*.list' \
   --include '*/' --include '*.c' --include '*.h' --include '*.s' --include '*.ld' \
   --include '*.mk' --include 'makefile' --include 'RSN_FSBL.launch' --exclude '*' \
   "$ROOT/FSBL" "$STAGE/"

rs "$ROOT/Drivers" "$STAGE/"

# host tools
mkdir -p "$STAGE/tools"
rs --exclude 'captures' --exclude '*.log' --exclude '.*.gdb' "$ROOT/tools/board" "$STAGE/tools/"
files tools/make_submission.sh

files tools/rsn_v3/README.md \
      tools/rsn_v3/rsn/__init__.py tools/rsn_v3/rsn/model.py tools/rsn_v3/rsn/dsp.py \
      tools/rsn_v3/rsn/data.py tools/rsn_v3/rsn/prep.py tools/rsn_v3/rsn/train.py \
      tools/rsn_v3/rsn/losses.py tools/rsn_v3/rsn/gan.py tools/rsn_v3/rsn/export.py \
      tools/rsn_v3/rsn/package.py tools/rsn_v3/rsn/calibrate.py tools/rsn_v3/rsn/f0logic.py \
      tools/rsn_v3/rsn/psola.py tools/rsn_v3/excitation/excitation_twin.py \
      tools/rsn_v3/c1/c1_crossval.py tools/rsn_v3/c1/c1_parse.py \
      tools/rsn_v3/swiftf0/swiftf0/__init__.py tools/rsn_v3/swiftf0/swiftf0/frontend.py \
      tools/rsn_v3/swiftf0/swiftf0/decode.py \
      tools/rsn_v3/swiftf0/artifacts/artifacts/swiftf0_float.onnx \
      tools/rsn_v3/swiftf0/artifacts/artifacts/swiftf0_float.onnx.data

files tools/swiftf0/README.md tools/swiftf0/requirements.txt tools/swiftf0/gen_tables.py \
      tools/swiftf0/swiftf0/__init__.py tools/swiftf0/swiftf0/frontend.py tools/swiftf0/swiftf0/decode.py \
      tools/swiftf0/swiftf0/model.py tools/swiftf0/swiftf0/data.py tools/swiftf0/swiftf0/train.py \
      tools/swiftf0/swiftf0/eval.py tools/swiftf0/swiftf0/export.py tools/swiftf0/swiftf0/npu_rewrite.py \
      tools/swiftf0/npu_build/swiftf0_int8_phasegrid.onnx tools/swiftf0/npu_build/swiftf0_int8_phasegrid_Q.json \
      tools/swiftf0/st_ai_ws/stm32n6_rsn.mpool tools/swiftf0/st_ai_ws/stm32n6_sf0.mpool

# the deployed RSN
mkdir -p "$STAGE/deploy"
rs --exclude '*.pt' "$ROOT/deploy/rsn_final_v1" "$STAGE/deploy/"

rm -f "$OUT/$NAME.zip"
(cd "$(dirname "$STAGE")" && zip -qr "$OUT/$NAME.zip" "$NAME")
echo "$OUT/$NAME.zip ($(du -h "$OUT/$NAME.zip" | cut -f1))"
