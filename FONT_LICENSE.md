# 字体来源和分发许可

核对日期：2026-09-29。PSV 0.16 起默认内置思源黑体简体常规字重。

## 旧字体：不加入发行包

此前本机测试用 `fonts/simhei.ttf` 是 **SimHei 5.03 / 中易黑体**，版权信息为 Beijing ZhongYi Electronics Co., 1995–2005。SHA-256：`aa4560dd8fe5645745fed3ffa301c3ca4d6c03cbd738145b613303961ba733b8`。

该文件内许可只允许在所附微软产品条款下使用，且限制嵌入用途；OS/2 的 `fsType=0x0008` 表示文档可编辑嵌入，不构成游戏随包分发许可。没有找到覆盖本项目游戏分发的额外授权，不能仅因非商用便随 VPK 提供。

依据：[微软 SimHei 字体页](https://learn.microsoft.com/en-us/typography/font-list/simhei)、[微软字体分发 FAQ](https://learn.microsoft.com/en-us/typography/fonts/font-faq)。后者明确区分文档嵌入和游戏/应用分发。

## 内置字体：Source Han Sans CN Regular

- 来源：Adobe 官方 [source-han-sans](https://github.com/adobe-fonts/source-han-sans) 发布分支。
- 固定提交：`a4f7cf94edfb9d7ffbdfc4841de276358bd7e0f2`。
- 字体文件：`assets/SourceHanSansCN-Regular.otf`，8,429,224 字节。
- [该版本的官方字体](https://raw.githubusercontent.com/adobe-fonts/source-han-sans/a4f7cf94edfb9d7ffbdfc4841de276358bd7e0f2/SubsetOTF/CN/SourceHanSansCN-Regular.otf)，SHA-256：`e2bc8a2e7f37474b774fff8db758681ece40bb6947a90d571bce9dd60671a8e4`。
- 完整版权与许可：[SourceHanSans-OFL.txt](licenses/SourceHanSans-OFL.txt)。上游文件及其 Git blob 哈希另见 [来源清单](assets/source-han-sans.json)。
- 许可为 **SIL Open Font License 1.1**，允许随软件捆绑、嵌入和再分发，也允许随商业软件分发；须保留版权和许可，不能把字体单独销售。
- 本项目直接使用上游提供的简体区域版本，没有自行裁剪、转换、改名或修改字体内容。字体保留 OFL，项目 MIT 许可不替代它。
- VPK、发行 ZIP 和源码 ZIP 保留完整许可；打包脚本校验内置字体与固定版本 SHA-256 一致，并继续排除其他外来字体和游戏档案。

## 加载行为

PSV 默认从 `app0:assets/SourceHanSansCN-Regular.otf` 读取；Windows 测试版从可执行文件旁的 `assets/` 读取。普通文本、整页文本和动态 UI 标签共用内置字体。数据目录里的旧 `font.ttf` 不再覆盖默认字体，不需要删除它。

可选 `font-alt.ttf` 仍由玩家自行提供并在设置里选择；以前已开启备用字体的设置会保留。游戏原始资源档案仍需玩家提供。字体自身的许可不会授予游戏资源的分发权。

打包资源中的文字图像维持原有图集。新字体用于程序绘制的文字，字形与中易黑体不同，原有文字分页/推进的几何规则未改变。
