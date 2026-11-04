# 移动运行时与 Apple launchd

`lilac-xnu` 通过 `./lilac build userspace --mobile-runtime` 构建完整的移动运行时环境，支持在只读系统卷与基于 RAM 的临时数据卷上拉起原版 Apple `launchd`（PID 1）及 iOS 桌面服务。

## 系统初始化与时序

移动运行时的初始化分为以下阶段：

1. **静态监督程序启动**：内核加载完毕后，首先执行 RAMDisk 中的静态程序 `/sbin/launchd`（由 `userland/probe-init.c` 编译），负责硬件环境检查与虚拟设备自检。
2. **挂载存储与准备运行目录**：
   - 基于物理区间映射表（FIEMAP）发布只读虚拟块设备 `/dev/lilac_image`，并将 APFS 系统卷挂载至 `/lilac-probe`。
   - 为 `/private/var` 准备并挂载独立的内存文件系统（ramfs），构建必要的临时目录结构。
3. **子系统基础验证**：在独立子进程中校验 POSIX 线程（pthread）、Mach IPC、kqueue 事件机制、GCD（Grand Central Dispatch）、Unix 域套接字、本地 XPC 以及 Foundation 基础框架。
4. **根卷切换（Pivot Root）**：通过 bindfs 提供 `notifyd`、`cfprefsd` 等核心常驻服务配置文件，将系统根文件系统安全切换至只读 APFS 卷。
5. **移交原版 Apple `launchd`**：静态监督程序通过 `execve` 将 PID 1 控制权移交给原版 Apple `launchd`，随后系统常驻守护进程启动，并进一步拉起 `backboardd`、`SpringBoard` 以及 CoreAnimation 桌面合成服务。

## 开发桌面模式（`--desktop-bringup`）

### 设计背景与实现原理

在原生 iOS 16.5 系统中，系统首次开机时会拉起 `Setup`（Setup Assistant，初始设置向导）进程，要求连接 Apple 激活服务器以完成设备配置。在当前移植阶段，蜂窝基带、Wi-Fi 固件及 Apple 专属激活服务尚未完整适配，`Setup` 进程会因等待依赖服务响应超时被 Watchdog 机制终止（错误码 `0x8badf00d`），导致系统无法进入主屏幕。

为此，项目提供了开发桌面模式（`--desktop-bringup`，需搭配 `--fast-boot --touchscreen` 使用）：

1. **进程拦截注入**：静态监督程序在根卷切换前，通过 `kern.exec_inject` 系统调用为 `SpringBoard` 配置注入库（避免使用会被原版 `launchd` 自动过滤的 `DYLD_INSERT_LIBRARIES` 环境变量）。
2. **Objective-C 运行时适配**：注入库通过 Method Swizzling 拦截 `SBSetupManager` 的设置逻辑，依据真实 ABI 方法签名 `B24@0:8Q16`（返回布尔值，携带无符号长整型参数）进行严格校验，安全抑制 Setup Assistant 的拉起，并将本进程标记为已激活状态。
3. **独立性保障**：该模式仅在内存中生效，用于开发阶段主屏幕渲染与输入交互测试，不向底层文件系统写入虚假激活记录。省略 `--desktop-bringup` 参数即可无损恢复原始 Setup 启动路径。

## 显示缩放模式（`--display-mode`）

系统支持通过 `--display-mode` 参数调整桌面渲染画布的缩放策略：

- **`--display-mode scaled2`（默认推荐）**：采用 750 × 1624 逻辑分辨率（即 375 × 812 点），像素总量仅为原始画布的 4/9。该模式大幅减轻了 GPU 填充率与内存总线带宽负担，使 CoreAnimation 桌面合成保持高帧率流畅运行。
- **`--display-mode iphone`**：采用 iPhone X 物理规格的原生 1125 × 2436 逻辑画布，提供完全一致的点对点布局。
- **`--display-mode native`**：向系统直接报告设备物理屏幕的 720 × 1280 规格。

更多关于启动耗时优化与输入手势配置的信息，请参阅 [RAM 桌面启动耗时与数据迁移](BOOT-PERFORMANCE.md) 与 [触摸屏驱动移植](TOUCH.md)。
