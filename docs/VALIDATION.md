# 验证记录

## A540 硬件渲染与桌面合成（2026-11-04）

同一 SO-02K / p114 使用 RAM 启动，通过了从供电、SMMU、ZAP 认证到 Mesa
和桌面显示的完整功能链路。普通软件图形检查与本次硬件证据分开记录。

首轮 `out/runs/20261104T063303.146166Z-device-run/` 的证据：

- Mesa `FD540 / OpenGL ES 3.1 Mesa 25.0.7`。
- 三帧不同颜色的 shader 输出和四个纹理区域的读回像素全部匹配。
- 桌面执行 2,459 次提交，退出时完成数同为 2,459；持续显示区间约 28–29 帧/秒。
- 显示驱动使用 GPU 写入的 IOSurface，等待 fence 后呈现，没有显示等待超时。
- GPU 停止、SMMU 禁用、PM8005 恢复和 MX/CX 释放全部成功；日志回收后复核了 Sony Fastboot。
- 镜像 SHA-256：`78474ba8c9811630a984891c410df9722c72d9db31b7ec0de770aec5e99fee43`。
- 日志 SHA-256：`0f6fdaf99ec5dbdb26b28cdcb8f4f49985020293bfa89b45fda6ab6b8ccaef22`。

最终回归 `out/runs/20261104T064425.612528Z-device-run/` 又完成 2,351 次提交，
全部 fence 完成。8 个持续显示区间为 28.58–29.15 帧/秒；最终 SMMU 无故障、
RBBM 空闲、CP interrupt/hardware/protection 均为零。自动验收结果保存为该目录的
`gpu-acceptance.json`，USB 状态为 `log_saved_fastboot_verified`。

- 最终内核 SHA-256：`a9aaffb315d90d1b8b197e3f39f5c024cb34bb5a56bba57574458f3469a3ab1b`。
- 最终测试镜像 SHA-256：`3882287c3bc76575a701d45a0b0c90a2be45fb9a0f1d07b06f572cba9b4f3244`。
- 最终日志 SHA-256：`59a052d3e52cc4f4b155bbd41c557d6445cf19ed2f026fb85a30548ffc7218bf`。

当前固定 19.2 MHz、单环、轮询完成，不代表原厂性能、Metal、动态电源管理、
挂起恢复或全部桌面效果均已完成。完整实现范围及复现命令见 [GPU](GPU.md)。

源码检查及内核构建通过。44 个主机测试程序全部通过，报告为
`out/runs/20261104T064134.601264Z-test-host/`。五项集成检查全部通过，报告为
`out/runs/20261104T064423.675300Z-test-integration/`。首次集成检查使用了旧的
fast-boot 参数预期，未包含新增 GPU 启动参数；已扩展精确参数和像素验收程序
检查后复跑通过，没有放宽原有系统卷、任务及镜像摘要检查。

## 实机（2026-11-03）

Xperia XZ1 Compact SO-02K，p114 引导固件
`1306-5035_X_Boot_MSM8998_LA2.0_P_114`。设备原本已解锁；测试只发送
RAM 启动镜像，没有刷写分区、更新固件或改变锁状态。

普通移动运行时镜像在设备启动后，维护者通过屏幕确认“新版诊断界面全部通过”。
该结果是用户观察反馈，不是主机自动采集的屏幕或完整内核日志；不扩大为
fast-boot、SMP、GPU 加速、触摸、持久写入或 SpringBoard 已通过。

- 镜像：`out/runs/host-verified-mobile/image/xnu-boot.img`
- SHA-256：`df3e59ded83c0d09402eb63dd7f1feb2de1947e6731b9cee7d095125e9c76ca1`
- 工具：普通 p114 xperable，未启用 `LILAC_READONLY_SURVEY`。
- 配置：`DEVELOPMENT / ARM64 / BCM2837`，`lilac_ufs=1`；不启用 fast-boot 或 SMP。
- 本地命令、设备身份和传输日志：`out/runs/device-20261103-new-mobile/`。

在 xperable 产物目录运行的启动命令为：

```sh
./xperable -v -V -I /absolute/path/to/xnu-boot.img -1 -c boot -1
```

镜像下载返回 `OKAY`；发送 `boot` 后 USB 回复超时、设备离线。
下载工具退出码不能代表 XNU 的运行结果，必须结合设备观察。此次主机进程在报告
传输错误后未退出，已终止该主机进程，没有因此重启设备。

## 桌面启动

同日使用当前源码重新构建桌面配置，启用 `--mobile-runtime --desktop-attempt
--fast-boot`，不启用 SMP。维护者在 RAM 启动后确认“能进入桌面”。镜像 SHA-256
为 `ea46ced662daec5373d48076a373e8edc967e469268b8b6d0aaba2f6fdb74035`，
本地构建在 `out/runs/desktop-20261103T1120-userspace/`，设备记录在
`out/runs/device-20261103-desktop/`。此结果不证明 GPU 加速、触摸或完整系统服务可用。
此前列出的旧诊断镜像及过期主机构建已按维护者要求删除，哈希及验证记录保留。

## USB 日志与重启

2026-11-03 在同一 SO-02K/p114 上验证 USB 自检、90 秒内核定时返回和 20 秒受控 panic，三次均完成日志长度／CRC／SHA-256 校验落盘，随后直接返回原 Sony Fastboot 并复查身份。完整命令和回执见 [USB 调试记录](USB-DEBUG.md)。镜像 SHA-256 分别为：

- USB 自检：`c8121766b09eaf6360fa9ac9c463bd04c27d69a134b42d276a21a75e47919b2f`
- 触摸故障隔离：`d193de48de0ffebe2483dd2fe5834ba19baea2ac803d10090c42a7d1baa179fc`
- 受控 panic：`876748f29086db27e81116b3b8c8446c9595eb8d2dd334f83fa3a66e7b73ec03`

内核为 `f444d8d2a3e9e4fc218ba3d661b8e7b3a890e334e07962c59131e942818ec47e`。触摸访问故障不再导致全内核 panic，但 QUP `+0x404` 仍报告外部访问异常，触摸和 GPU 加速均未通过功能验收。

### S3330 真实触点采集

后续修正 BLSP1/QUP5 时钟、释放继承的 GPIO89 复位并修正 F01 配置状态检查后，
`touch-events-01-probe` 在同一 SO-02K / p114 上运行 180 秒。
`out/runs/20261103T140240.470322Z-device-run/` 保存完整命令、身份、USB 回执和日志：

- 镜像 SHA-256：`523ffcee7c25d36469bd2767ea7c57b4309e0beccb9ee552bfb739f97190bad0`。
- 日志 SHA-256：`49b2030f139c20ce0da9f5b4c5120bd37f08f2ca9e8273c9cf71d322087b7b34`。
- 897 个事件；观察到 DOWN/MOVE/UP、最多 4 指同时活动，最后全部释放。
- QUP error 为 0，保持 `stage=4`；8 次未分类帧拒绝后继续采集，没有停止或 panic。
- USB 校验和直接返回原 Sony Fastboot 均成功；没有固件或分区写入。

这是内核触点采集验证，不等于十指、中断、电源管理或桌面 HID 界面响应全部通过。
桌面测试方法及其独立边界见 [TOUCH.md](TOUCH.md)。

本轮 macOS 的 30 个通用测试程序通过，新增调试 USB、主机采集、PMIC 重启准备和设备树资源校验。报告为 `out/runs/20261103T131940.033151Z-test-host/`；这些新增测试尚未在 Linux 复跑。源码检查与构建前后不变检查通过。

## 主机构建与检查

2026-11-03 已完成 macOS 全部构建：XNU、普通 RAM 镜像、移动运行时、fast-boot、
系统 APFS、p114 xperable 和可选 Mesa。系统 APFS 的 43 个原始 shared cache 文件
已逐项回读和校验。构建目录与源码分离，源码修改不要求工作树干净。

macOS 与 Debian 12 aarch64 各通过 21 个通用测试程序，均构建了 pe-parse 和 p114
xperable；macOS 的固件、fast-boot、CoreGraphics、CoreAnimation、GPMU 共 5 个
集成检查程序通过。Linux 完整 SDK 交叉构建未执行；CI 配方尚未远程运行。

通用工作区测试包含 14 个用例，覆盖输入复制隔离、配置优先级、依赖哈希、共享链接完整性、互斥、
失败报告、成功产物发布和源码变化检测。内核已验证同配置增量复用、共享 UFS 修改
触发重建、导出头文件变化重建依赖库，以及编译选项变化切换缓存。

```sh
./lilac sources check
./lilac test host
./lilac test integration --build out/latest/userspace
```

本地报告位于 `out/runs/` 和 `out/verification/`。缺失 SDK、固件或系统材料的集成
检查不算通过。新增驱动或修改内核运行行为时，应重新完成相应实机检查。

源码直接检出不包含 SDK、缓存、系统材料或子模块；可运行通用测试并构建 xperable。
源码入口整理后还重新构建了内核、普通 RAM 镜像、移动运行时、系统 APFS 和下载工具。
主机复测结果不冒充实机结果；当前镜像由 `out/latest/` 指向，历史验证哈希单独保留。
