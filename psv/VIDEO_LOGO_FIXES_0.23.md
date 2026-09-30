# 0.23 解码器与 SDL 显存池竞争修复

日期：2026-09-30。

0.22 实机日志提供明确失败点：

```text
HW AVC config: visible=800x600 coded=800x608 refs=2 context=5767168 frames=5505024
HW video avc-context failed: 0x80024309 (-2147335415)
```

## 原因与修复

[VitaSDK 错误定义](https://docs.vitasdk.org/kernel_2error_8h_source.html) 将该码命名为 `SCE_KERNEL_ERROR_NO_FREE_PHYSICAL_PAGE_CDRAM`。这次未到解码器创建和送入视频阶段，不能归因于 MP4 格式或取帧接口。

[SDL 2.32.8 的 GXM 内存实现](https://github.com/libsdl-org/SDL/blob/release-2.32.8/src/render/vitagxm/SDL_render_vita_gxm_memory.c) 在创建首张纹理时，查询所有剩余 CDRAM 并分配整个 `gpu_texture_pool`。本项目先创建画布、标题和 Logo 纹理，再启动片头。实际链接的 VitaSDK `libSDL2.a` 中 `vita_gpu_mem_alloc` 反汇编也确认此流程。纹理释放只归还池内空间，不能解决随后独立调用 `sceKernelAllocMemBlock` 的请求。

0.22 未考虑 SDL 与解码器竞争同一内存池。原主机替身分配无限量内存，没有覆盖这个关键条件。

0.23 将 AVC 上下文、参考帧与 NV12 输出改为 `SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW`。这是独立的物理连续主内存池，硬解可使用；[参考后端](https://github.com/xfangfang/wiliwili/blob/88e5876bea9502d06f46a8656e3530684d3aaf7d/scripts/psv/ffmpeg/ffmpeg.patch) 包含同种分配模式。PHYCONT 采用 1 MiB 分配粒度和默认对齐选项。按实机返回大小，三块分别为 6、6、1 MiB，共 13 MiB。AAC 保持普通 uncached 内存；SDL 从解码输出复制到其 NV12 纹理，不额外要求解码输出位于 CDRAM。

日志新增分配前后和失败时的 `main / CDRAM / PHYCONT` 可用字节数，以及每块的池名和实际大小。未更改 MP4、索引、帧率、Logo 绘制和剧情状态。

## 验证与边界

1. 更新主机 API 替身，CDRAM 剩余为零、PHYCONT 初始为 26 MiB，并使用用户日志中的真实解码内存查询值。修改生产实现前，测试复现同样的 `avc-context / 0x80024309`。
2. 修改后，同一场景分配 13 MiB PHYCONT，并完成全片 2,695 AVC 帧和 3,870 AAC 数据包，音频总长、暂停、EOF、背压、无首帧超时、20 个原有故障点及资源释放检查通过。
3. PHYCONT 仅剩 12 MiB 时，第三块分配返回 `0x80024302`，前两块完整回收；不会跳过视频伪装成功。
4. VitaSDK ARM 构建、SELF 和 00.23 VPK 内容校验通过。视频及其他内置资产字节与 0.22 一致，旧版 VPK 保留。

上述测试确认这次内存竞争的重现和修复逻辑，不能证明真实硬件已经出帧。设备验收仍需出现 `HW AVC decoder ready`、`HW first video` 和 `HW playback started`，并检查完整播放和声音。
