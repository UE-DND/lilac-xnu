# 软件显示与 IOSurface

Sony ABL 在启动阶段配置并保留了一块物理分辨率为 720 × 1280、色彩格式为 24 位 BGR888 的连续物理显示缓冲，物理基地址位于 `0x9d400000`，每行扫描线跨度为 2160 字节。在软件显示降级链路中，`IOMobileFramebuffer` 服务利用 CPU 将用户空间的 IOSurface 图层内容缩放并写入该缓冲，无需依赖 GPU 硬件渲染或高通 DPU 显示控制器。

## 显示管道与像素操作

驱动底层的像素操作实现位于 `userland/graphics-present.h` 与 `MiniFramebufferPixels.h`：

- **格式支持**：兼容 24 位 BGR888 与 32 位 BGRA8888 缓冲区，严格保留行尾字节填充。
- **并发同步**：图层渲染、双线性滤波与像素采样均由同一把互斥锁串行化，防止屏幕撕裂。
- **写带宽优化**：对于 24 位扫描缓冲的对齐区域，驱动将连续 4 个像素（12 字节）合并打包为 3 个 32 位整字进行批量写入，未对齐及边界裁剪区域采用按字节写入，大幅提高内存写入带宽。

## 用户空间接口与测试

用户空间接口支持完整的 IOSurface 属性查询、Mach Port 跨进程共享、多平面 YUV 采样，并适配 CoreGraphics、CoreText、CoreAnimation 软件图层及 UIKit 离屏渲染。若需启用硬件加速渲染，请参阅 [Adreno 540 GPU 移植](GPU.md)。

### 自动化单元测试

```sh
python3 tools/tests/test-framebuffer.py
python3 tools/tests/test-display-probe.py
python3 tools/tests/test-graphics-probe.py
python3 tools/tests/test-layer-probe.py
```

帧缓冲主机测试支持在 ASan / UBSan 内存检测模式下运行，全面校验像素格式转换、坐标缩放、边界裁剪、滤波算法及越界保护逻辑。

## 状态与采样诊断接口

内核头文件 `kernel/lilac-display-api.h` 定义了 `selector 127` 用户态交互接口，供诊断工具安全查询驱动状态：
- 返回驱动版本、累计呈现帧数、屏幕物理格式与逻辑画布尺寸。
- 返回当前滤波状态以及屏幕均匀分布的 9 个采样点的 RGB 颜色数值。
- 该接口严格拒绝接收外部内存地址或偏移量，不暴露内核指针，确保运行安全。
