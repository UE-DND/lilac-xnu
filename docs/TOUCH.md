# 触摸屏移植

原厂设备树描述 Synaptics ClearPad，I²C 地址为 `0x2c`，QUP 地址为 `0x0c179000`。原厂 Android 在现有设备上识别到 S3330，报告最多 10 个触点、坐标上限 719 × 1279 和每个 F12 对象 8 字节。设备树中的资源不代表 XNU 接管时控制器仍通电或可访问。

## 已有代码

- `kernel/lilac-clearpad-f12.h` 解析 F12 寄存器布局和触点帧；非法帧不会修改状态。
- `kernel/lilac-clearpad-rmi.h` 通过注入的只读总线接口扫描功能并检查注意位。
- `kernel/lilac-clearpad-bus.h` 选择 RMI 页并读取数据；调用方必须串行化操作。
- `kernel/lilac-qup-i2c-tags.h` 生成 QUP v2 的读取和页选择序列。
- `kernel/lilac-qup-i2c.h` 实现有界的 QUP FIFO 传输，不使用 DMA。
- `kernel/lilac-qup-clock.h` 管理 BLSP1 AHB 投票和 QUP5 I²C 时钟，保留合法的继承时钟配置。
- `sources/xnu/bsd/dev/lilac-touch-reset.h` 检查继承的触摸 GPIO 状态，仅释放 GPIO89 复位；不切换显示、AVDD 或 VDDIO 电源。
- `sources/xnu/bsd/dev/lilac.touch.c` 由 `lilac_touch=1` 显式启用，执行 RMI 发现、F01 唤醒和 F12 轮询；失败后停止，不自动重试或刷写传感器固件。

主机测试覆盖多点事件、命令边界和失败注入。页选择本身是一笔 I²C 写入；注意位不变也不能保证读取期间坐标未变化。

## 实机诊断

`./lilac build hardware --kernel PATH --touch --gpu-snapshot` 生成独立的文字测试页，不加载桌面，也不启用 GPU 加速。`kern.lilac_touch_status` 返回阶段、QUP 错误和独立的时钟错误；`stage=4` 才表示触点采集已就绪，`stage>=101` 表示停止。测试页周期输出 `PROBE ALIVE`，用于区别程序仍在运行与画面不再更新；不能将其当作触摸通过。

2026-11-03 的设备照片显示 `stage=102 error=0 ver=0 polls=0 events=0`，AHB=`0x2000c220`、core=`1`、cfg=`0`。这表示在时钟配置检查中退出，尚未访问 QUP 或发现触摸传感器；QUP 的 `error=0` 不代表初始化成功。旧检查错误地仅接受 HID 分频字段 `1`。

固定 Linux v6.12 的 `gcc-msm8998.c` 将此 RCG 的父源 `0` 映射到 XO；`clk-rcg2.c` 的 `calc_rate()` 对 HID=`0` 不分频、HID=`1` 计算 `2/(1+1)`，两者都保留 19.2 MHz。当前检查接受这两种形式，但仍拒绝其他父源、分频或 M/N 模式，不重编程已启用的未知时钟。参考文件的提交和哈希记录于 `upstream.lock.json`，主机测试覆盖照片中的配置、未知速率、读回错误和各级超时。修复后的传输及触点结果仍需真机验证。

照片中的 `vm_swap_create_file failed @ 366 secs` 是交换文件失败时的系统时间，不是“在该调用中等待了 366 秒”的测量。该照片不足以定位进入 PID 1 前的长延迟。

下一次设备实验在 `touch_worker` 读取 QUP `+0x30` 时发生 kernel data abort。当前改为在访问版本前检查块复位状态，按固定 Linux／Sony 驱动先执行有界 QUP 软件复位；忙总线不重置。专用 MMIO load/store 仅在 PC、访问地址、方向、对齐、QUP 窗口和 EL1 data-abort 类型全部匹配时捕获异常，保存 ESR／偏移并停止后续 QUP 访问。其他内核异常仍进入原 panic 路径。`stage=107` 表示 MMIO 不可访问，`108` 表示块复位仍有效，`109` 表示软件复位失败或总线忙。这是故障隔离与初始化顺序修正，尚不能证明控制器访问权限、总线或传感器已可用。

USB 自动采集与返回 Fastboot 的入口见 [RAM USB 调试](USB-DEBUG.md)。屏幕故障信息仍作为 USB 失败时的后备。

同日已通过 USB 取得真实内核日志：`out/runs/20261103T131130.056934Z-device-run/kernel.log`。QUP 首次读取 `+0x404` 时产生 `ESR=0x96000010`（同步外部 data abort），`BCR=0`；驱动进入 `stage=107` 并停止后续访问。诊断程序继续运行至 90 秒定时返回，日志校验后自动回到 Sony Fastboot。上一轮不受保护的 QUP 访问导致全内核 panic 的问题已隔离；这不是触摸可用的证明，也不能仅凭 ESR 判定是权限还是供电问题。仍需定位 BLSP/QUP 访问条件，不能去掉保护或通过盲目重试掩盖错误。

## 控制器接管进展（2026-11-03）

最新 USB 日志已定位并修复三处接管问题：

1. 实际 DTB 的时钟 ID `0x8caa5b4f` / `0xacae5604` 对应 Sony **BLSP1** AHB / QUP5 I²C；此前使用 BLSP2 的寄存器和投票位。修正为 GCC `+0x17004`、`+0x21008`、RCG `+0x21020/24`、投票 bit17 后，QUP 版本读出 `0x20070000`，MMIO 外部异常消失。构建工具验证 GCC phandle 和这两个时钟 ID，不依赖固定 phandle 编号。
2. 启动时 GPIO89 为输出低，触摸被保持复位；GPIO133 也是输出低，但 Sony incell 驱动明确以 **低电平开启 VDDIO**，不能将它误判为断电并拉高。驱动要求这两脚已经是 GPIO 输出且 VDDIO 为低，仅释放 GPIO89、等待 100 ms；不访问 PMIC、不重置显示。`110` 拒绝未知供电/复用状态，`111` 表示复位引脚读回失败。构建时验证 GPIO 控制器及 89/125/133 的 DT 映射。
3. 写 F01 configured 后，实机控制寄存器读回 `0` 而非写入的 `0x80`，设备状态从 `0x81` 变为 `0`。按 Sony 初始化流程等待 20 ms，再验证未配置/刷写/故障状态清除及正常唤醒；不再错误要求 configured 位持续置位，也不跳过设备健康检查。

`out/runs/20261103T140026.867337Z-device-run/kernel.log` 记录 RMI/F12 发现成功（719 × 1279、10 个 8 字节对象）、`stage=4` 和无触点轮询，25 秒后自动取日志并回到 Sony Fastboot。该轮 `events=0`，**尚不能证明真实触点或桌面 HID 输入可用**。固定 Sony incell 参考源码的提交和内容哈希在 `upstream.lock.json`。

随后 180 秒实机测试 `out/runs/20261103T140240.470322Z-device-run/` 已收到 **897 个真实事件**，采样日志涵盖 DOWN / MOVE / UP、最多同时 4 个触点，最后活动掩码为 0。控制器保持 `stage=4`，QUP error 为 0；共有 8 次帧拒绝后继续工作，尚未区分是采样期间注意位改变还是帧内容检查失败，不将它们描述为“零错误”。日志 SHA-256 为 `49b2030f139c20ce0da9f5b4c5120bd37f08f2ca9e8273c9cf71d322087b7b34`，镜像 SHA-256 为 `523ffcee7c25d36469bd2767ea7c57b4309e0beccb9ee552bfb739f97190bad0`。完整命令、设备身份和自动 Fastboot 返回结果在 `result.json`，摘要在 `touch-summary.json`。

## 桌面输入验证

`--touchscreen` 将同一驱动和 HakiOS 的 `/dev/hakios-touch` → backboardd 虚拟 HID 桥接接入 fast-boot 桌面。静态 PID1 在切换根卷之后、执行 Apple launchd 之前，通过现有 `kern.exec_inject` 配置仅对 `backboardd` 注入库，检查库存在、配置冲突和 sysctl 读回；失败保留静态 PID1。不要在 launchd plist 中设置 `DYLD_INSERT_LIBRARIES`，原版 launchd 会过滤该变量。硬件事件通过不等于桌面点击有效，需要分别验证 HID 接收与实际界面响应。

```sh
./lilac build userspace --kernel out/latest/kernel/hakios-kernel \
  --ufs-kernel-probe --ufs-system-map configs/lilac/system-map.bin \
  --mobile-runtime --desktop-attempt --fast-boot --touchscreen \
  --usb-debug
./lilac device run --build out/latest/userspace \
  --fastboot /path/to/platform-tools/fastboot \
  --serial BH905S2WB5 --timeout 3600 --reboot-bootloader
```

仅 `--touchscreen --usb-debug` 同时启用时，输入桥接将服务创建、触点及 HID accepted/rejected 计数写入 USB 可收集的控制台日志。其余系统服务仍使用 fast-boot 的安静配置，不恢复整套诊断页。交互桌面不指定 `--return-after`，因此不会定时退出；`--timeout` 仅限制主机等待，不会重启设备。测试后可用内核已有的五指按住 1.5 秒返回手势触发日志回收（要求五个触点均被驱动识别），或在已返回的调试 USB 上使用 `device collect`。USB 调试拒绝 SMP。HID accepted 也不能替代真实桌面响应的观察。

### 限时桌面镜像的问题

`touch-desktop-01` 两次实验均以 `--- loader_return_after expired ---` 结束，不是 kernel panic。300 秒从内核早期启动计时，并非从桌面就绪计时；日志只证明 SpringBoard/backboardd 进程已运行，维护者未看到桌面。同时日志明确记录 `Disallowing environment variable: DYLD_INSERT_LIBRARIES`，旧 plist 方法没有加载输入桥接。

修正版 `touch-desktop-02` 使用上述 sysctl 方式，移除启动定时器。镜像 SHA-256 为 `b11e75bac654cce1fc180ec1dab4aeca9ab0fbb9a62b513aa2291b0162617454`；主机检查和 APFS 回读通过，桌面显示与输入效果仍以本次设备反馈为准。只有短时故障诊断才显式传入 `--return-after`；构建器对限时 fast-boot 镜像给出警告。

当前使用轮询，尚未完成触摸中断、十指实机验证和电源状态切换。HakiOS 的 Goodix 驱动面向其他硬件，不能直接使用。
