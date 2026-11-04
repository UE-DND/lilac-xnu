# RAM USB 调试

## 架构定位与工作模式

RAM USB 调试机制属于 BootShim 阶段内置的轻量回传与控制协议，工作于 XNU 内核启动前（自检模式）或内核退出/受控 Panic 之后，而非 XNU 运行期的实时控制台驱动。

核心实现位于 `bootloader/debug-usb.c`，使用高通 DWC3 USB 控制器的轮询模式与独立 DMA 缓冲区，提供符合 Fastboot 规范的 USB Bulk 传输接口。

### 安全边界与设计原则

- **无分区写入**：该调试通道仅提供只读的内存日志导出、接收校验确认与系统暖复位控制，不提供任何擦除分区、覆写存储或刷写固件的功能，确保测试过程对底层分区的零侵入。
- **非实时日志**：通道在内核受控退出或发生 Panic 后由 BootShim 接管激活，用于完整可靠地回收内存日志。

## 两阶段 SMP 关核安全保证机制

在 8 核心 Kryo 280 对称多处理（SMP）架构下，系统退出或崩溃时若从核尚未完全切断电源，而引导核心过早清理外设资源或复用内存，将导致跨核总线冲突与死锁。

为此，系统实现了两阶段安全停核退出协议：

1. **阶段一（暂停与总线排空）**：
   - 退出流程触发时，所有从核立即屏蔽本地中断，排空内存流水线与 MMIO 访问，向系统发布 `PARKED` 状态。
   - 主引导核心（CPU 0）确认所有从核均已暂停后，统一执行外设收尾与总线隔离。在此期间，内核拒绝任何新的 `CPU_ON` 请求。
2. **阶段二（统一关核与状态复核）**：
   - 主引导核心完成设备清理后，统一释放从核执行 ARM PSCI `CPU_OFF` 指令切断核心供电。
   - BootShim 接管控制权后，独立通过 PSCI 查询所有 7 个从核的真实电源状态。只有确认全部从核均处于确切的 `OFF` 状态后，BootShim 才复用内存缓冲区并启动 DWC3 USB 服务。
   - 若检测到任一核心仍处于活动状态，BootShim 坚决拒绝启动 USB 传输，并在物理屏幕上打印现场各核心的状态码，以防止内存污染并保护底层硬件。

## 主机端构建与采集工作流

### 1. 独立 USB 通道自检

```sh
./lilac build ramboot --kernel out/latest/kernel/hakios-kernel --usb-self-test
./lilac device run --build out/latest/ramboot \
  --fastboot /path/to/fastboot \
  --serial <设备序列号> --timeout 120 --reboot-bootloader
```

### 2. 包含内核定时返回的调试构建

```sh
./lilac build userspace \
  --kernel out/latest/kernel/hakios-kernel \
  --ufs-kernel-probe --ufs-system-map configs/lilac/system-map.bin \
  --mobile-runtime --desktop-bringup --touchscreen \
  --usb-debug --return-after 180

./lilac device run --build out/latest/userspace \
  --fastboot /path/to/fastboot \
  --serial <设备序列号> --timeout 300 --reboot-bootloader
```

若目标设备已在运行且此前已指定 `--usb-debug`，可直接使用 `./lilac device collect` 独立等待并回收日志。

## 传输协议与安全复位

1. **握手与设备核验**：主机端工具通过 Fastboot 协议核验设备型号与构建标识，确认设备处于解锁的正常通信状态。
2. **有界日志传输**：按批次读取内存中保留的有界日志内容，实时校验 CRC-32 校验和与 SHA-256 摘要，确保日志传输完整无损。
3. **受控暖复位**：主机确认日志落盘后向设备发送带 CRC 的 `oem log-ack`，随后下发 `reboot-bootloader`。设备通过 SPMI 配置 PM8998 PMIC 暖复位寄存器，优雅释放 `PS_HOLD`，使设备平滑重回 Sony Fastboot 模式待命。

## 自动化测试

通过 `./lilac test host` 可运行针对调试协议命令解析、DWC3 传输状态机、两阶段从核停核模拟及 PMIC 暖复位参数的单元测试，确保协议栈在各种边界条件下的健壮性。
