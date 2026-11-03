# 项目状态

以下状态记录截至 2026 年 11 月 3 日的设备实验。设备为 Xperia XZ1 Compact，实际结果受固件、设备配置和输入材料影响。11 月 3 日普通移动运行时 RAM 启动后，维护者确认诊断界面全部通过；验证范围见[验证记录](VALIDATION.md)。

## 已验证

- Sony ABL 通过 BootShim 加载 XNU；文本控制台、GIC、定时器和 APFS 文件执行可用。
- 原生 UFS 提供只读设备；通过不可变文件区间映射挂载系统卷，并加载原始 shared cache。
- 原版 Apple launchd 已运行。pthread、Mach IPC、kqueue、GCD、Unix socket、本地 XPC、ramfs 和 Foundation 检查通过；测试 job、bootstrap、notifyd 和偏好读写也已运行。
- IOSurface 共享和软件显示检查通过。CoreGraphics、CoreText、CoreAnimation 图层和 UIKit 离屏视图已在实机检查。

CFPreferences 可能回退到本地访问，偏好同步结果不能单独证明所有 daemon RPC 路径可用。图形结果使用 CPU 软件呈现，不能证明 GPU 或窗口服务器可用。

## 尚未完成

GPU 硬件初始化、触摸输入、持久可写数据卷和完整系统服务尚未完成。没有验证 SpringBoard 桌面或完整 iOS 启动。`/private/var` 位于 RAM，重启后数据会丢失。

A540 固件准备、命令编码和环形缓冲代码通过了主机检查，但尚未提交硬件命令。桌面启动和多核启动也仍是实验功能。详情见[图形](GPU.md)、[触摸](TOUCH.md)和[移动运行时](MOBILE.md)。
