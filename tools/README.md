# 工具目录

本目录包含用于存储映射准备、设备树分析、镜像打包及固件核验的独立辅助工具，以及 `./lilac` CLI 所依赖的底层编排库和自动化测试套件。

## 目录组成

- **根目录脚本**：面向特定任务的独立 Python 和 C 辅助工具。
- **`lilaclib/`**：`./lilac` 统一入口调用的核心 Python 模块库，负责参数解析、环境诊断、工具链配置及构建流程编排。
- **`tests/`**：自动化测试套件，包含单元测试与集成测试程序（Python 脚本与 C 源码）。

## 常用工具说明

日常构建使用 `./lilac build kernel|userspace|system|mesa|xperable`。基础 RAM 引导与硬件探测分别使用 `./lilac debug ramboot` 和 `./lilac debug hardware`；二者仍沿用统一的输出目录、构建记录及 `out/latest/` 发布机制。

`lilaclib/probes.py` 提供 `userspace` 与 `system` 共用的小型用户程序编译流程，系统镜像无需读取用户空间构建的中间产物。

### 存储映射工具

- **`prepare-ufs-map.py`**：依据 Android 系统中导出的 userdata 文件物理区间分布（FIEMAP）与 GPT 分区布局，生成供 XNU 只读镜像驱动解析的二进制区间映射文件 `system-map.bin`。
- **`prepare-raw-ufs-map.py`**：用于规划连续物理块区间映射的辅助工具。

### 镜像打包与固件分析

- **`mkbootimg-lilac.py`**：构建符合 Sony 引导规范的 Android 格式 RAM 引导镜像（包含 BootShim、内核及附加载荷）。
- **`analyze-dtb.py`**：解析并提取 Sony 原厂设备树（DTB）中的硬件资源、时钟控制器与寄存器基地址配置。
- **`disassemble-pe.py`**：基于 `pe-parse` 解析并反汇编 Sony ABL 的 LinuxLoader PE 镜像模块。
- **`a540_firmware.py` / `analyze-a540.py`**：校验并解析 Adreno 540 GPU 的 PM4/PFP 微码与 ZAP 固件结构。

## 测试开发规范

在 `tests/` 目录下新增测试时，请遵循以下命名规范：
- 命名格式为 `test-功能名称.py` 或 `test-功能名称.c`。
- 新增的主机单元测试可通过 `./lilac test host` 自动发现并执行。
- 若属于依赖外部材料或构建产物的集成测试，需在 `tools/lilaclib/testing.py` 中显式登记并配置前置依赖条件。
