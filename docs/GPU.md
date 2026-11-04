# Adreno 540 移植

2026-11-04，SO-02K / p114 已跑通 **A540 → HakiosGPU → Mesa Freedreno → CoreAnimation 桌面合成 → IOSurface → Sony 显示**。这是实际硬件渲染，已独立验证 shader 和纹理的读回像素。当前使用固定 19.2 MHz、单环和轮询 fence；未完成动态调频、GPMU 自动电源管理、中断或挂起恢复，不能视作原厂性能或完整生产驱动。

## 真机验证

设备 ChipID `0x05040001`，原厂识别为 Adreno 540 v2。首轮完整验证在 `out/runs/20261104T063303.146166Z-device-run/`，镜像为 `out/runs/a540-desktop-20261104a/image/xnu-boot.img`：

- ZAP `PAS_AUTH_AND_RESET` 返回 `0/0`，CP 通过认证 shader 退出安全模式。
- 间接命令写回 `0x0540f00d`，`CACHE_FLUSH_TS` fence 写回 1。
- Mesa 报告 `FD540`、`OpenGL ES 3.1 Mesa 25.0.7`。`userland/gpu-probe.c` 编译 vertex/fragment shader，绘制三帧不同颜色并读回比较，再验证纹理的四个颜色区域；全部通过。软件 renderer 会被拒绝。
- 测试之后启动原版 Apple launchd/backboardd/SpringBoard。桌面经同一服务完成 2,459 次 GPU 提交，退出时完成 fence 同为 2,459。显示日志的持续区间约 28–29 帧/秒；这不是标准性能基准，也不证明所有界面效果正确。
- 退出完成 GPU 停止、GX 关闭、用户 SMMU 禁用、PM8005 恢复、MX/CX 释放。USB 回收日志并复核 Sony Fastboot。没有分区写入。

镜像 SHA-256：`78474ba8c9811630a984891c410df9722c72d9db31b7ec0de770aec5e99fee43`。日志 SHA-256：`0f6fdaf99ec5dbdb26b28cdcb8f4f49985020293bfa89b45fda6ab6b8ccaef22`。

最终回归在 `out/runs/20261104T064425.612528Z-device-run/` 通过 `tools/check-a540-run.py`：2,351 次提交全部完成，8 个持续显示区间为 28.58–29.15 帧/秒，最终 `FSR=0x400`（格式位，无故障）、`RBBM_STATUS=1`，CP interrupt/hardware/protection 均为 0。完整结果为该目录下的 `gpu-acceptance.json`。

最终测试镜像 `out/runs/a540-final-desktop-20261104/image/xnu-boot.img` 的 SHA-256 为 `3882287c3bc76575a701d45a0b0c90a2be45fb9a0f1d07b06f572cba9b4f3244`；日志 SHA-256 为 `59a052d3e52cc4f4b155bbd41c557d6445cf19ed2f026fb85a30548ffc7218bf`。交互镜像位于 `out/runs/a540-gpu-desktop-20261104/image/xnu-boot.img`，使用同一最终内核，省略定时退出；真机回归使用的是上面的 180 秒版本。

## 实现范围

`CONFIG_HAKIOS_A540` 选择 `sources/xnu/bsd/dev/lilac.a540.c`，与 A642L 后端互斥；共享 `hakios.gpu.cpp` 的 IOKit/Mesa 接口。普通镜像仍不自动开启 GPU；必须提供经过校验的固件并显式选择 GPU 桌面构建。

供电使用 MSM8998 RPM GLINK 的 `rwmx/vlvl` ACK、PM8005 S1 和 GPUCC。借用合适的继承电压；电源关闭时，在确认 GX collapsed/clamped 后设置固定低角电压，退出恢复。GX 未确认停止时保留上游供电和 DMA 内存到重启。RBCCU 先于 SP 开启，关闭顺序相反。

SMMU 仅使用 stream 0 / CB0，独立 4 KiB 页表。内核 ring/固件位于 `0x02000000` 附近，用户 VA 为 `0x04000000..0x40000000`。共享服务串行管理用户缓冲、缓存维护、IB 提交和完成 fence。当前多个客户端共享 GPU 地址空间，未实现进程间 GPU 内存隔离和故障恢复。

CP 使用 `a530_pm4.fw`、`a530_pfp.fw`。A540 不能使用早期 A530 snapshot 的 `CP_PFP_ME_CNTL=0x18000000` 暂停操作；初始化使用复位状态，配置后写 0 启动。停止先排空 VBIF，再执行 A540 整核复位。读到 RPTR 追上 WPTR 后，只轮询 RBBM 空闲状态，避免 CP 寄存器读取持续唤醒总线造成假超时。

ZAP 先解析 ELF32 split metadata，再由 TrustZone 完成认证；结构校验不代替签名认证。按 Sony PIL 的重定位方式保留独占、1 MiB 对齐的 1 MiB 内存，向 `MEM_SETUP` 传实际 8 KiB extent；SMC interrupted 续调保留全部返回寄存器。早期 `AUTH=-68` 的实验不能算通过；对齐这些调用细节并开启渲染域后认证通过，没有绕过安全模式。

渲染初始化设置 1 MiB GMEM、各单元 64 位地址模式、UBWC highest-bank-bit 15、LMLOADKILL quirk 和 CP 寄存器保护。提交仅在真实硬件 fence 完成后允许相关缓冲回收；发布结果不确定时保留缓冲。

固件和安全处理使用的专属页保留 wired 到重启。Sony 固件允许配置 CBAR/S2CR，但不接受恢复其初始值；清理独立确认 SMR 无效、CB0 禁用和 GX 关闭，不声称路由字段全部复原。GPMU `.fw2` 转码器已通过主机/固件检查，本次运行采用手动域管理，未启动 GPMU 或 CPR 闭环。

## 构建与验收

本地固件、Apple 系统、SDK 和 UFS 映射不得提交。`--gpu-mesa-dir` 必须指向与当前 Darwin 补丁摘要一致的 Mesa 输出。GPU 测试失败时保留诊断 PID1，不把软件回退称为 GPU 成功。

```sh
./lilac build kernel --output out/runs/gpu-kernel
./lilac build userspace \
  --kernel out/runs/gpu-kernel/hakios-kernel \
  --ufs-kernel-probe --ufs-system-map configs/lilac/system-map.bin \
  --mobile-runtime --desktop-attempt --fast-boot --touchscreen \
  --gpu-firmware-dir local/gpu-firmware \
  --gpu-mesa-dir out/build/mesa/ac7e15a25449c418874e/out \
  --usb-debug --return-after 180 --output out/runs/gpu-desktop
./lilac device run --build out/runs/gpu-desktop \
  --fastboot /path/to/fastboot --serial BH905S2WB5 \
  --timeout 240 --reboot-bootloader
python3 tools/check-a540-run.py out/runs/DEVICE_RUN --output out/runs/gpu-acceptance.json
```

目录必须是新目录。定时镜像从内核启动计时，180 秒到期即退出；交互镜像省略 `--return-after`。验收脚本验证日志摘要、认证、硬件 IB/fence、shader/纹理像素、桌面持续显示、最终故障状态、所有 fence 完成和电源清理；单独 `log_saved_fastboot_verified` 只证明 USB 回收，不代表 GPU 成功。

`--gpu-backend-stage` 供分阶段硬件定位：1 PM8005 快照，2 inactive SMMU，3 GX 供电，4 CP/DMA，5 ZAP，6 渲染配置及 IB/fence，7 保持 Mesa 服务。前 6 阶段结束即释放电源；第 7 阶段由正常退出路径回收。

主机检查：`./lilac sources check`、`./lilac test host`、`./lilac test integration --build <GPU桌面构建>`、`./lilac build kernel`。主机测试覆盖 RPM 握手/ACK、供电失败与清理、有界 PM4/ring、固件范围、VA 耗尽/重复释放和 GPU 验收误判；不能替代上述真机验证。

## 参考与早期记录

实现参考固定 Sony `696bf91693d3d8b03dd8a4afa1b12a78b33b6d73` 的 `clock-gpu-8998.c`、`adreno_a5xx.c`、`peripheral-loader.c`、`subsys-pil-tz.c`、`scm.c`，以及 [Linux v6.12 A5xx](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/msm/adreno/a5xx_gpu.c)。使用 Mesa 25.0.7 Freedreno 寄存器定义。

早期 CX/SMMU 只读日志在 `out/runs/20261103T155404.443756Z-device-run/`，RPM/SMMU 状态快照在 `out/runs/20261104T043233.597136Z-device-run/`；这些只证明当时的读取和 CX 投票。主动 PM8005 供电路径在 `out/runs/20261104T060526.368854Z-device-run/` 通过，CP/DMA 在 `out/runs/20261104T061258.663847Z-device-run/` 通过，ZAP 在 `out/runs/20261104T062355.816647Z-device-run/` 通过。Mesa softpipe、CoreGraphics/UIKit 的旧离屏验证仍属于软件绘制，不能与本次硬件验收混用。
