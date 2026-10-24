<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Tama-Go Pet Simulator for AI Passport

This repository contains the complete application and board source for the Tama-Go simulator firmware. Game and figure ROMs, pet saves, device credentials, and firmware binaries are not included. Users import their own ROMs through the device transfer page.

The release image with SHA-256 `6087de5d7f3e3689ceac87be16bd2363e84c8409acaf32676476042eca862a2d` was built from this source revision with ESP-IDF 5.5.3. To build, install ESP-IDF 5.5.3 and run `./tools/validate.sh --firmware`. The merged image is generated as `build/FoloToy-AI-Passport-full.bin` for flashing at offset `0x0`. Build IDs and hashes may differ between environments.

For gameplay, import and save transfer instructions, see [English](docs/tamago.md) or [简体中文](docs/tamago.zh_CN.md). For USB transfer, download `TamaGo-USB.html` from [Baidu Netdisk](https://pan.baidu.com/s/1mR0SY2MC4fA9zxEMfxmO-w?pwd=3dxy), extraction code `3dxy`.

## Licenses

The emulator core under `main/t7_core/` comes from T7Emu and is GPL-3.0-or-later; see its [license](main/t7_core/LICENSE) and [upstream notice](main/t7_core/UPSTREAM-NOTICE.txt). The combined firmware includes this core and is distributed under GPL-3.0-or-later terms. The FoloToy baseline has its MIT [license](LICENSE). The generated Chinese font uses SIL OFL 1.1 and the Lucide icon sources use ISC; see their license files under `assets/`. No game or figure ROM is part of this repository or the published firmware.
