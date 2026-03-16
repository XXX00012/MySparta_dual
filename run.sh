#!/bin/bash
set -e

export XILINX_XRT=/usr

XCLBIN=${1:-svd.xclbin}
ITER=${2:-2}
INPUT_PREFIX=${3:-./data/hdiff}
OUTPUT_TXT=${4:-./data/aie_out_gmio.txt}

if [ ! -f "$XCLBIN" ] && [ -f "a.xclbin" ]; then
  XCLBIN="a.xclbin"
fi

echo "[run.sh] XCLBIN       = $XCLBIN"
echo "[run.sh] ITER         = $ITER"
echo "[run.sh] INPUT_PREFIX = $INPUT_PREFIX"
echo "[run.sh] OUTPUT_TXT   = $OUTPUT_TXT"

./host.exe "$XCLBIN" "$ITER" "$INPUT_PREFIX" "$OUTPUT_TXT"