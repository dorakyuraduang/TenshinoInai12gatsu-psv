# 0.21 默认片头流启动修复

日期：2026-09-30。此前的转码和 Logo 证据见 [0.20 记录](VIDEO_LOGO_FIXES_0.20.md)。

## 实机证据

用户提供的 0.20 日志：播放器句柄 `0x823C78E0`，READY 事件 `0x2`；GetStreamInfo 成功读出第 0 路 type=0、时长 89,836 ms，接着 `AVPlayer stream enable failed: -2140536830`，即 `0x806A0002`。Start 从未调用，音视频帧数均为 0。这确定了失败的 API 阶段，不能只凭错误码判断为坏视频或已启用。

## 修改

内置 MP4 固定只有一条 H.264 视频和一条 AAC 音频，没有切换多语言或多视频轨的需求。采用默认流选择，去掉 READY 阶段额外的 EnableStream；读取两路信息并检查恰好各一路，再明确调用 Start。参考 [vitaGL 原生视频示例](https://github.com/Rinnegatamante/vitaGL/blob/master/samples/video_playback/main.c) 的默认流播放方式；该示例使用 autoStart，本项目保留 autoStart=false 和 READY 后显式 Start。该选择没有将未知错误当作可忽略的“已启用”返回值。

保留启动、GetStreamInfo 等失败检查，日志增加完整 API 阶段、流编号及十六进制错误码。第一帧截止检查移到活跃状态返回之前：Start 后即使 IsActive 一直为真，15 秒未交付首帧也会报错。无帧与缓冲仍不会直接回到标题。主循环进入错误页时立即解除片头音频回调并关闭播放器／模块，避免在错误页继续解码及重复输出 0 帧的性能统计。

H.264 文件、800×600 分辨率、30 fps、89.84 秒时长、NV12 上传、PCM 环形缓冲、Logo GPU 绘制以及字体/图标保持原版本。视频 SHA-256 仍为 `209938e0bcc7790f2a655b9079c67e1859f14581a3faa5305d7fb010a9ee8278`。

## 验证

- 生产头文件的 API 替身将 EnableStream 返回值设为 `0x806A0002`；改动前头文件复现同样的实机错误，0.21 默认流流程成功取帧且没有调用 EnableStream。
- 4 种指针/UID、5 种初始化失败、NV12 更新/去重、PCM 回调隔离、暂停/恢复、缓冲、EOF 与关闭顺序通过。
- 新增 7 种故障：两路流信息查询错误、Start 同码失败、缺音频流、活跃/未活跃而无首帧、缺 READY。均报具体阶段错误，不静默跳过；关闭前解除音频回调，随后 Stop、Close、Unload。
- VitaSDK 编译、SELF、版本 00.21、安装包视频/字体/许可/图标一致性校验通过。无游戏截图生成或发送。

该替身检查验证调用流程和错误处理，不证明原生库或硬件解码。需实机确认日志出现 `AVPlayer explicitly started after READY`、`AVPlayer first video` 和 `AVPlayer first audio`，并完成全长音画同步及退出测试。Logo 已有用户实机约 60 fps 的证据，本次未更改该路径。
