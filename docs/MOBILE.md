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
