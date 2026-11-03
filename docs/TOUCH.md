# 触摸屏移植

原厂设备树描述 Synaptics ClearPad，I²C 地址为 `0x2c`，QUP 地址为 `0x0c179000`。原厂 Android 在现有设备上识别到 S3330，报告最多 10 个触点、坐标上限 719 × 1279 和每个 F12 对象 8 字节。设备树中的资源不代表 XNU 接管时控制器仍通电或可访问。

## 已有代码

- `kernel/lilac-clearpad-f12.h` 解析 F12 寄存器布局和触点帧；非法帧不会修改状态。
- `kernel/lilac-clearpad-rmi.h` 通过注入的只读总线接口扫描功能并检查注意位。
- `kernel/lilac-clearpad-bus.h` 选择 RMI 页并读取数据；调用方必须串行化操作。
- `kernel/lilac-qup-i2c-tags.h` 生成 QUP v2 的读取和页选择序列。

主机测试覆盖多点事件、命令边界和失败注入。页选择本身是一笔 I²C 写入；注意位不变也不能保证读取期间坐标未变化。

## 待完成

这些准备代码尚未接入 XNU 或访问硬件。仍需验证供电、I²C、中断、RMI 功能发现和完整帧读取，再接入 HID 输入。HakiOS 的 Goodix 驱动面向其他硬件，不能直接使用。
