# 硬件规格与地址

本项目面向 Sony Xperia XZ1 Compact（设备代号 `lilac`，型号包括 SO-02K 与 G8441），搭载高通骁龙 835（MSM8998 v2.1）平台。以下规格与物理地址基于硬件架构与原厂设备树配置。

| 项目 | 规格 |
| --- | --- |
| CPU | 4 个 Kryo 280 Gold（大核）与 4 个 Kryo 280 Silver（小核），ARMv8.0-A 架构 |
| 内存 | 4 GB LPDDR4X；当前 XNU 管理窗口为 1.375 GiB |
| 存储 | 32 GB UFS 2.1 |
| 显示 | 720 × 1280，24 位 BGR888，每行 2160 字节 |
| GPU | Adreno 540，设备树 ChipID `0x05040001` |

在原厂设备树中，DRAM 物理地址起始于 `0x80000000`，但部分高位与中间物理区域被调制解调器（Modem）、TrustZone（TZ）及显示子系统硬件保留，因此系统为 XNU 分配了连续安全的 1.375 GiB 内存管理窗口。处理器核心不支持 ARMv8.1 PAN 或 ARMv8.2 UAO 特性。

## 平台外设基地址

| 资源 | 地址或参数 |
| --- | --- |
| GICv3 Distributor | `0x17a00000`，64 KiB |
| GICv3 Redistributor | `0x17b00000`，1 MiB |
| 虚拟定时器 | PPI 3，即 INTID 19 |
| 显示缓冲 | `0x9d400000` |
| GPU／GPUCC／SMMU | `0x05000000`／`0x05065000`／`0x05040000` |
| UFS 控制器 | `0x01da4000`，长度 `0x2500`，INTID 297 |
| SPMI 仲裁器 | `0x0800f000` |
| PSHOLD | `0x010ac000`；写入 0 会触发硬件复位 |

### 核心拓扑与电源控制

多核心电源控制遵循 ARM PSCI 规范，通过 SMC 指令调用固件服务：
- 4 个 Kryo Silver 小核的 MPIDR 亲和性标识分别为 `0`、`1`、`2`、`3`。
- 4 个 Kryo Gold 大核的 MPIDR 亲和性标识分别为 `0x100`、`0x101`、`0x102`、`0x103`。

显示控制器为 MDSS MDP5，液晶显示屏面板通过 DSI 接口连接。UFS 2.1 存储控制器寄存器位于 `0x01da4000`，采用符合 UFSHCI 2.1 标准的只读轻量驱动进行管理。
