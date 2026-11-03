# IOSurface 与软件显示

Sony ABL 留下的显示缓冲为 720 × 1280、24 位 BGR888，每行 2160 字节。当前 IOMobileFramebuffer 服务使用 CPU 将 IOSurface 内容缩放到该缓冲，不初始化 Adreno 或 DPU。

## 已验证路径

`MiniFramebufferPixels.h` 处理像素读写和清屏，支持 24 位及 32 位缓冲并保留行尾填充。绘制、滤波与采样使用同一把锁。首次图形帧释放文字控制台后，实机原生尺寸、缩放、滤波和清屏检查通过，周期闪烁消失。

用户空间检查覆盖 IOSurface 属性、Mach port、跨进程共享、YUV 平面和屏幕采样。CoreGraphics、CoreText、CoreAnimation 图层及 UIKit 离屏视图检查也已在设备上通过。它们属于软件绘制，不证明窗口服务器、SpringBoard 或 GPU 加速可用。

```sh
python3 tools/test-framebuffer.py
python3 tools/test-display-probe.py
python3 tools/test-graphics-probe.py
python3 tools/test-layer-probe.py
```

部分测试需要本机 Apple 框架或系统字体。帧缓冲主机测试在 ASan／UBSan 下检查像素格式、缩放、裁剪、滤波和边界保护。

## 内部采样

`kernel/lilac-display-api.h` 定义的 selector 127 返回版本、呈现计数、屏幕格式、逻辑尺寸、滤波状态和九个 RGB 采样，不接受地址或偏移，也不暴露内核指针。内存采样不能单独证明显示面板已刷新；现有设备实验另行观察了画面与计数。
