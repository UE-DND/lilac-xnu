# 系统验证与测试规范

本文档定义 `lilac-xnu` 项目的质量保障与测试体系规范，包括主机端单元测试套件、集成构建校验以及硬件端到端功能验收准则。

## 测试体系架构

项目构建了分层的测试验证体系：

1. **主机单元测试（Host Tests）**：纯主机环境运行，无需连接目标硬件或准备专有固件材料，重点验证协议状态机、内存算法、解析器及边界防护逻辑。
2. **集成构建测试（Integration Tests）**：在本地准备完备材料后，验证端到端构建流程、镜像格式对齐、Shared Cache 回读及参数一致性。
3. **硬件功能验收准则（Hardware Acceptance Criteria）**：指导在真实硬件上验证各子系统的功能完整性。

## 主机单元测试套件

主机测试位于 `tools/tests/` 目录，通过 `./lilac test host` 统一执行：

```sh
./lilac test host
```

### 核心测试模块与覆盖范围

- **存储与物理映射**：
  - `test-ufs-read.py`：测试 UFSHCI 2.1 只读传输协议、LUN0 容量探测、单块读取及 MBR/GPT 解析器。
  - `test-prepare-ufs-map.py` / `test-raw-ufs-map.py`：校验物理区间映射表生成工具的区间对齐、非重叠性检查及异常文件拦截。
  - `test-ufs-kernel.py`：校验虚拟块设备 `/dev/lilac_image` 的缓存命中、越界防护与只读属性。
- **图形与 GPU 控制**：
  - `test-a540-clock.c`：验证 Fabia PLL 在 180 MHz 与 257 MHz 档位下的分频系数计算与锁定检测逻辑。
  - `test-a5xx-ring.c` / `test-a5xx-pm4.c`：测试 CP 命令环边界回绕、间接缓冲区（IB）解析与 PM4 数据包合法性。
  - `test-gpu-va.c` / `test-mmio-guard.c`：模拟 SMMU CB0 独立页表映射、地址空间隔离以及越权 MMIO 访问异常捕获。
  - `test-framebuffer.py` / `test-layer-probe.py`：验证软件显示链路的双线性滤波、坐标缩放算法与图层合并。
- **多核心 SMP 与调试协议**：
  - `test-smp-topology.c`：验证 Kryo 280 8 核心拓扑解析与 PSCI 核心亲和性标识。
  - `test-psci-return.py`：模拟从核先暂停排空、主核心执行设备清理，再统一释放从核执行 `CPU_OFF` 的两阶段退出流程。
  - `test-debug-protocol.c` / `test-debug-usb.c`：测试 DWC3 调试通道的 Fastboot bulk 协议状态机与日志 CRC-32 校验。
- **触控与输入**：
  - `test-qup-i2c.c` / `test-qup-clock.c`：测试高通 QUP v2 FIFO 读写序列构造与 RCG2 时钟配置。
  - `test-clearpad-rmi.c` / `test-clearpad-f12.c`：测试 Synaptics RMI4 协议发现、F01 唤醒状态机及 F12 多点触控帧解析。
  - `test-home-gesture.c`：测试三指轻点虚拟 Home 键手势的判定时序与防误触算法。

## 集成构建测试

集成测试针对完整的系统输出镜像执行自动化核验：

```sh
./lilac test integration --build out/latest/userspace
```

### 校验维度

1. **APFS 镜像与 Shared Cache 完整性**：逐项回读系统磁盘镜像中的 43 个 arm64 Shared Cache 文件及动态链接器，校验 SHA-256 摘要与文件系统魔数。
2. **内存段布局与对齐**：核验 BootShim、XNU 内核段、设备树（DTB）与 RAMDisk 在物理内存中的排布与 16 KiB 页面对齐。
3. **环境一致性与符号链接**：通过 `./lilac sources check` 校验 `upstream.lock.json` 中声明的软链接与上游快照版本无偏差。

## 硬件功能验收准则

当镜像烧录至 Sony Xperia XZ1 Compact 设备执行时，需依据以下准则确认各子系统达到验收标准：

### 1. 存储与系统卷挂载
- 内核启动阶段通过 `shared/ufs-read.c` 成功读取 LUN0 并解析 GPT 分区。
- 成功通过物理区间映射挂载 APFS 系统卷，按需读取 Shared Cache，且整个过程无门铃超时或块 I/O 校验错误。

### 2. Adreno 540 GPU 硬件加速
- **固件认证**：CP 单元加载 `a530_pm4.fw` 与 `a530_pfp.fw` 后，通过 TrustZone 成功完成 ZAP 签名校验并退出受限模式。
- **渲染正确性**：Mesa Freedreno 驱动成功编译并执行着色器程序，多重纹理贴图采样读回像素与预期严格一致。
- **合成稳定性**：CoreAnimation 桌面合成持续稳定运行，Fence 完成序号正常递增。
- **隔离与容错**：在故意触发非法虚拟地址写入时，SMMU 准确报告故障，驱动成功隔离故障客户端并复位 GPU，系统桌面渲染不受影响。

### 3. 多点触控与手势交互
- S3330 控制器保持在工作阶段（`stage = 4`），连续上报触点多指按下、移动与释放事件。
- 用户态虚拟 HID 桥服务正常将触点事件投递至窗口系统，三指轻点手势可准确触发系统返回主屏幕。

### 4. 多核心 SMP 与安全停核
- 8 个 CPU 核心全部加入调度，跨核心 IPI 通信与定时器计数平稳递增。
- 系统请求重启或退出时，严格执行两阶段关核协议，BootShim 独立确认所有 7 个从核确切切断电源（PSCI `OFF`）后安全返回 Fastboot。
