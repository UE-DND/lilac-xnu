# Adreno 540 移植

设备树的 GPU ChipID 为 `0x05040001`，原厂 Android 将其识别为 Adreno 540 v2。`tools/analyze-a540.py` 可解析本地 DTB 中的 GPU、GPUCC、SMMU、时钟、电源域和固件信息；设备树资源不能证明硬件当前处于可用状态。

## 固件与命令准备

A540 使用 `a530_pm4.fw`、`a530_pfp.fw`、`a540_gpmu.fw2` 和 `a540_zap`。`tools/a540_firmware.py` 检查文件大小及结构，不验证签名。`--gpu-firmware-dir` 可以将本地固件放入 RAM 启动卷，但不会启用 GPU。

`kernel/lilac-a5xx-pm4.h` 和 `kernel/lilac-a5xx-ring.h` 提供命令编码及有界环形写入；`kernel/lilac-a540-gpmu.h` 将 GPMU 指令转换为 Type 4 包，`kernel/lilac-a540-me.h` 准备 CP_ME_INIT 命令。主机测试覆盖边界、校验和及写入失败。这些代码尚未接入硬件。

环形提交仍需调用方处理单生产者、硬件指针同步、缓存维护、内存屏障和 doorbell 顺序。电源、时钟、SMMU、TrustZone、固件认证、DMA 和恢复流程也尚未实现。

## 图形结果的范围

Mesa softpipe 和 Freedreno 曾为 iOS ARM64 交叉编译；RAM 中的 OSMesa 检查验证了用户态加载和软件绘制，不验证 GPU。CoreAnimation 和 UIKit 的离屏检查同样使用软件路径。

HakiOS 的 Adreno 642L 驱动面向不同硬件，不能直接用于 A540。相关实现可参考 [Linux A5xx 驱动](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/msm/adreno/a5xx_gpu.c)。
