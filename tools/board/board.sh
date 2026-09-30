#!/bin/bash
# board.sh - build, load and run on the STM32N6570-DK (DEV boot mode, both
# images are loaded into RAM) and log the console at 2 Mbaud.
#
#   board.sh run [SECONDS]    build, load, run, log (default 20 s)
#   board.sh load             build, load, run
#   board.sh log [SECONDS]    log the console
#   board.sh build
#   board.sh where [gdb cmds] attach without reset and print a backtrace
#
# blobs/*.bin are written to the address in the matching .addr file once the
# app reaches main(), after the FSBL has powered the NPU RAM. This is how the
# RSN weights get to AXISRAM4; they are too big for the app image.
#
# Copyright 2026 Sher Amir Singh Dullat
# SPDX-License-Identifier: Apache-2.0
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PLUG=/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins
TC=$(ls -d $PLUG/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin | tail -1)
GDBS=$(ls -d $PLUG/com.st.stm32cube.ide.mcu.externaltools.stlink-gdb-server.*/tools/bin | tail -1)/ST-LINK_gdbserver
CP=$(ls -d $PLUG/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.*/tools/bin | tail -1)
export PATH="$TC:$PATH"
APP="$ROOT/AppliNonSecure/build/NeuralAutoTune_uTK.elf"
FSBL="$ROOT/FSBL/Debug/RSN_FSBL.elf"
PORT=61234
TTY=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
LOG="${BOARD_LOG:-$HERE/last_console.log}"

build() {
  # never load a stale ELF after a failed build
  local out rc
  out=$(cd "$ROOT/AppliNonSecure" && make -j10 all 2>&1); rc=$?
  echo "$out" | grep -E "error|warning: implicit|region .* overflowed" | grep -v "^make" || true
  [ $rc -eq 0 ] || { echo "APP BUILD FAILED (make rc=$rc) -- not loading"; exit 1; }
  (cd "$ROOT/FSBL/Debug" && make -j8 all >/dev/null 2>&1) || { echo "FSBL build failed"; exit 1; }
  arm-none-eabi-size "$APP" | tail -1
}

log_console() {  # $1 seconds
  python3 - "$TTY" "$1" "$LOG" <<'PY'
import sys, time, serial
tty, secs, path = sys.argv[1], float(sys.argv[2]), sys.argv[3]
s = serial.Serial(tty, 2000000, timeout=0.2)
t0 = time.time(); out = open(path, "wb")
while time.time() - t0 < secs:
    b = s.read(4096)
    if b:
        out.write(b); out.flush()
out.close()
PY
}

load() {
  # AP 1 is the Cortex-M55
  pkill -f "ST-LINK_gdbserver -p $PORT" 2>/dev/null; sleep 0.5
  "$GDBS" -p $PORT -d -m 1 -k -cp "$CP" -l 1 > "$HERE/gdbserver.log" 2>&1 &
  GS=$!
  for i in $(seq 1 40); do grep -q "Waiting for debugger connection" "$HERE/gdbserver.log" 2>/dev/null && break; sleep 0.25; done
  CMDS="$HERE/.load.gdb"
  {
    echo "set pagination off"
    echo "set confirm off"
    echo "target extended-remote :$PORT"
    echo "load $APP"
    echo "load $FSBL"
    echo "file $FSBL"
    blobs=$(ls "$HERE"/blobs/*.bin 2>/dev/null || true)
    if [ -n "$blobs" ]; then
      # stop at the app's main (by address, the FSBL has a main too); by then
      # the FSBL has powered the NPU RAM
      amain=$(arm-none-eabi-nm "$APP" | awk '$3=="main"{print "0x"$1}')
      echo "tbreak *$amain"
      echo "continue"
      for b in $blobs; do
        a=$(cat "${b%.bin}.addr")
        echo "restore $b binary $a"
        echo "echo restored $(basename $b) -> $a\\n"
      done
    fi
    echo "detach"
    echo "quit"
  } > "$CMDS"
  arm-none-eabi-gdb -batch -x "$CMDS" "$FSBL" > "$HERE/gdb.log" 2>&1
  rc=$?
  sleep 0.3; kill $GS 2>/dev/null
  grep -E "Loading section|restored|Error|error|Transfer rate" "$HERE/gdb.log" | tail -8
  return $rc
}

where() {  # extra gdb commands can be passed
  pkill -f "ST-LINK_gdbserver -p $PORT" 2>/dev/null; sleep 0.5
  "$GDBS" -p $PORT -d -m 1 -cp "$CP" -l 1 --attach > "$HERE/gdbserver.log" 2>&1 &
  GS=$!
  for i in $(seq 1 40); do grep -q "Waiting for debugger connection" "$HERE/gdbserver.log" 2>/dev/null && break; sleep 0.25; done
  CMDS="$HERE/.where.gdb"
  {
    echo "set pagination off"; echo "set confirm off"
    echo "target extended-remote :$PORT"
    echo "info registers pc lr sp xpsr"
    echo "bt 12"
    for c in "$@"; do echo "$c"; done
    echo "detach"; echo "quit"
  } > "$CMDS"
  arm-none-eabi-gdb -batch -x "$CMDS" "$APP" 2>&1 | grep -v "^warning\|^Reading\|^$"
  sleep 0.3; kill $GS 2>/dev/null
}

case "${1:-run}" in
  build) build ;;
  load)  build && load ;;
  log)   log_console "${2:-20}"; cat "$LOG" | tr -d '\r' ;;
  where) shift; where "$@" ;;
  run)
    build || exit 1
    log_console "${2:-20}" &
    LP=$!
    sleep 0.5
    load
    wait $LP
    echo "---- console ($LOG) ----"
    tr -d '\r' < "$LOG"
    ;;
esac
