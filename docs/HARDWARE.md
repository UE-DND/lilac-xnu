# 硬件规格与地址

项目针对 Sony Xperia XZ1 Compact（`lilac`，SO-02K／G8441）上的 MSM8998 v2.1。以下值来自现有设备和设备树，使用其他型号或固件前应重新核对。

| 项目 | 规格 |
| --- | --- |
| CPU | 4 个 Kryo 280 Gold 和 4 个 Kryo 280 Silver，ARMv8.0-A |
| 内存 | 4 GB LPDDR4X；当前 XNU 管理窗口为 1.375 GiB |
| 存储 | 32 GB UFS 2.1 |
| 显示 | 720 × 1280，24 位 BGR888，每行 2160 字节 |
| GPU | Adreno 540，设备树 ChipID `0x05040001` |

原厂 DTB 的内存范围从 `0x80000000` 开始，长度为 `0xfe4c0000`。这不代表整个范围都可供 XNU 分配。CPU 不支持 ARMv8.1 PAN 或 ARMv8.2 UAO。

## 平台地址

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

PSCI 使用 SMC。小核 MPIDR 为 `0` 至 `3`，大核为 `0x100` 至 `0x103`。显示控制器为 MDSS MDP5，面板通过 DSI 连接。HakiOS 参考 UFS 控制器地址 `0x01d84000` 不适用于此设备，其初始化依赖也不同。
