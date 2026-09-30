# Tenshi Vita（天使不在的十二月 PSV 移植）

《天使不在的十二月》的 PlayStation Vita 移植项目，基于 [Godot / C# 重制项目](https://github.com/dorakyuraduang/TenshinoInai12gatsu-godot) 编写独立 C++ / SDL2 运行时。PSV 上直接运行 VPK，无需安装 Godot 或 .NET。

当前发布 **0.26 测试版**，应用 ID 为 `TNSH00001`。包含原始资源直读、中文对白、存读档、设置、CG / 音乐鉴赏、触摸操作及内置思源黑体；安装包内置 H.264 / AAC 片头。Logo 约 60 帧已有实机日志，当前片头硬解、完整流程及 Vita3K 兼容性仍需设备复验。

## 下载与安装

- [下载固定名称安装包 tenshi-vita.vpk](https://github.com/dorakyuraduang/TenshinoInai12gatsu-psv/releases/download/v0.26/tenshi-vita.vpk)
- [查看全部发布文件及版本说明](https://github.com/dorakyuraduang/TenshinoInai12gatsu-psv/releases/tag/v0.26)
- **[完整安装、操作、存档与排错说明](psv/INSTALL.md)**

在可以安装自制软件的 PSV 上，用 VitaShell 安装 `tenshi-vita.vpk`。然后把自己持有的对应 GB18030 资源版的下列文件原样复制到 `ux0:/data/tenshi/`，无需解包：

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

中文字体与片头已内置；其余原始游戏资源需自行提供。Vita3K 中应复制到其 `pref-path` 下的 `ux0/data/tenshi/`，具体定位见安装说明。

## 游玩与保存

| 操作 | 功能 |
| --- | --- |
| × | 确认；补全文字后再次按下推进 |
| ○ | 返回、隐藏 / 恢复对白、跳过片头 |
| 方向键 | 菜单与选项导航、调整滑条 |
| △ | 历史记录 |
| START | 设置与存读档入口 |
| L / R | 自动播放 / 快进 |
| 前触摸屏 | 点击按钮和选项，拖动滑条 |

剧情依靠**手动存档**：`START → 存读档页 → 保存 → 槽位`，之后从标题“继续”读档。每次启动的 Logo / 片头流程与是否有存档分别处理，重启不会自动恢复当前剧情。存档、已读和设置均在 `ux0:/data/tenshi/`，升级前可备份整个目录。

## 开发与验证

- [构建步骤和移植说明](psv/README.md#构建)
- [验证记录与设备测试边界](psv/VALIDATION.md)
- [与 Godot 版的差异审计](psv/GODOT_PARITY_AUDIT.md)
- [第三方组件和字体许可](psv/THIRD_PARTY_NOTICES.md)

源代码位于 `psv/src/`，Windows 构建脚本位于 `psv/tools/`。`Scripts/` 保留生成开发参考所需的原 Godot 代码；玩家不需要运行这些工具。发行文件名固定为 `tenshi-vita.vpk`、`tenshi-vita.zip` 和 `tenshi-vita-source.zip`，版本通过包内版本号及 GitHub Release 标识。

已有 26 个界面、72 个输入事件与定向行为检查通过主机验证。当前仍有部分专用转场滤镜尚未还原，字体栅格化和雪花随机序列存在差异；真机视频出帧、完整音画同步、休眠和完整通关尚未验收。报告问题时请附版本、PSV / Vita3K 环境与 `ux0:/data/tenshi/runtime.log` 的相关文字日志。

## 许可与来源

本项目原创代码使用 [MIT 许可](LICENSE)。字体为 Adobe 思源黑体简体常规字重，按 SIL OFL 1.1 分发。原始游戏剧本、图像、声音和片头不适用本项目 MIT 许可。第三方版权与对应源码资料保留在 `psv/licenses/`、`psv/third_party/` 和发行源码包中。

感谢原 Godot 项目与所引用的开源组件。硬解实现的技术来源及检查范围见 [视频实现记录](psv/VIDEO_LOGO_FIXES_0.22.md)。
