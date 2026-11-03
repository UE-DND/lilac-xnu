# 架构与内存布局

## 启动链

```text
Sony XBL / ABL
  → 下载 RAM 镜像并展开 GZIP
  → BootShim：显示、Mach-O 装载、Apple 设备树和 boot_args
  → XNU：中断、内存管理、RAM 磁盘和 APFS
  → 静态 PID 1 → 可选的原版 Apple launchd
```

BootShim 也提供加载内核前终止的 UFS 诊断。XNU 通过同一只读传输访问存储；系统卷和用户空间的现状见[项目状态](STATUS.md)。

## 引导交接

设备实测入口为 EL1、小核 0，载入地址为 `0x80080000`，`x0` 保存 Sony DTB 地址。Sony 已调用 `ExitBootServices`，因此不能依赖 UEFI 启动期 Block I/O。代码包含 EL2 到 EL1h 的分支，但该分支尚未在此设备验证。

`shim-entry.S` 设置栈和异常向量并关闭 MMU 及缓存。BootShim 以 ARMv8-A、严格对齐和通用寄存器选项编译。它按 Mach-O 的 `LC_SEGMENT_64` 装载段，从 `LC_UNIXTHREAD` 获取入口，并使用 `virtBase=0xfffffff004000000` 和 `physBase=0xa0000000` 换算地址。

| 地址或范围 | 用途 |
| --- | --- |
| `0x80080000` 起 | BootShim 和附加载荷；载荷起点由链接器决定 |
| `<0x81f00000` | 展开后载荷的允许末端 |
| `0x82000000` | 向低地址增长的 BootShim 栈顶 |
| `0x83000000..0x8307ffff` | 512 KiB panic 日志缓冲 |
| `0x9d400000` 起 | 固件显示缓冲，720 × 1280，BGR888 |
| `0xa0000000..0xf7ffffff` | 1.375 GiB XNU 管理窗口 |
| `0xa3000000` 起 | 当前内核段；末端取决于构建 |
| 内核末端以上 | RAMDisk、Apple 设备树和 boot_args |

不要假定 BootShim 的固定大小，也不要把 DTB 中的全部 DRAM 当作可供 XNU 分配的内存。UFS 固件队列地址会随启动变化。

## 设备树和中断

BootShim 生成 `/chosen`、`/chosen/memory-map`、`/cpus`、`/arm-io/gic` 和 NVRAM 镜像。设备树的 `virtual-timer-intid` 为 32 位小端值 19；驱动只接受 16 至 31 的 PPI，缺省值 27 留给上游 QEMU 路径。

GIC 初始化先切换 ARE_NS 亲和路由，再设置 GICR 的中断使能、优先级和状态。实机 IRQ、SGI 2 计数和定时等待已得到验证。处理程序使用 EOIR，诊断读取 HPPIR，避免读取会确认中断的 IAR。

## RAM 磁盘和日志

`LILC` 头记录内核与 RAMDisk 长度。传输按 4 KiB 填充，内存磁盘按照当前 16 KiB 虚拟内存页补齐；`mdevadd` 仍以 4 KiB 页为单位。分页器校验虚拟地址、偏移和长度的页对齐。

RAM 卷中的 `/sbin/launchd` 是静态监督程序。移动模式准备完成后，PID 1 切换根卷并执行原版 Apple launchd。进程入口先将栈按 16 字节对齐。

BootShim 通过 `XNULDRST` 接收内核返回信息；panic 缓冲包含文字和二进制 stackshot，提取时使用头部的文字偏移与长度。只有异常路径才把 ESR、ELR、FAR 和 LR 解释为现场寄存器。

系统卷的只读设备和不可变区间映射见 [UFS 系统镜像](UFS-SYSTEM.md)。
