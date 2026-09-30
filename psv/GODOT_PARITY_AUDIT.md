# Godot → PSV 0.15 行为差异核对

核对日期：2026-09-29。基准是审计当时的 Godot 源码和 PSV 0.15 的实现。

此报告保留 0.15 审计快照。后续 0.16 改为内置 OFL 思源黑体，并区分触摸/鼠标与方向键的焦点提示；下列字体来源和焦点行为描述按 0.15 读取。其他差异未因该版本更新而自动解决。**当前 0.17 的后续修复和验证见 [0.17 修复记录](GODOT_PARITY_FIXES_0.17.md)，下表继续保留历史基准。**

**结论：PSV 0.15 的剧情解释、资源解码和基础排版已有较多对照，但界面、阅读状态、音频过渡及系统恢复仍有遗漏，不能称为完整等价移植。** 本次记录 21 类已确认实现差异，另列平台适配和待实机验证项目。清单是本次检查范围内的结果，不代表已排除全部其他差异。

本次只新增核对脚本和报告，没有修改游戏运行代码或重新发布 VPK。没有读取用户存档作为写入目标，没有向对话发送游戏图片。

## 基准和证据

- 校验了 `psv/src/main.cpp`、`interface.inc`、`runtime.hpp` 与发行源码 ZIP 的对应文件，字节一致；该源码 ZIP 也与 `tenshi-vita-0.15.zip` 内嵌版本一致。
- `main.cpp` SHA-256：`26b950985cca8cb44fa11488abe091d950bc8a6671380ce2126b29673d85b3c4`。
- `interface.inc` SHA-256：`0b3b7a71ccaf2ff3e699ea4c3eee5fd419dbefb7d055df3748abdc7f8c107099`。
- 检查了启动、剧情推进、消息/已读、存读档、设置、历史、鉴赏、选项、音频、字体/UI、转场、结尾和视频恢复相关源码。
- **动态证据**：从生产 `App` 生成独立 Windows SDL 审计程序，调用实际菜单、点击、触摸、存读档、消息推进和混音函数。使用 SDL dummy 驱动及独立状态目录，未启动用户的 Vita3K。
- **Godot 预期**来自逐项阅读当前 C# 方法；本轮没有启动 Godot 引擎逐画面对拍。参考事件使用已有的 C# 导出数据。测试故意针对源码审查发现的疑点，不是随机抽样，也不是整体通过率。
- 最终 20 个定向检查点：19 个复现差异，1 个确认一致（进入选项时会停止语音）。一个问题可能有多个检查点，因此不能把 19 个检查点当作 19 类独立缺陷。
- [审计入口](D:/tenshi/godot/psv/tools/parity-audit.ps1)、[检查实现](D:/tenshi/godot/psv/tests/parity_audit.inc)、[本轮基准结果 JSON](D:/tenshi/godot/psv/tests/parity-audit-baseline-0.15.json)、[最近运行结果](D:/tenshi/godot/psv/build-host/parity-audit-results.json)。

## 已确认的实现差异

以下“复现”表示在上述独立主机程序中运行了生产函数；“源码”表示已确认代码路径不同，尚未用两端运行画面或真机验收。

| 编号 | Godot 行为 | PSV 0.15 行为及影响 | 证据与位置 |
| --- | --- | --- | --- |
| A01 鉴赏入口接反 | 第二项“回想”进入回想，第三项“音乐鉴赏”进入音乐 | 图集顺序相同，但动作顺序为 CG→音乐→回想；点回想会打开音乐，点音乐会打开回想 | **点击复现**；[Godot Gallery:71](D:/tenshi/godot/Scripts/GameMain.Gallery.cs:71)、[PSV galleryUi:615](D:/tenshi/godot/psv/src/interface.inc:615)、[dispatch:868](D:/tenshi/godot/psv/src/interface.inc:868) |
| A02 读档未恢复消息阅读状态 | `RestoreScene`/`BeginMessage` 重新查当前台词已读；`RestorePage` 从该页逐字显示 | 清空 `messageKey`，未重算 `messageRead`，直接把该页显示完整。当前台词不能正常补记已读，快进判定可能沿用读档前的状态 | **复现**：已读台词读回后 key 为空、read=false、显示距离 396 而非 0；[Godot Saves:60](D:/tenshi/godot/Scripts/GameMain.Saves.cs:60)、[Messages:142](D:/tenshi/godot/Scripts/GameMain.Messages.cs:142)、[MessageView:186](D:/tenshi/godot/Scripts/MessageView.cs:186)、[PSV loadGame:598](D:/tenshi/godot/psv/src/main.cpp:598) |
| A03 快速两次点击会漏记已读 | 补全文字和翻到下一条时都调用 `MarkMessageRead` | 点击只补全/推进，记录已读依赖下一次帧更新；同一轮事件队列内两次点击可以跳过记录 | **复现**：同一帧两次 action 后进入第二条，第一条未入已读集合；[Godot Messages:170](D:/tenshi/godot/Scripts/GameMain.Messages.cs:170)、[PSV originalAction:1220](D:/tenshi/godot/psv/src/interface.inc:1220)、[tick:1465](D:/tenshi/godot/psv/src/interface.inc:1465) |
| A04 “翻页后语音继续”不完整 | 关闭中断语音后，下一条无配音时让原语音继续 | 每次 Dialogue 都调用 `playVoice`，该函数先无条件停止旧语音；即使新语音名为空也停止 | **复现**；[Godot ShowDialogue:971](D:/tenshi/godot/Scripts/GameMain.cs:971)、[PSV playVoice:258](D:/tenshi/godot/psv/src/main.cpp:258)、[Dialogue:465](D:/tenshi/godot/psv/src/main.cpp:465) |
| A05 已读快进仍播放新语音 | `_skipRunning` 时不启动当前配音 | Dialogue 不检查 `skipMode`，继续启动每条配音，快进时可能听到短促语音片段 | **复现**：已读快进模式下声道仍开始播放；同 A04 代码位置 |
| A06 BGM 渐变缺失 | 停止音乐按脚本 `Slot × 0.01秒` 渐弱；换曲时旧曲用 0.3 秒渐弱 | 忽略 Music 事件的 `slot`；先 `Mix_HaltMusic`，再立即换曲或停止，场景切换听感突兀 | **停止渐变复现**、换曲源码确认；[Godot PlayStoryMusic:1037](D:/tenshi/godot/Scripts/GameMain.cs:1037)、[PSV playMusic:268](D:/tenshi/godot/psv/src/main.cpp:268)、[Music:370](D:/tenshi/godot/psv/src/main.cpp:370) |
| A07 音效渐弱曲线不同 | 对 `volume_db` 做 Tween | 对 SDL 的整数线性音量插值；时长相同也不是相同的音量曲线 | **源码**；[Godot StopSound:47](D:/tenshi/godot/Scripts/GameMain.Sound.cs:47)、[PSV StopSound:495](D:/tenshi/godot/psv/src/main.cpp:495)、[tick:1419](D:/tenshi/godot/psv/src/interface.inc:1419) |
| A08 打开设置取消自动/快进 | 设置挡住剧情更新，关闭设置后保留原自动/快进状态 | `showUi` 一律清掉 `autoMode` 和 `skipMode`；进入设置就丢失状态。设置内新开启自动再关闭可保留，但这不等于保留进入前的状态 | **两项复现**；[Godot OpenSettings:137](D:/tenshi/godot/Scripts/GameMain.SystemUi.cs:137)、[CloseSystemUi:77](D:/tenshi/godot/Scripts/GameMain.SystemUi.cs:77)、[PSV showUi:270](D:/tenshi/godot/psv/src/interface.inc:270) |
| A09 自动阅读等待计算不同 | 用当前事件原文的 GB18030 字节数 | 用已经替换姓名、可能累积了多条事件的整段文字，并近似按非 ASCII 字符 2 字节计数；追加段落或姓名替换后等待时间不同 | **源码及真实事件样本**；[Godot Messages:234](D:/tenshi/godot/Scripts/GameMain.Messages.cs:234)、[PSV tick:1485](D:/tenshi/godot/psv/src/interface.inc:1485) |
| A10 初始设置不同 | 文字速度 5，自动速度 5，总音量 100% | 文字速度 4，自动速度 4，总音量 75%；相同资源首次运行就更慢、更小声 | **三项复现**；[Godot Preferences:13](D:/tenshi/godot/Scripts/GameMain.Messages.cs:13)、[PSV preferences:42](D:/tenshi/godot/psv/src/interface.inc:42) |
| A11 滑条拖动/滚轮未补全 | 点击定位、按住拖动连续更新、滚轮增减 | 点击和方向键有效，鼠标/手指移动只更新坐标，滑条值不跟着变化；滚轮也没有设置页处理 | **触摸拖动复现**：从最左拖到最右仍为 1，预期 20；[Godot OriginalSlider:170](D:/tenshi/godot/Scripts/GameMain.SystemUi.cs:170)、[PSV pointer:1010](D:/tenshi/godot/psv/src/interface.inc:1010)、[pointerEvent:1051](D:/tenshi/godot/psv/src/interface.inc:1051)、[wheel:1036](D:/tenshi/godot/psv/src/main.cpp:1036) |
| A12 历史浏览行为不同 | 翻过最新一条关闭历史；多页历史文本从第 1 页显示 | 翻到末尾后钳制在最后一条，不退出；渲染多页历史时取最后一页 | **末尾翻页复现**，多页显示源码；[Godot ChangeHistory:275](D:/tenshi/godot/Scripts/GameMain.SystemUi.cs:275)、[RenderHistory:318](D:/tenshi/godot/Scripts/GameMain.SystemUi.cs:318)、[PSV historyUi:595](D:/tenshi/godot/psv/src/interface.inc:595)、[dispatch:803](D:/tenshi/godot/psv/src/interface.inc:803) |
| A13 关闭对话窗未清说话人 | MessageWindow=0 同时把 speaker 清为 255 | 只改窗口可见性，保留旧 speaker；后续没有显式 Speaker 事件时，可能错误保留上一位姓名 | **复现及实际路线样本**；[Godot ApplySceneEvent:1010](D:/tenshi/godot/Scripts/GameMain.cs:1010)、[PSV MessageWindow:432](D:/tenshi/godot/psv/src/main.cpp:432) |
| A14 出现选项时保留快进状态 | `ShowChoices` 停止自动、快进、语音 | 停止自动和语音，但未清除快进；选完后可能继续快进已读文字 | **复现**；[Godot ShowChoices:39](D:/tenshi/godot/Scripts/GameMain.Routes.cs:39)、[PSV Choice:478](D:/tenshi/godot/psv/src/main.cpp:478)。本轮同时验证了语音会停止，这部分没有遗漏 |
| A15 按钮视觉状态/选项字号未对齐 | 对话按钮使用专门的普通、悬停、按下、禁用图；选项字号 24，焦点/悬停使用 frame 32 | 通用按钮只选普通/热态，没有按下态，禁用使用整体染暗；选项字号 20，图像选择由 `choice` 控制，鼠标经过不会像 Godot 一样切换热态 | **源码**；[Godot SystemUi:59](D:/tenshi/godot/Scripts/GameMain.SystemUi.cs:59)、[Routes:51](D:/tenshi/godot/Scripts/GameMain.Routes.cs:51)、[PSV button:252](D:/tenshi/godot/psv/src/interface.inc:252)、[choice draw:1345](D:/tenshi/godot/psv/src/interface.inc:1345) |
| A16 Windows Godot 字形阴影缺失 | GDI 字形路径绘制偏移 (1,1)、透明度 0.7 的黑色阴影，再绘制字形 | SDL_ttf 字形只绘制一次；基线/栅格也来自不同字体引擎。排版坐标一致并不代表字形像素一致 | **源码**；[Godot MessageView:198](D:/tenshi/godot/Scripts/MessageView.cs:198)、[Windows 判断:265](D:/tenshi/godot/Scripts/MessageView.cs:265)、[PSV drawMessage:362](D:/tenshi/godot/psv/src/interface.inc:362)。此阴影差异特指 Windows GDI 分支，Godot 非 Windows 回退分支本身也不画该阴影 |
| A17 强制快进/桌面快捷键不同 | Ctrl 可强制跳过未读；H 隐藏消息、V 重播语音、Up/PageUp 历史、F5/F9 存读档 | Ctrl 仍受已读条件限制；H 变历史，缺少对应 V/F5/F9/PageUp 路径；右键隐藏窗口，与 Godot 右键设置也不同 | **Ctrl 复现**，其他源码；[Godot Messages:212](D:/tenshi/godot/Scripts/GameMain.Messages.cs:212)、[shortcuts:313](D:/tenshi/godot/Scripts/GameMain.Messages.cs:313)、[PSV tick:1456](D:/tenshi/godot/psv/src/interface.inc:1456)、[keys:959](D:/tenshi/godot/psv/src/main.cpp:959)、[pointerEvent:1093](D:/tenshi/godot/psv/src/interface.inc:1093)。桌面快捷键与 PSV 手柄重映射需要分开验收 |
| A18 结局解锁落盘时机不同 | 进入 Ending 时立即写 `endings.json` | 仅标记 `uiDirty`，等待周期持久化或退出/关闭界面。进入结尾后很快强制结束进程，解锁可能尚未落盘 | **源码**；[Godot Ending:31](D:/tenshi/godot/Scripts/GameMain.Ending.cs:31)、[PSV Ending:536](D:/tenshi/godot/psv/src/main.cpp:536)、[persist interval:1402](D:/tenshi/godot/psv/src/interface.inc:1402) |
| A19 手动存读档流程不同 | 点有效槽直接读取；保存以最后的有效 Dialogue 和路线决策为锚点；不支持的存档场景给警告后留在界面 | 读取前多一次确认；保存整个当时 VM/画面（含等待状态），并禁止回想内保存；部分格式/脚本哈希异常抛出到主循环故障页 | **源码**；[Godot Saves:41](D:/tenshi/godot/Scripts/GameMain.Saves.cs:41)、[LoadSlot:60](D:/tenshi/godot/Scripts/GameMain.Saves.cs:60)、[slot click:99](D:/tenshi/godot/Scripts/GameMain.Saves.cs:99)、[PSV saveGame:562](D:/tenshi/godot/psv/src/main.cpp:562)、[loadGame:598](D:/tenshi/godot/psv/src/main.cpp:598)、[dispatch:769](D:/tenshi/godot/psv/src/interface.inc:769)。这不表示所有 Godot 回想存档都可成功读回，只说明保存入口条件不同 |
| A20 返回路径不同 | 系统界面右键/Esc 直接关闭覆盖层；回想完成调用 `ShowMenu` | CG、鉴赏、存档等按层级返回；回想完成后直接再打开回想列表 | **源码**；[Godot CloseSystemUi:77](D:/tenshi/godot/Scripts/GameMain.SystemUi.cs:77)、[route finish:965](D:/tenshi/godot/Scripts/GameMain.cs:965)、[PSV goBack:980](D:/tenshi/godot/psv/src/interface.inc:980)、[replay finish:339](D:/tenshi/godot/psv/src/main.cpp:339)。属于流程差异，不全是崩溃类缺陷 |
| A21 OP 休眠恢复缺失 | 视频组件后台通知时暂停，前台恢复后从原时钟位置继续 | 后台时直接 `movie.stop()`，前台只恢复混音，没有视频位置恢复；也没有在该路径显式转到标题 | **源码确认缺少恢复路径，实机外观待验**；[Godot video notifications:179](D:/tenshi/godot/addons/original_video/src/original_video_player.cpp:179)、[PSV background:1038](D:/tenshi/godot/psv/src/main.cpp:1038) |

### 真实剧情数据中能触发的问题

重新读取已有 C# 参考 trace 的事件，不输出剧情文本：

| 参考路线 | 事件数 | 停止 BGM 的事件 | 有配音后接无配音的相邻台词 | 追加文本台词 |
| --- | ---: | ---: | ---: | ---: |
| trace1 | 4,690 | 63 | 508 | 155 |
| trace2 | 6,767 | 92 | 751 | 225 |
| trace3 | 7,092 | 104 | 799 | 228 |

这些是脚本事件计数，不是每次游玩必然听到或遇到错误的次数；例如语音提前自然结束就听不到截断，语音继续设置也必须开启。

- A09：`com_0100.p:24` 当前事件为 73 个 GB18030 字节，而累积段落为 87。即使把两端自动速度都设成 5，在整页模式下对应等待为 217 帧和 252 帧，差 35 帧，约 0.58 秒。PSV 的默认自动速度还另有 A10 差异。
- A13：trace1 中 `asu_0310.p:8613`、`:8614`、`:8615` 之前窗口关闭后没有新的 Speaker，PSV 会保留编号 8；Godot 已清为 255。这不是只靠人造事件推导的条件。

## 平台适配造成的差异

这些要明确告知，不能用“平台不同”掩盖上表中可以补齐的逻辑。

| 项目 | 差异 |
| --- | --- |
| 字体选择 | Godot 可通过字体选择界面指定普通/整页字体；PSV 用数据目录里的 `font.ttf`、可选 `font-alt.ttf` 切换，没有等价的系统字体浏览器 |
| 显示和控制 | PSV 固定全屏，800×600 逻辑画布适配 960×544 屏幕；实体键与前屏触摸另行映射。窗口模式、桌面键盘不能直接照搬 |
| 存档格式/位置 | Godot 的 `save_XX.json` v3、旧 `save1.txt` 及分开的设置/解锁文件，与 PSV 的 `saveN.json` VM 快照、`presentation.json` 不互通；目前没有转换器 |
| 姓名输入 | PSV 的 UTF-8 输入长度上限为 48 字节，验证规则也不同；Godot 使用 LineEdit 并处理空白。PSV 输入法能否实际唤起仍需设备验证 |
| 视频后端 | Godot 使用 FFmpeg 扩展；PSV 使用 pl_mpeg，限制 MPEG-1/MP2、44100 Hz 和最高 800×600。现有原始 OP 的主机播放测试通过不代表支持 Godot 后端能接受的全部格式 |
| 随机效果 | 天气规则有移植，但随机数实现/种子不同，雪花等不会逐颗落在相同位置；应比较密度、运动规律、层次和性能 |

## 特效及实机验证边界

- 当前 Godot 的转场 12／41／50 也只保留淡化时序，专用像素滤镜尚未还原，见 [GameMain.Transitions.cs:39](D:/tenshi/godot/Scripts/GameMain.Transitions.cs:39)。已有验证记录还标注 Logo 50／54／55 递归滤镜为 Godot 本身的未完成项。不能把这些全部计为 PSV 相对 Godot 新增的遗漏。
- 正常脚本条件下已有布局、效果数学和媒体字节对照，详见 [此前验证记录](D:/tenshi/godot/psv/VALIDATION.md)。31,964 组布局比较的是几何，400 个效果样本比较的是数学值，26 个界面/766 帧离屏检查证明渲染路径能走通；它们都不能证明最终画面或完整交互等价。
- 本轮发现了旧 UI 自检的具体盲区：它直接进入各个子界面并检查非空画布，没有对鉴赏第二/第三个按钮的实际目标做断言；存读档只比较 VM/visual，不包含 `messageKey`、`messageRead`。已有检查通过与 A01/A02 同时成立。
- PSV 消息/天气更新和电影解码的 dt 都有 0.1 秒上限，Godot 的视频使用音频播放时钟。低帧率时的节奏和音画同步需要真机测量；本轮没有据此声称已复现音画不同步。见 [PSV tick:1397](D:/tenshi/godot/psv/src/interface.inc:1397)、[Movie draw:108](D:/tenshi/godot/psv/src/movie.hpp:108)、[Godot playback_clock:553](D:/tenshi/godot/addons/original_video/src/original_video_player.cpp:553)。
- Vita3K 的宿主鼠标→虚拟前屏输入、PSV 真机触摸、输入法、休眠、帧率、峰值内存、长时稳定性仍需设备验收。主机 SDL 的触摸注入验证不能代替这一步。
- 当前程序图标已经按 Godot 根目录 `icon.png` 适配为 Vita 图标尺寸，这是此前 0.14 的修复，不列为本次仍未完成项目。

## 关于启动和自动保存

Godot 本身没有退出后自动续玩剧情位置的功能，标题“继续”是手动存档列表。Godot 已完成首次序章后每次启动仍会播放 OP；重复 OP 与重复首次序章要分开判断。

此前 0.15 把首次序章界限从错误的全部 2,213 个基础事件修正到 `com_0100.p` 的 50 个事件，并保留 `com_0110`/`com_0120` 正文，兼容旧序号且迁移错误的完成标记。本轮发现的 A02/A03 则是仍未补齐的读档/已读逻辑，不能因为启动边界已修复就认为保存完全一致。

## 复现方法和修复顺序

使用本项目已有 Windows SDL/MinGW 工具链和原始游戏资源：

```powershell
& ./psv/tools/parity-audit.ps1
```

可通过 `-Resources`、`-Compiler`、`-SdlRoot` 指定路径。需要已生成 `psv/data-reference/startup.json` 及已有 SDL_ttf/mixer 工具目录和 `build-host` 运行库。脚本创建随机命名的隔离状态目录，复制项目字体和启动参考，生成单独测试可执行文件，不覆盖 `tenshi.exe` 或发行 VPK。

运行结果为 0 表示审计顺利完成并记录数据，**不表示对照全部一致**；一致性查看 JSON 的 `different` 与逐项记录。以后修复运行代码后可重跑，检查相应项目从 DIFFERENT 变为 same。Godot 预期是人工核对当前源码后写入的断言；若 Godot 行为改变，应同时复核预期。

建议顺序：

1. 先补 A02/A03/A13 的存档、已读、消息状态，避免进度和说话人错误。
2. 补 A01/A08/A11/A14 的菜单接线和输入状态，再检查所有图标的实际点击结果。
3. 补 A04–A07 的语音/BGM/音效过渡，统一 A09/A10 的阅读参数。
4. 对齐历史、按钮状态、字号、阴影及返回流程；明确保留的平台适配项。
5. 补 A18/A21 持久化和恢复，再进行 Vita3K/真机长时与音画同步验证。
