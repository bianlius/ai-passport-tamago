#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd -- "$repo_dir"
python_bin="$repo_dir/../_toolchain/espressif/python_env/idf5.5_py3.9_env/bin/python"
if [ ! -x "$python_bin" ]; then
    python_bin=python3
fi
exec "$python_bin" tools/tamago_usb_transfer.py
