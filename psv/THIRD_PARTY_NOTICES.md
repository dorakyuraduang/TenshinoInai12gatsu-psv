# Tenshi Vita 第三方说明

本 PSV 移植的原创代码采用根目录 MIT 许可证。游戏剧本、图片和原始档案由玩家提供。0.20 按用户要求将原片头转为 H.264/AAC，并内置 VPK；片头不适用项目 MIT 或字体 OFL 许可，其来源及哈希见 `assets/opening.json`。其余原始档案不进入 VPK。0.16 起另行内置 Adobe 思源黑体，字体按 SIL OFL 1.1 分发，不适用项目 MIT 许可。

## 代码与静态库

- nlohmann/json 3.11.3：MIT，许可在 `licenses/nlohmann-json-MIT.txt`；源代码随 `src/json.hpp` 提供。
- ReAvPlayer：MIT，Copyright (c) 2021 Jaylon Gowie；`vita_hw_codec.hpp` 的分离送帧／取帧调用参考其 `abff24a21ffec3e57fc1cd3b1d17dc26666251b0` 实现，许可在 `licenses/ReAvPlayer-MIT.txt`。
- PL_MPEG：MIT，作者 Dominic Szablewski；许可在 `licenses/pl_mpeg-MIT.txt`，完整解码器在 `src/pl_mpeg.h`。
- SDL2、SDL2_image、SDL2_ttf、SDL2_mixer：zlib 许可，分别保留在 `licenses/SDL2*-LICENSE.txt`。
- FreeType：本构建选择 FreeType License（FTL），见 `licenses/FreeType-FTL.txt`。Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved.
- HarfBuzz、libpng、zlib、libjpeg-turbo / IJG、libwebp、libogg、libvorbis、Opus、opusfile、libxmp-lite、libmodplug、bzip2：各自的许可与必要声明保留在 `licenses/`，来源见该目录的 `sources.json`。libmodplug 的上游声明为公有领域。
- VitaSDK pthread-embedded：整体受 LGPL 约束，Vita 专用补丁为 MIT。保留 `pthread-embedded-COPYING.txt`、`pthread-LGPL-2.1.txt`、`pthread-vita-MIT.txt`。本 SDK 使用 commit `63e1cd9152082d9ccb1b38d67a4caf975562fbeb`，完整对应源码随 `third_party/pthread-embedded-63e1cd9.zip` 提供。
- 运行时和开发验证程序复用了主工程的 GARbro 衍生 PX 解码逻辑；相关 MIT 版权全文在根目录 `THIRD_PARTY_NOTICES.md`，并复制于本目录的 `licenses/GARbro-MIT.txt`。

## 重新构建与分发

`tools/dependencies.lock.json` 记录本次实际使用的 SDK 和库包下载地址及 SHA-256。`tools/build-vita.ps1`、`CMakeLists.txt` 和全部本项目源代码可用于重新构建、修改和重新链接程序。pthread 对应源码包内的 `platform/vita/Makefile` 提供该库的 Vita 构建规则；可用自己的构建结果替换 SDK 中的 `libpthread.a` 后重新链接本程序。保留所有源文件中的版权注释。

分发 VPK 时请同时提供发布包中的 `tenshi-vita-source.zip`（包含本项目源码、构建脚本、解码器和 pthread 对应源码）及本说明，不要只转发二进制而丢弃源码资料。不得限制许可证允许的修改、重新链接及为调试修改而进行的逆向工程。

本 PSV 程序不链接 Godot 主工程的 FFmpeg / Original Video 二进制。0.26 PSV 运行时使用系统 SceVideodec/SceCodecEngine 和 SceAudiodec 直接解码 H.264/AAC，SDL 负责画面与音频输出；PL_MPEG 仅用于桌面原视频参考验证。FFmpeg/libx264 作为开发机的离线转码工具，未链接或打包到 PSV 程序。源码 ZIP 不附带游戏片头文件，可使用自己的对应原文件和 `tools/transcode-opening.py` 重建，或从此 VPK 提取。逐帧索引由 `tools/index-opening.py` 使用 FFprobe 重建，转码脚本会自动调用它。

用户指定的 `spyro-98/vita-hw-decoder`（固定提交 `12f3fff5aaf7145ee3839318bd1e8e2b6840197c`）及其 wiliwili 后端用于核对硬解 API、内存对齐与 NV12 布局。本项目使用针对固定片头编写的独立读取／调度实现；未复制或链接该 GPL 播放器库与其 FFmpeg 二进制，也未打包其截图。技术来源和验证边界见 `VIDEO_LOGO_FIXES_0.22.md`。

## 内置字体

Source Han Sans CN Regular（思源黑体简体常规字重），Copyright 2014–2025 Adobe，SIL Open Font License 1.1。使用未经本项目修改的官方版本；完整版权、保留名称和许可见 `licenses/SourceHanSans-OFL.txt`，固定上游提交、文件链接及哈希见 `assets/source-han-sans.json`。许可允许随软件分发，字体不得单独销售。旧 SimHei 未加入发行包；详见 `FONT_LICENSE.md`。

0.23 内存池兼容性核对 SDL 2.32.8 的 `SDL_render_vita_gxm_memory.c` 及 wiliwili 补丁的 PHYCONT 分配模式；未修改所链接的 SDL 库。具体证据见 `VIDEO_LOGO_FIXES_0.23.md`。
