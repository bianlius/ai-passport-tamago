<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# AI Passport 上的 Tama-Go 宠物模拟器

此仓库提供 Tama-Go 模拟器固件的应用、板级代码和构建资料。游戏与人物配件 ROM、宠物存档、设备凭据和固件二进制均未包含；用户通过设备传输页面导入自己的 ROM。

SHA-256 为 `6087de5d7f3e3689ceac87be16bd2363e84c8409acaf32676476042eca862a2d` 的发布镜像由此版本源码以 ESP-IDF 5.5.3 构建。安装 ESP-IDF 5.5.3 后运行 `./tools/validate.sh --firmware`。完整镜像输出为 `build/FoloToy-AI-Passport-full.bin`，刷写偏移为 `0x0`。不同环境的构建标识和哈希可能不同。

玩法及传输方法见[中文说明](docs/tamago.zh_CN.md)或[英文说明](docs/tamago.md)。USB 工具 `TamaGo-USB.html` 可从[百度网盘](https://pan.baidu.com/s/1mR0SY2MC4fA9zxEMfxmO-w?pwd=3dxy)下载，提取码 `3dxy`。

## 许可

`main/t7_core/` 的模拟器核心来自 T7Emu，按 GPL-3.0-or-later 发布，详见其[许可](main/t7_core/LICENSE)和[上游声明](main/t7_core/UPSTREAM-NOTICE.txt)。集成此核心的完整固件按 GPL-3.0-or-later 条款分发。FoloToy 基础代码保留其 [MIT 许可](LICENSE)。生成的中文字体采用 SIL OFL 1.1，Lucide 图标源文件采用 ISC 许可；许可文件位于 `assets/`。此仓库及发布固件均不包含游戏或人物配件 ROM。
