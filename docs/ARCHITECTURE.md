# 架构与内存布局

## 启动链

```text
Sony XBL / ABL
  → 加载 RAM 启动镜像并解压 GZIP
  → BootShim：初始化显示、解析 Mach-O、生成 Apple 设备树与 boot_args
  → XNU 内核：初始化中断控制器、虚拟内存、只读 UFS 与 APFS
  → 静态 PID 1 监督程序 → 原版 Apple launchd
```

在系统引导过程中，BootShim 作为承上启下的阶段，完成硬件基础配置并将控制权转交至 XNU 内核。内核接管后通过轻量只读驱动挂载存储并拉起用户空间。系统的完整能力与支持状态可参阅[项目状态](STATUS.md)。

## 引导协议与环境交接

系统进入 BootShim 时的执行状态为 AArch64 EL1 模式，初始运行在主引导核心（Kryo Silver 核心 0）上，加载物理基地址为 `0x80080000`，寄存器 `x0` 保存 Sony 原厂设备树（DTB）的物理地址。由于 Sony ABL 在跳转前已调用 `ExitBootServices`，引导流程不再依赖 UEFI 启动期块 I/O 协议，后续存储访问均由独立驱动接管。

`bootloader/shim-entry.S` 负责初始化栈指针、配置异常向量表并禁用 MMU 与数据缓存。BootShim 采用 ARMv8-A 规范与严格内存对齐选项进行编译，按 Mach-O 头部中的 `LC_SEGMENT_64` 装载各个内存段，从 `LC_UNIXTHREAD` 中提取内核入口点，并基于 `virtBase = 0xfffffff004000000` 与 `physBase = 0xa0000000` 计算物理映射地址。

### 物理内存分布

| 地址或范围 | 用途 |
| --- | --- |
| `0x80080000` 起 | BootShim 代码段及附加载荷（载荷起点由链接脚本确定） |
| `< 0x81f00000` | 解压后载荷的安全上限边界 |
| `0x82000000` | BootShim 栈顶（向低地址增长） |
| `0x83000000..0x8307ffff` | 512 KiB 内核返回与 Panic 诊断缓冲 |
| `0x9d400000` 起 | 原厂固件显示帧缓冲（720 × 1280，24 位 BGR888） |
| `0xa0000000..0xf7ffffff` | 1.375 GiB XNU 内存管理窗口 |
| `0xa3000000` 起 | XNU 内核段（末端取决于具体构建产物） |
| 内核末端以上 | 内存磁盘（RAMDisk）、Apple 设备树与 `boot_args` |

## 设备树生成与中断配置

BootShim 在启动阶段动态生成 Apple 格式的设备树节点，包括 `/chosen`、`/chosen/memory-map`、`/cpus`、`/arm-io/gic` 以及 NVRAM 镜像。

ARM 虚拟定时器的中断号 `virtual-timer-intid` 配置为 32 位小端值 19（即 PPI 3），驱动程序仅接收 16 至 31 范围内的私有外设中断（PPI）。

GICv3 中断控制器初始化时，先切换至 `ARE_NS` 非安全亲和路由模式，再配置各核心 GICR 的中断使能、优先级屏蔽与触发状态。中断处理程序使用 `EOIR` 结束中断，诊断读取使用 `HPPIR`，避免因读取 `IAR` 导致意外确认中断。

## 内存磁盘与进程监督

系统引导镜像使用 `LILC` 头部描述内核与 RAMDisk 的边界与校验信息。内存磁盘数据按照 16 KiB 虚拟内存页面对齐，内核初始化阶段通过 `mdevadd` 以 4 KiB 物理页为单位注册内存块设备。分页器在此过程中严格校验虚拟地址、偏移量与长度的页对齐约束。

RAM 卷根目录下的 `/sbin/launchd` 充当静态监督程序。当文件系统挂载与环境准备就绪后，静态 PID 1 切换根文件系统并执行原版 Apple `launchd`，拉起核心系统服务。

BootShim 通过 `XNULDRST` 结构体接收内核退出或异常返回状态。诊断缓冲区包含可读文本与二进制 Stackshot 数据，提取工具通过头部记录的偏移与长度进行解析。在发生异常的路径中，寄存器上下文（ESR、ELR、FAR、LR）将被完整保留并输出至诊断界面。

关于系统卷虚拟块设备与物理区间映射的实现细节，请参阅 [UFS 系统镜像映射](UFS-SYSTEM.md)。
