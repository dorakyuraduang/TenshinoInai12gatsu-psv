# 0.22 直接 H.264/AAC 硬解

日期：2026-09-30。

用户提供的 0.21 日志已经成功识别两路流、Start 返回成功、收到 PLAY，但 15 秒仍没有音视频帧。因此不再把 PLAY 当作解码成功证据。参考用户指定的 [vita-hw-decoder](https://github.com/spyro-98/vita-hw-decoder/tree/12f3fff5aaf7145ee3839318bd1e8e2b6840197c)，沿其 SceVideodec/SceCodecEngine 直接解码思路改造固定片头。

## 实现与来源

- 原 MP4 不变；离线 FFprobe 生成逐访问单元索引 `opening.idx`。播放器严格检查帧数、尺寸、时间、文件大小及读取边界，并将 AVC 的长度前缀转为 Annex B，在首次送入时附 SPS/PPS。
- 从实际 SPS 校验到 800×608 编码尺寸、可见 800×600、2 个参考帧。上下文及参考缓冲采用物理连续 CDRAM，并通过 CodecEngine 的 unmap 内存接口建立解码上下文。配置、布局和内存规则核对 [wiliwili 固定后端补丁](https://github.com/xfangfang/wiliwili/blob/88e5876bea9502d06f46a8656e3530684d3aaf7d/scripts/psv/ffmpeg/ffmpeg.patch)。
- AVC 使用 `DecodeAuInternal` 和 `DecodeGetPictureWithWorkPictureInternal` 分离接口，调用序列参考 [MIT ReAvPlayer](https://github.com/SonicMastr/ReAvPlayer/blob/abff24a21ffec3e57fc1cd3b1d17dc26666251b0/main.c)。本地调用这些接口，不装载 hook 插件。SDK 模块只用于提供原生编解码服务，不创建 SceAvPlayer 播放会话。
- AAC 使用 4 KiB 粒度的 uncached 输入/PCM 缓冲，经 SceAudiodec 解码至 SDL 自有 PCM 环。跳过首个 1,024 样本编码延迟并裁剪到 3,961,724 样本；MP4 中偶发的单样本舍入按固定 AAC 粒度规范。
- 第一帧与 PCM 都就绪后释放音频；SDL 已提交采样数为播放时钟，暂停时冻结。输出 NV12 使用 800×608 的实际布局，画面裁到 800×600；不做 CPU RGB 转换。所有压缩参考帧均解码，延迟时只丢弃过期显示帧。
- 初始化、送入 AU、取回图片、AAC、分配及关闭分别检查/记录。首次送入的三帧记录尺寸和出帧情况；无首帧超时记录 AU 和 AAC 数量。跳过和错误页先解除音频回调，再关闭解码器、库、上下文和内存。

本实现针对单个固定片头；它不是通用 MP4 播放器，不包含参考项目的网络、选轨、FFmpeg 运行时或 vita2d 渲染库。

## 验证

生产索引读取器抽取 2,695 个 AVC 和 3,870 个 AAC 数据包。FFmpeg 对抽取流完整解码：2,695 帧的逐帧 MD5 与原 MP4 全部一致。裁剪后的音频 S16LE SHA-256 `c436c8fb900219e2c6169d132522b34a5817103e435217ac3afadeb87c0dbd5a`，与原音轨相同。视频 SHA-256 仍是 `209938e0bcc7790f2a655b9079c67e1859f14581a3faa5305d7fb010a9ee8278`。

API 替身使用实际 SDK 头文件，覆盖完整时长、无丢失 AU、精确音频裁剪、首帧门控、暂停、EOF、20 个初始化/解码故障点、输入背压、无帧超时和提前退出。所有场景检查资源清理顺序、音频回调隔离、无遗留内存块。该测试验证数据和调用流程，不替代设备解码验证。

VitaSDK 编译、链接、SELF 与 VPK 校验通过。设备验证应出现 `HW AVC decoder ready`、`HW first video`、`HW first audio` 和 `HW playback started`；全长结束应记录 2,695 帧及 3,961,724 音频采样帧。原生画面、声音、同步与休眠尚待实测。Logo 沿用之前用户已验证的约 60 fps 路径。
