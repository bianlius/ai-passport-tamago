#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out="$(mktemp /tmp/test-tamago-core.XXXXXX)"
trap 'rm -f -- "$out"' EXIT
"${CC:-cc}" -O2 -std=c11 -DT7_NOMALLOC=1 -DT7_VIDEO_CYCLES=1 \
  -DT7_FASTRUN=1 -DT7_HOT= -DT7_SND_RING=2048 -DT7_SND_SYN_N=64 \
  -DSPI_TRACE_MAX=16 -I"${repo_root}/main" -I"${repo_root}/main/t7_core" \
  "${repo_root}/tests/test_tamago_core.c" \
  "${repo_root}/main/t7_core/t7.c" \
  "${repo_root}/main/t7_core/t7_cpu.c" \
  "${repo_root}/main/t7_core/t7_memory.c" \
  "${repo_root}/main/t7_core/t7_eeprom.c" \
  "${repo_root}/main/t7_core/t7_sound.c" \
  "${repo_root}/main/t7_core/figure_data.c" \
  -lm -o "$out"
"$out" "${repo_root}/main/private_rom/tamago.bin" \
  "${repo_root}/main/private_rom/figures/mametchi.bin" \
  "${repo_root}/main/private_rom/figures/makiko.bin"
