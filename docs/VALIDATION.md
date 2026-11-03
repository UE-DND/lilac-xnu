# 验证记录

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

## 主机

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
./lilac test integration --build out/runs/host-verified-fast
```

本地报告位于 `out/runs/` 和 `out/verification/`。缺失 SDK、固件或系统材料的集成
检查不算通过。新增驱动或修改内核运行行为时，应重新完成相应实机检查。

源码直接检出不包含 SDK、缓存、系统材料或子模块；可运行通用测试并构建 xperable。
源码入口整理后还重新构建了内核、普通 RAM 镜像、移动运行时、系统 APFS 和下载工具，
产物位于 `out/runs/source-verified-*/`。这些是主机复测产物，不冒充上文已实测镜像；
已实测镜像及其哈希单独保留。
