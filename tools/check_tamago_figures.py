#!/usr/bin/env python3
"""Optional local check for Figure dumps before uploading them to a device."""
from pathlib import Path

root = Path(__file__).resolve().parents[1] / "main/private_rom/figures"
small = ("mametchi", "memetchi", "kuchipatchi", "chamametchi",
         "violetchi", "ringotchi")
large = ("kuromametchi", "sebiretchi", "ponytchi", "ichigotchi",
         "shimashimatchi", "makiko")
for slot, name in enumerate(small + large, 1):
    path = root / f"{name}.bin"
    data = path.read_bytes()
    expected = 349536 if name in small else 524288
    if (len(data) != expected or data[1:3] != b"\xaa\x55" or
        data[17:19] != b"\xaa\x55" or data[19] != 3 or
        data[20] != slot + 2 or data[21] != 3 or
        data[3] != (8 if slot <= 6 else 4)):
        raise SystemExit(f"Invalid Tama-Go figure ROM: {path}")
print("12 Tama-Go figure ROM sizes and internal IDs: PASS")
