# Tenshi Vita · 原始资源直读版

基于[Godot / C# 游戏](https://github.com/dorakyuraduang/TenshinoInai12gatsu-godot)移植的独立 C++ / SDL2 PSV 版本，PSV 上无需 Godot 或 .NET。当前为 **0.27 测试版**：按用户要求内置 H.264/AAC 片头，接入 SceVideodec/SceAudiodec 直接硬解；Logo 改为 GPU 合成。实机日志已确认 Logo 稳定在约 60 帧；新片头硬解路径仍需实机复验。保留 0.17 的行为差异修复、内置思源黑体及项目图标。

## 安装

玩家完整安装与操作步骤见 [INSTALL.md](INSTALL.md)。GitHub 发布页见 [Tenshi Vita Releases](https://github.com/dorakyuraduang/TenshinoInai12gatsu-psv/releases/tag/v0.27)。

安装包固定为 `dist/tenshi-vita.vpk`，后续构建直接更新同名文件，不再额外生成带版本号的 VPK。当前包内版本为 `00.27`，升级时内部版本号仍正常更新。

1. 安装 `dist/tenshi-vita.vpk`，应用 ID `TNSH00001`，版本 `00.27`。
2. 将下列原始资源放到 `ux0:/data/tenshi/`，无需解包。片头和中文字体已内置，不必再放 `openning.v` 或 `font.ttf`。

```text
ux0:/data/tenshi/
  tenshi_dvd.a
  sys.a
  egbg.a
  char.a
  voice.a
  music.a
  se.a
  ed.a
```

在 Windows Vita3K 中，这对应其配置 `pref-path` 下的 `ux0/data/tenshi/`，不是 `ux0/app/TNSH00001/`。VPK 包含程序、图标、OFL 思源黑体、许可及用户要求内置的片头；其余原始游戏资源自行提供。此版本验证的是本项目使用的 GB18030 资源版本。

## 0.27 音频加载与预读

针对实机切图、加载资源时的 BGM 卡顿与爆音，语音改由独立的 `libvorbisfile` 路径在后台线程读盘、修复归档 Ogg 并解码为 PCM，再交给 SDL_mixer 播放，不再使用 SDL_mixer 加载 Ogg 时持有的全段音频锁。

音乐由后台线程预读：第一页常驻，另有两页滚动缓存和一页 staging 缓冲，共 4 MiB。音频回调只从缓存复制数据，不直接读盘；慢盘缺页和读取错误会在 `ux0:/data/tenshi/runtime.log` 中记录 `Audio: music ... cache underrun`、`read errors` 及缓冲大小。新曲预填期间旧曲继续播放，随后抓取旧曲的 0.3 秒片段进行接续。

本轮以主机数值检查与 VitaSDK 原生编译为验证范围，实机听感、慢盘表现与完整游玩仍待复验。具体结果见 [VALIDATION.md](VALIDATION.md)。本轮没有改变片头视频的设备验收边界；0.26 的确认文字和此前 UI 修复继续包含。

## 0.26 覆盖存档确认文字

覆盖安装 `dist/tenshi-vita.vpk`。“覆盖这个存档？”等确认弹窗此前单独使用深灰色，未受 0.24 通用文字颜色修改影响；本版移除该覆盖，统一使用对白相同的纯白色。姓名输入区仍使用深色文字。保留 0.25 移除保存成功横幅及此前的 UI、视频修复。

## 0.25 移除保存成功横幅（已包含）

覆盖安装 `dist/tenshi-vita.vpk`。保存成功后不再显示顶部“已保存至槽位”横幅，存档槽内容正常刷新；保存成功也会清除之前残留的提示。空槽、坏存档等错误提示仍保留。包含之前的文字、焦点和视频内存修复。

## 0.24 文字颜色与焦点提示（已包含）

覆盖安装 `dist/tenshi-vita.vpk`。角色名、选项、存档文字、设置字体名称、历史页码和提示文字统一采用与对白相同的纯白色。姓名输入区保留深色文字；确认弹窗的遗漏已在 0.26 修正。

去掉按钮、滑条和姓名输入框的额外白色边框。方向键仍可导航：有原始聚焦图片的按钮使用该图片，其余控件用左侧小箭头提示当前位置；点击或触摸后不保留箭头。姓名输入以文字末尾光标提示编辑状态。详见 [0.24 UI 修复记录](UI_FIXES_0.24.md)。

本版包含 0.23 的视频内存池修复，视频数据及解码流程不变。

## 0.23 片头解码内存修复（已包含）

覆盖安装 `dist/tenshi-vita.vpk`。内置 MP4 保持 H.264 Main Level 3.1、800×600、30 fps、约 89.84 秒，AAC-LC 44.1 kHz 立体声。无需额外复制视频或插件。

0.22 实机日志已定位到 `avc-context / 0x80024309`：SDL GXM 在首张纹理创建时预留剩余 CDRAM，随后解码器从同一个池申请失败。0.23 将 AVC 上下文、参考帧、NV12 输出改用独立的 PHYCONT 连续主内存，按实机查询值共分配 13 MiB，并记录分配前后的可用内存。主机测试在 CDRAM 剩余为零时先重现旧错误，再验证修复后的全片流程；实机出帧仍需复验。详见 [0.23 内存修复记录](VIDEO_LOGO_FIXES_0.23.md)。

0.21 实机已收到 READY/PLAY，但 15 秒内没有任何视频或音频输出。0.22 参考用户提供的 vita-hw-decoder 及其底层实现，改为用 SceVideodec 直接送入 AVC 数据、用 SceAudiodec 解 AAC，每个初始化和解码阶段分别检查结果。SDL 按 NV12 绘制 800×608 缓冲的可见 800×600 部分。131 KB 的 `opening.idx` 提供同一 MP4 的逐帧位置和时间，不重新编码视频。

音频在第一帧和预缓冲 PCM 都准备好后开始；播放时间由已提交给 SDL 的音频采样数推进，裁掉 AAC 编码前置延迟与末尾补齐。暂停冻结时钟，跳过/退出解除音频回调后释放解码器和内存。无帧会显示具体阶段错误。

全片索引读取与原 MP4 的 2,695 帧像素哈希、3,961,724 个立体声 PCM 采样帧一致；全长调度及 20 种底层失败替身检查通过。这些主机结果不代表真机硬解验收。实机仍需确认首帧、声音与完整播放，日志路径 `ux0:/data/tenshi/runtime.log`，新记录以 `HW` 开头。详见 [0.22 修复与验证记录](VIDEO_LOGO_FIXES_0.22.md)。

Logo 沿用 GPU 合成；此前用户实机连续九段日志为 59.9–60.0 fps，逐帧纹理上传为 0。

## 0.17 差异修复

安装 `dist/tenshi-vita.vpk`，原资源与存档目录沿用。读档恢复当前页逐字显示与已读判定；菜单、自动/快进、语音继续、BGM/音效渐弱、滑条拖动、按钮按下、返回路径与片头后台暂停恢复已修正。新存档以最后对白为锚点，旧版对白存档可恢复消息标识。默认速度/音量只作用于新配置，不覆盖已有设置。

本轮 41 项定向检查、72 个输入事件、26 个界面及三条参考路线通过。详细修复范围、测试证据和剩余边界见 [0.17 修复记录](GODOT_PARITY_FIXES_0.17.md)。

## 0.16 内置字体与点击焦点框

默认字体改为 Adobe 官方思源黑体简体常规字重，使用 SIL OFL 1.1，可随安装包分发，详见 [字体授权核对](FONT_LICENSE.md)。旧 SimHei 没有游戏随包分发授权，不纳入发布包。普通／整页文本及动态界面标签默认读取 `app0:assets/SourceHanSansCN-Regular.otf`；现存 `font.ttf` 不再覆盖默认字体，原文件和存档无需删除。若之前已开启 `font-alt.ttf` 备用字体，关闭该选项即可使用内置字体。

以前无热态图片的按钮和音量滑条会无条件画焦点矩形，点击后也保留。现在鼠标／触摸操作不画这种导航框，方向键选择时才显示；触摸后再次按方向键可恢复提示。姓名编辑框的编辑状态提示仍保留，用于识别当前输入栏。

0.16 的字体和焦点修复已包含在当前包中；其后续行为修复见上面的 0.17 记录。

## 0.15 启动与保存逻辑对齐 Godot

Godot 的 `GameMain.cs` 只将 `com_0100.p` 的 50 个事件作为首次序章，完成时写入 `progress.txt`；其后 `com_0110.p`、`com_0120.p` 是正文。旧 PSV 错把三段线性脚本共 2,213 个事件都当成首次序章，导致 `introSeen` 写入太晚、退出重启重复序章；完成后再开新游戏又会跳过前两段正文。

0.15 将完成判断与跳过序章的位置修正为 Godot 的 50 事件边界。新游戏从 `com_0110.p:28` 的对白开始。旧存档中的事件序号保持不变；若旧 `presentation.json` 已包含 `com_0110.p`／`com_0120.p` 已读记录而 `introSeen` 仍为 false，启动时会自动修正完成标记并保留备份。

**Godot 本来就会每次启动播放 Logo → OP → 标题，且剧情依靠手动存档，不会自动续玩。** 本次保留该流程。完成序章后不再重新播放序章剧情；OP 可以点击或按 ×／○ 跳过。保存剧情请进入 START → 存读档页 → 保存 → 选择槽位，之后在标题“继续”中读取。没有存档时“继续”禁用，保存时同时写入已读记录，与 Godot 一致。

安装 `dist/tenshi-vita.vpk` 即可，原有字体、资源、已读记录和手动存档均沿用。首次序章中途退出且未手动保存的情况仍按 Godot 行为从头开始；已读记录只能证明完成状态，不能替代剧情存档。

## 0.14 触摸与图标修复

此前只处理鼠标事件并依赖 SDL 的触摸转鼠标功能，没有直接接收前屏触摸。所用 SDL 2.32.8 中，Vita 触摸面板选择在 hint 未设置时保持 0，而前屏设备 ID 是 1，导致前屏没有生成旧代码需要的鼠标点击。

0.14 直接处理前屏 `SDL_FINGERDOWN/MOTION/UP`，使用 SDL 已换算到原始 800×600 画布的坐标；一次按下只执行一次操作，忽略背触、第二根手指和重复生成的鼠标事件。桌面鼠标仍可操作，黑边点击不触发剧情。前八次有效触摸的画布坐标写入 `runtime.log`，方便复验。

应用图标改为 Godot Android 导出配置中的根目录 `icon.png`，缩放为 128×128 PNG。安装 `dist/tenshi-vita.vpk` 即可；游戏资源和存档沿用原目录。此次已完成主机事件队列回归和 VitaSDK 编译，Vita3K／真机触摸仍待安装实测。

## 0.13 启动修复

0.12 在转场和姓名确认时从离屏画布调用 `SDL_RenderReadPixels`，而 VitaSDK 的 GXM 后端不支持目标纹理回读。用户运行日志已证明字体成功打开并呈现 Logo，随后才报 `That operation is not supported`；错误页却无条件附上复制 `font.ttf` 的提示，容易被误认为缺少字体。

0.13 从保存的绘制数据在内存合成转场快照，保留遮罩、震屏和姓名确认效果，不再调用目标纹理回读。错误页改为显示实际错误与日志位置；字体缺失和字体已找到但解析失败会分别报告路径、文件大小及底层原因。安装新 VPK 即可，原有字体、资源和存档无需重新复制。

## 0.12 引入的界面和效果

- 使用 `sys.a` 的原始图集：标题及悬停按钮、姓名输入和确认、普通／整页对话框、选项、四页设置、历史回放、五页共 40 个存档槽与带文字的缩略图。
- 接入 CG 五页鉴赏、BGM 播放列表／循环／随机、回想入口、五结局解锁后的声优留言，以及已读和鉴赏解锁持久化。
- 文字逐字淡入、原版行宽与禁则排版、`$p` 字号、分页、等待光标、自动推进、已读快进、隐藏文本、语音继续／中断设置。
- 淡入淡出、原始遮罩转场、四类震屏／闪光／缩放、灰度和反相、七类雪花行为与图集；效果开关、雪花开关和时钟。
- 开场 Logo 的分层通道混合与轨迹、首次启动序章→片头→标题流程、五条结尾滚屏的装饰、字幕淡入淡出和收尾。
- 总音量及 BGM／语音／音效独立调节、音效停止与音量渐变。界面纹理及其 CPU 副本共按 32 MiB LRU 缓存预算管理，存档预览按页缓存。

## 操作与保存

| 按键 | 功能 |
| --- | --- |
| × | 确认；第一次补全当前页文字，第二次推进 |
| ○ | 返回；剧情中隐藏／恢复文本；跳过片头 |
| 方向键 | 菜单／选项／历史导航；设置滑条左右调整 |
| △ | 历史记录，可回放语音 |
| START | 设置／返回 |
| L | 自动推进 |
| R | 快进开关，默认限已读文本 |
| 前触摸屏 | 按下／松开按钮和选项；滑条可按住拖动 |

桌面验证程序支持 Enter／Space、Backspace、方向键、H（隐藏文本）、Esc／右键（设置）、A（自动）、S（快进）、Ctrl（强制快进）、V（重播语音）、Up／PageUp（历史）、F5／F9（存档／读档）；滑条支持滚轮。首次观看的结尾不能跳过，已解锁结尾可用确认或返回跳过。

存档为 `save1.json` 至 `save40.json`；沿用 0.11 的前五槽和资源指纹验证，写入时保留 `.bak`。设置、已读和解锁保存于 `presentation.json`。与 Godot 版的存档格式不互通。姓名可通过界面输入，也可使用数据目录中的 UTF-8 `player.json`：

```json
{"surname":"木田","given":"时纪"}
```

PSV 无桌面字体选择器；在数据目录额外放入 `font-alt.ttf` 后，原字体按钮可分别切换普通／整页文本字体。没有备用字体时会显示说明。PSV 的全屏／窗口按钮禁用，其余显示设置保留。启动和首次呈现阶段写入 `runtime.log`。

## 仍有差异

- Godot 的转场 **12／41／50** 目前只有已核对的淡出时序，原作对应的专用像素滤镜尚未还原。本版继承这一限制。
- Logo 的原生 **50／54／55 递归滤镜** 在 Godot 中也未还原，本版采用相同的透明度过程。
- SDL_ttf 与 Godot／Windows GDI 的字形栅格化可能有像素差异；已对照的是文本位置、字号、分页与推进距离。雪花的随机序列不同，运动规则按 Godot 移植。
- 尚未完成 PSV 真机验收。用户提供环境的 0.12 日志已确认上述回读兼容性错误；0.17 已通过主机回归和 VitaSDK 编译，仍待模拟器／设备安装复验。帧率、内存峰值、内置 800×600 H.264 片头的音画同步、触摸、系统输入法和休眠恢复仍需设备验证。

验证详情见 [VALIDATION.md](VALIDATION.md)。测试过程只输出文字日志和内存校验结果，不生成游戏截图。

## 构建

Windows 使用 PowerShell 7、Python 3 与 MSYS2 的 `tar`、`xz`、`bzip2`。从原片头重建还需要 PATH 中可用的 FFmpeg / FFprobe：

```powershell
./tools/bootstrap-windows.ps1 -MsysBin "D:/msys2/usr/bin"
# 从源码 ZIP 重建时，先准备内置片头与索引：
python tools/transcode-opening.py "D:/原始资源/openning.v"
./tools/build-vita.ps1
python tools/package-release.py
```

也可从同版本 VPK 中同时提取 `assets/opening.mp4` 与 `assets/opening.idx` 到 `assets/`，然后跳过转码命令。仅提取 MP4 而没有索引无法完成打包。

发布脚本同样更新固定文件名 `dist/tenshi-vita.zip`、`dist/tenshi-vita-source.zip` 和 `dist/SHA256SUMS.txt`。过去生成的历史文件不会自动删除；安装时使用上面的固定 VPK。

更新图标时运行 `python tools/make-icon.py "D:/原图/icon.png"`（需 Pillow），读取显式指定的原图；无需在仓库根目录存放原图。预生成的 PSV 图标也随源码包附带。

工具放在 `.tools/`，校验值见 `tools/dependencies.lock.json`。也可使用 VitaSDK 的 CMake：

```sh
cmake -S . -B build-vita-cmake -DTENSHI_VITA=ON
cmake --build build-vita-cmake
```

## 开发验证

主机 CMake 构建需要 SDL2、SDL2_image、SDL2_ttf、SDL2_mixer 与 `vorbisfile` 的开发库和 pkg-config 信息。音频验证目标包括 `voice_decoder_test`、`prefetched_audio_test` 和 `audio_loading_test`，分别检查语音解码、音乐预读及资源加载期间的音频行为；它们属于主机验证，不能代替设备试听。

`prepare/` 和参考导出工具只用于开发验证。运行 PSV 和构建 VPK 不需要 Godot 源码。若要重新生成与 Godot 对照的参考数据，先另外检出 [Godot 项目](https://github.com/dorakyuraduang/TenshinoInai12gatsu-godot)，然后通过 `GodotProjectRoot` 或 `--godot-root` 指定它的位置；该项目不随 PSV 仓库发布。先生成剧情参考，再独立运行媒体和回想导出：

```powershell
dotnet run --project prepare/Prepare.csproj -c Release -p:GodotProjectRoot="D:/Godot参考项目" -- "D:/原始资源" "data-reference"
$env:TENSHI_MEDIA_REFERENCE='1'
dotnet run --project prepare/Prepare.csproj -c Release -p:GodotProjectRoot="D:/Godot参考项目" -- "D:/原始资源" "data-reference"
Remove-Item Env:TENSHI_MEDIA_REFERENCE
$env:TENSHI_REPLAY_REFERENCE='1'
dotnet run --project prepare/Prepare.csproj -c Release -p:GodotProjectRoot="D:/Godot参考项目" -- "D:/原始资源" "data-reference"
Remove-Item Env:TENSHI_REPLAY_REFERENCE
python tools/presentation-reference.py data-reference --godot-root "D:/Godot参考项目"
```

新增 `logo_test <原始资源目录>` 比较全部 64 种 Logo 变体，并运行 `movie_vita_test` 检查实机指针句柄、异步准备、缓冲、EOF 和释放顺序；后者使用 API 替身，不是物理设备验收。

编译并运行 `runtime_test`、`coverage_test`、`replay_test`、`media_test`、`presentation_test`、`snapshot_test`、`startup_test` 和 `archive_audio_test`。前端验证命令为：

```text
tenshi.exe <原始资源目录> <测试模式> <帧数> <独立测试状态目录>
```

主机测试程序旁需有 `assets/SourceHanSansCN-Regular.otf`，状态目录不再要求 `font.ttf`。模式：`--story-smoke`、`--menu-smoke`、`--movie-smoke`、`--ending-smoke`、`--ui-smoke`、`--effects-smoke`、`--input-smoke`、`--startup-smoke`、`--restart-smoke`。UI 自检会写测试存档和设置，必须使用独立测试目录。SDL dummy 驱动允许离屏运行；没有截图输出。测试参考、状态、模拟器副本和工具缓存不进入发布包。

启动参考由 `python tools/startup-reference.py <原始资源目录> data-reference --godot-root <Godot项目目录>` 抽取当前 Godot 源码生成。运行 `startup_test data-reference <原始资源目录>` 比较脚本边界；将生成的 `startup.json` 放入新的独立测试状态目录，依次运行 `--startup-smoke` 和 `--restart-smoke`，验证真实文件与两次独立进程启动。

## 许可

原创代码采用根目录 [MIT](LICENSE)，第三方许可见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。发行 ZIP 附带源代码、构建资料和第三方声明。
