# 项目状态

以下状态记录截至 2026 年 11 月 4 日的设备实验。设备为 Xperia XZ1 Compact，实际结果受固件、设备配置和输入材料影响。11 月 3 日普通移动运行时 RAM 启动后，维护者确认诊断界面全部通过；验证范围见[验证记录](VALIDATION.md)。

## 已验证

- Sony ABL 通过 BootShim 加载 XNU；文本控制台、GIC、定时器和 APFS 文件执行可用。
- 原生 UFS 提供只读设备；通过不可变文件区间映射挂载系统卷，并加载原始 shared cache。
- 原版 Apple launchd 已运行。pthread、Mach IPC、kqueue、GCD、Unix socket、本地 XPC、ramfs 和 Foundation 检查通过；测试 job、bootstrap、notifyd 和偏好读写也已运行。
- IOSurface 共享和软件显示检查通过。CoreGraphics、CoreText、CoreAnimation 图层和 UIKit 离屏视图已在实机检查。
- 单核 RAM 调试镜像在自检、内核定时返回和受控 panic 后，均已通过 USB 自动保存日志并直接返回原 Sony Fastboot。不是实时 USB 控制台，见 [USB 调试](USB-DEBUG.md)。
- S3330 触摸已完成 QUP/I²C 接管、RMI/F12 发现和真实触点采集；180 秒实验收到 897 个事件，日志观察到最多 4 个同时触点、拖动与松开。桌面 HID 及界面响应需独立验收，见 [触摸](TOUCH.md)。
- A540 已完成认证、CP/SMMU、Mesa shader/纹理像素验收及 GPU 桌面合成。两次 180 秒 RAM 实验分别完成 2,459 和 2,351 次提交，最终持续显示区间为 28.58–29.15 帧/秒，全部 fence 完成并清理供电后返回 Fastboot，见 [GPU](GPU.md)。

CFPreferences 可能回退到本地访问，偏好同步结果不能单独证明所有 daemon RPC 路径可用。11 月 3 日的离屏图形检查使用 CPU 软件路径；11 月 4 日的 GPU 结果有独立硬件证据。

## 尚未完成

GPU 的动态调频、GPMU 自动电源管理、中断、挂起恢复和进程间 GPU 内存隔离仍未完成；当前使用固定 19.2 MHz、单环及轮询 fence。桌面触摸端到端验收、持久可写数据卷和完整系统服务尚未完成。11 月 3 日维护者已确认软件 fast-boot 镜像能够进入桌面；本次 GPU 结果来自单独的硬件像素及提交日志验收，仍不证明桌面输入或全部系统服务通过。`/private/var` 位于 RAM，重启后数据会丢失。

GPU 桌面需显式使用 `--gpu-mesa-dir` 和本地固件；普通构建不自动开启。硬件像素测试失败会保留诊断 PID1。多核启动仍是实验功能。详情见[图形](GPU.md)、[触摸](TOUCH.md)和[移动运行时](MOBILE.md)。
