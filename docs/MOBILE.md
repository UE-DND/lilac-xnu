# 移动运行时与 Apple launchd

`./lilac build userspace --mobile-runtime` 构建 RAM 启动镜像，用于检查只读系统卷、移动运行时和原版 Apple launchd。它不修改 UFS 系统文件。

## 启动流程

1. 静态 PID 1 检查 APFS、进程、原生 UFS、镜像挂载和动态执行。
2. 将 RAM 卷挂载到系统卷的 `/lilac-probe`，并为 `/private/var` 准备 ramfs。
3. 在子进程中检查线程、Mach IPC、kqueue、GCD、Unix socket、XPC、ramfs 和 Foundation。
4. 使用 bindfs 提供 notifyd、cfprefsd 和测试 job，再切换到 UFS 系统根卷。
5. 原版 Apple launchd 接管 PID 1，启动测试 job；随后检查服务、IOSurface 和软件显示。
6. 检查结束后，状态面板继续显示 RAM 中的结果。

准备或根卷切换失败时保留静态 PID 1。原版 launchd 启动后若 PID 1 退出，仍可能触发 panic。图形提交成功后，物理屏幕上的文本控制台会释放。

运行时和图形检查有超时限制。日志和状态仅保存在本次启动的 RAM 中。`xpc-local` 只覆盖进程内对象 API；CFPreferences 的同步和读回也不能单独证明 daemon RPC 路径完整。

## 桌面与多核实验

`--desktop-attempt` 增加部分原厂桌面及相关服务配置，并使用 RAM 日志诊断。此前观察到 runningboardd，未观察到 backboardd 和 SpringBoard；修复相关库缺失的尝试尚待实机验证。进程存在也不代表桌面或窗口服务已可用。

`--fast-boot` 需要 `--desktop-attempt`，会跳过部分重复诊断，但仍检查 GPT 和文件映射。去掉该参数可以恢复诊断面板。`--smp` 单独启用多核实验；固件是否接受 PSCI 调用及核心是否运行，仍需实机验证。

fast-boot 在静态 PID 1、系统卷挂载、RAM `/var` 就绪和执行 launchd 前记录启动时间与 UFS 计数。2026-11-03 的单核 USB 限时实验中，这四个检查点分别为 142、218、226、270 毫秒；首个图形 swap 出现在内核启动约 121 秒后。这些是特定设备与镜像的一次实验，不代表桌面已可操作。日志中还有激活、安装、图标等服务查找失败以及后续 Setup 进程反复退出；它们可能影响完整桌面启动，不能将半小时等待简单归结为 UFS 挂载。原系统服务尚未完整移植，RAM `/var` 也不会跨启动保存初始化状态。

2026-11-04 使用包含触摸桥和 Mesa 的单核桌面镜像启动，维护者观察到 Apple 标志或转圈，并以五指长按结束运行回收日志。`out/runs/20261104T045603.740771Z-device-collect/kernel.log` 记录系统卷在 223 毫秒挂载、launchd 在 5.644 秒启动；Mesa RAM 解压成功，但 CoreAnimation 明确报告没有 GPU 并使用软件渲染。SpringBoard、backboardd 与 datamigrator 均曾启动；第一个 migrationpluginwrapper 从 12:28:57 运行至 12:39:39，随后多个 wrapper 依次启动。日志同时记录 installd、mobileactivationd 等服务查找失败及 logd 的 SIGTRAP；不能仅据此断定哪个失败导致转圈，也没有桌面可操作的确认。本次日志经 USB 校验保存后设备返回 Sony Fastboot。

实测将 launchd 继承的标准输出重定向到 `/dev/null` 不能阻止它自行输出控制台日志，因此未将其视为启动性能修复。

交互桌面默认不限时，不要沿用短时硬件探针的 `--return-after` 参数；它从内核启动起计时，可能在桌面加载完成前强制返回 BootShim。`--usb-debug` 本身不设置定时器，主机采集器的 `--timeout` 也不控制设备重启。触摸通过 `--touchscreen` 显式启用，使用静态 PID1 配置的 `kern.exec_inject` 而不是会被 launchd 过滤的 plist `DYLD_INSERT_LIBRARIES`。详见 [触摸验证](TOUCH.md)。
