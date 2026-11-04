# 项目状态

本文档客观陈述 `lilac-xnu` 在 Sony Xperia XZ1 Compact（`lilac`，MSM8998）上的系统实现现状、已支持的核心特性以及当前存在的边界与限制。详细的验证规范请参阅[系统验证与测试规范](VALIDATION.md)。

## 已实现与支持特性

### 引导与底层架构

- **BootShim 引导与内核加载**：通过 Sony ABL 成功接管并加载 XNU 内核，支持文本控制台渲染、GICv3 中断路由配置、ARM 虚拟定时器初始化以及 Apple 设备树动态构建。
- **多核 SMP 对称多处理**：支持 8 个 Kryo 核心全开，包含跨核心 IPI 通信与分布式定时器调度。
- **两阶段安全停核退出**：实现退出阶段从核先暂停排空、主核心收尾设备，再由固件统一执行 `CPU_OFF` 的保护机制，避免多核并发关核时的死锁。

### 存储与文件系统

- **UFSHCI 2.1 只读驱动**：实现单块只读与 DMA 页面借用机制，发布 `/dev/lilac0` 只读块设备。
- **系统卷不可变物理映射**：基于 Android userdata 中的 APFS 镜像物理扇区表（FIEMAP），发布虚拟块设备 `/dev/lilac_image` 并安全挂载 APFS 系统卷，完整保留并复用原始 iOS arm64 Shared Cache。
- **RAM 数据卷**：使用内存文件系统（ramfs）挂载 `/private/var`，并通过预置数据迁移版本文件（`--ram-migration-state`）避免因内存盘清空导致系统重跑耗时的数据迁移流程。

### 用户空间与桌面环境

- **原版 Apple `launchd` 运行**：由静态 PID 1 监督程序顺利拉起原版 Apple `launchd`（PID 1），支持 pthread、Mach IPC、kqueue、GCD、Unix 域套接字、本地 XPC 以及 Foundation 框架调用。
- **开发桌面模式（`--desktop-bringup`）**：针对开发调试环境，通过 SpringBoard Objective-C 钩子安全旁路未适配的 Setup Assistant（首次设置向导），直接进入主屏幕以支持图形与交互测试。

### 图形与硬件渲染

- **Adreno 540 GPU 硬件加速**：打通 TrustZone ZAP 固件认证（`PAS_AUTH_AND_RESET`）、HakiosGPU 内核服务与 Mesa 25.0.7 Freedreno 驱动，实现 CoreAnimation 硬件桌面合成与提交。
- **GPU 电源管理与动态调频**：支持 Fabia PLL 180 MHz 与 257 MHz 动态切换、自动空闲挂起与电源状态恢复。
- **客户端与内存安全隔离**：基于 SMMU CB0 实现每客户端独立的 4 KiB 页表映射，实现地址空间隔离、内核 Ring 保护以及越权访问的硬件级隔离与复位恢复。
- **软件降级显示**：IOMobileFramebuffer 服务支持基于原厂 720×1280 24 位 BGR888 帧缓冲的像素缩放与滤波呈现。

### 输入与手势

- **Synaptics S3330 ClearPad 触摸**：接管 BLSP1 QUP5 I²C 控制器，完成 RMI4/F12 多指数据采集。
- **用户态虚拟 HID 桥接**：驱动采集的触点数据通过 `/dev/hakios-touch` 转发至 `backboardd` 虚拟 HID 设备。
- **虚拟 Home 键手势**：支持通过三指同时轻点屏幕触发 Home 按键事件。

## 已知限制与后续工作

- **物理按键与深度睡眠唤醒**：PM8998 音量与电源键已实现轮询驱动，但整机深度睡眠及外部中断唤醒机制尚未完成适配。详情见[按键与后置相机适配](BUTTONS-CAMERA.md)。
- **相机子系统**：MSM8998 CCI 控制器、CSIPHY 与传感器驱动尚未适配。
- **GPU 高级特性**：当前 GPU 驱动采用单命令环与轮询 Fence 机制，尚未启用硬件中断通知；GPMU 与 CPR 闭环调频尚未接入。详情见 [GPU 文档](GPU.md)。
- **数据持久化存储**：系统卷当前为只读挂载，用户数据卷 `/private/var` 位于 RAMDisk，设备重启后产生的数据不会持久保留。
