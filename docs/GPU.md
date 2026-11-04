# Adreno 540 GPU 移植

本项目为搭载高通骁龙 835（MSM8998）的 Sony Xperia XZ1 Compact 实现了 Adreno 540 GPU（设备树 ChipID 为 `0x05040001`，硬件版本为 Adreno 540 v2）的硬件加速支持。系统打通了从底层固件安全认证、内核 SMMU 地址空间隔离、Mesa Freedreno 用户态驱动到 CoreAnimation 桌面硬件合成的完整图形栈。

## 图形架构栈

```text
CoreAnimation / SpringBoard 桌面合成
  → EGL / GLES 3.1（Mesa 25.0.7 Freedreno 驱动）
  → HakiosGPU 内核服务（IOUserClient 接口与 SMMU 页表管理）
  → Adreno 540 硬件（ZAP 认证、CP 命令环、SMMU CB0 独立地址空间）
  → IOSurface 硬件呈现 → Sony 显示控制器
```

## 固件认证与 TrustZone 安全交接

Adreno 540 依赖专有微码与安全固件运行：
- **微码加载**：CP（Command Processor）使用 `a530_pm4.fw` 与 `a530_pfp.fw` 微码。初始化时保持复位状态，配置完毕后写入启动控制寄存器拉起微码。
- **ZAP 着色器固件认证**：通过 TrustZone 执行安全校验。内核先解析 ELF32 Split Metadata 结构，随后通过 ARM SMC 指令调用外设加载接口（PIL），请求执行 `PAS_AUTH_AND_RESET` 操作。
- **内存对齐约束**：依据 Sony PIL 内存重定位规范，系统为 ZAP 固件分配 1 MiB 独占且 1 MiB 物理对齐的专用内存区域，并在 `MEM_SETUP` 请求中传入 8 KiB 的物理 Extent；在 SMC 调用期间妥善处理中断重入并保留所有返回值，确保硬件 CP 单元顺利通过认证并解除渲染安全限制。

## 内存管理与 SMMU 硬件隔离

内核配置高通 SMMU Context Bank 0（CB0），采用 4 KiB 物理页表映射：

### 地址空间划分

- **内核 Ring 与固件映射**：固定映射在 `0x02000000` 虚拟基地址附近，配置为只读属性，防止异常越权覆写。
- **硬件完成 Fence 页**：映射于 `0x02018040`，仅赋予内核特权访问权限（APRIV）。
- **用户空间缓冲区**：分配在 `0x04000000..0x40000000` 虚拟地址区间内。

### 客户端多任务隔离与容错机制

每个 `IOUserClient` 拥有独立的 4 KiB 页表及虚拟地址分配器，系统支持最多 16 个并发客户端：
1. **串行化上下文切换**：切换客户端时，驱动首先排空正在执行的 GPU 硬件流水线，更新 SMMU 的 TTBR 寄存器，同步刷新 TLB 并失效 UCHE 缓存，最后执行 Mesa 上下文恢复间接缓冲区（IB）。
2. **硬件故障隔离**：驱动实时监听 SMMU 异常（如 FSR 寄存器）与 CP 保护状态。当检测到非法未映射虚拟地址访问或越权尝试修改内核 Ring Buffer 时，驱动精准定位故障客户端并将其隔离，随后执行硬件软复位，确保其他正常客户端与 SpringBoard 桌面合成不受影响。

## 时钟与动态电源管理

驱动在 `kernel/lilac-a540-clock.h` 中实现了高通 Fabia PLL 控制器逻辑：

- **动态频率档位**：支持 180 MHz（基础负载与待机）与 257 MHz（图形密集合成）两档时钟。在执行频率切换前，驱动排空 CP 队列，将 RCG 时钟源切回 19.2 MHz XO 振荡器，关闭 PLL，重置分频参数并等待 PLL 锁定后重新生效。
- **自动空闲挂起**：后台守护服务以 100 ms 为周期轮询 GPU 负荷状态。空闲持续 50 ms 后自动降频至 180 MHz；空闲持续超过 500 ms 时，依序停用 CP 处理、切断 SP/RBCCU 单元、关闭 GX 供电域并恢复 PM8005 电源管理状态。
- **并发唤醒防护**：在 GPU 从挂起状态恢复期间，驱动通过内部服务锁同步保护 `gpuReady()` 状态判断，避免新客户端分配请求在电源恢复的短暂间隙发生误报，确保桌面合成多线程并发稳定性。

## 构建与运行指南

```sh
# 1. 构建支持 A540 驱动的内核
./lilac build kernel

# 2. 构建包含 Mesa 与 GPU 桌面支持的用户空间镜像
./lilac build userspace \
  --kernel out/latest/kernel/hakios-kernel \
  --ufs-kernel-probe --ufs-system-map configs/lilac/system-map.bin \
  --mobile-runtime --desktop-bringup --touchscreen \
  --gpu-firmware-dir local/gpu-firmware \
  --gpu-mesa-dir out/build/mesa/25.0.7/out
```

### 调优与诊断参数

- `--no-gpu-runtime-pm`：禁用自动电源管理，使 GPU 在运行期间保持供电与时钟开启。
- `--no-gpu-dynamic-clock`：禁用动态调频机制，锁定在基准频率。
- `--gpu-backend-stage {1..7}`：支持对底层硬件阶段进行分步测试与排查（1: PM8005 供电, 2: SMMU 就绪, 3: GX 供电切换, 4: CP/DMA 传输, 5: ZAP 认证, 6: 渲染流水线配置, 7: 保持 Mesa 常驻服务）。

## 当前已知限制

1. **命令环与中断机制**：当前驱动采用单命令环并使用软件轮询 Fence 完成状态，尚未启用高通 GPU 硬件中断通知。
2. **CPR 闭环电压管理**：系统目前采用预设的稳态工作电压，高通 GPMU 与硬件 CPR 闭环动态核心电压调节尚待进一步适配。
