# 文档目录

## 开始使用

- [项目状态](STATUS.md)：当前系统已验证的硬件支持与尚未实现的功能边界。
- [第三方源码与许可](SOURCES.md)：上游基线版本、源码归属与开源许可证声明。
- [系统验证与测试规范](VALIDATION.md)：自动化测试套件架构与系统功能验收准则。
- [用户空间与系统材料](USERLAND.md)：iOS 系统材料提取、Shared Cache 处理与用户空间构建。
- [UFS 系统镜像映射](UFS-SYSTEM.md)：基于不可变文件的物理区间映射原理与只读块设备发布。

## 架构与核心子系统

- [架构与内存布局](ARCHITECTURE.md)：启动链交接、物理内存分布与设备树构建。
- [硬件规格与寄存器地址](HARDWARE.md)：MSM8998 平台硬件配置与外设物理基地址对照。
- [Sony 只读 UFS 驱动](UFS.md)：UFSHCI 2.1 控制器协议、DMA 机制与只读设备实现。
- [移动运行时与 launchd](MOBILE.md)：系统初始化时序、根卷切换、开发桌面模式与显示缩放。
- [RAM 桌面启动耗时与数据迁移](BOOT-PERFORMANCE.md)：启动时序瓶颈分析、数据迁移机制与启动优化方案。
- [软件显示与 IOSurface](GRAPHICS.md)：基于原厂显示缓冲的像素缩放、滤波呈现与采样。
- [Adreno 540 GPU 移植](GPU.md)：TrustZone 认证、Mesa Freedreno 驱动栈、SMMU 隔离与动态电源管理。
- [触摸屏驱动移植](TOUCH.md)：Synaptics S3330 控制器时钟、RMI4/F12 协议解析与虚拟输入桥。
- [按键与后置相机适配](BUTTONS-CAMERA.md)：PM8998 按键轮询驱动设计与相机子系统硬件调研。
- [RAM USB 调试机制](USB-DEBUG.md)：BootShim 阶段 DWC3 调试通道协议与两阶段安全关核退出机制。
