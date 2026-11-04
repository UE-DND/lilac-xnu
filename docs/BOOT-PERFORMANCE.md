# RAM 桌面启动耗时与数据迁移机制

本文档分析 `lilac-xnu` 在 RAM 引导模式下的系统启动时序瓶颈，阐述 iOS 原生数据迁移（Data Migration）机制在内存易失盘环境下的级联超时原因，并介绍 `--ram-migration-state` 启动优化方案的技术实现。

## 系统启动时序

系统在搭载 MSM8998 的 Sony Xperia XZ1 Compact 上的典型启动阶段时序如下：

| 启动检查点 | 典型耗时 | 阶段说明 |
| --- | ---: | --- |
| 静态 PID 1 启动 | 约 0.15 秒 | 内核完成内存与外设初始化，拉起静态监督程序 |
| UFS 系统卷挂载完成 | 约 0.22 秒 | 验证物理映射并挂载只读 APFS 系统卷 |
| RAM `/var` 目录就绪 | 约 0.27 秒 | 完成内存数据卷挂载并构建基础骨架目录 |
| 执行 Apple launchd | 约 5–15 秒 | 完成基础子系统校验与根卷切换，移交原版 `launchd` |

从底层时序可见，内核初始化与 UFS 物理块读取具备较高的执行效率。但在原版 Apple `launchd` 拉起系统桌面服务后，系统一度出现长达数十分钟的加载延迟，该延迟源于用户空间的数据迁移机制。

## 数据迁移瓶颈成因分析

在原生 iOS 架构中，系统通过 `datamigrator` 与 `backboardd` 协同管理跨版本升级的数据迁移：

1. **内存数据卷的易失特性**：
   当前移植方案中，可写用户卷 `/private/var` 采用 RAMDisk（ramfs）独立挂载。由于设备断电或重启后内存数据完全清空，每次引导后系统看到的都是一个全新的、缺乏历史配置的空数据卷。
2. **版本记录缺失导致误判升级**：
   系统通过 `/private/var/mobile/Library/Preferences/com.apple.migration.plist` 文件记录历史版本。若该文件缺失，`backboardd` 与 `datamigrator` 会判定设备刚刚经历了大版本固件更新，从而在 Apple 标志下方显示进度条，并强制执行全量数据迁移。
3. **插件链级联等待与超时**：
   `datamigrator` 依序唤起多达数十个 `migrationpluginwrapper` 插件子进程进行数据迁移。在当前轻量用户空间环境中，部分复杂的原厂服务（如 `com.apple.mobile.installd`、`com.apple.mobileactivationd`、`com.apple.managedconfiguration.profiled` 等）尚未启动，各迁移插件在尝试通过 Mach IPC 查找这些服务时发生长时间阻塞，最终触发 Watchdog 强制超时，导致开机加载阶段累积产生 10 至 15 分钟的停滞。

## 迁移记录优化方案（`--ram-migration-state`）

为了消除空内存盘引发的不必要全量迁移，项目设计了 `--ram-migration-state` 构建选项：

### 技术实现细节

1. **预置版本偏好文件**：
   在静态监督程序完成新 RAM `/var` 挂载后，自动向对应路径预置 `com.apple.migration.plist` 文件，写入与当前 iOS 16.5 镜像完全匹配的系统标识：
   ```xml
   <key>LastSystemVersion</key>
   <string>20F66</string>
   <key>DMLastMigrationResults</key>
   <dict>
       <key>buildVersion</key>
       <string>20F66</string>
   </dict>
   ```
2. **严格权限与所属权配置**：
   该文件由监督程序以 `mobile` 用户权限（UID 501，GID 501）创建，确保后续启动的系统守护进程具备合法的读写访问权限。
3. **优化成效**：
   `backboardd` 启动时读取该文件并确认系统版本一致，`datamigrator` 在启动后迅速完成轻量检查并正常退出（耗时缩减至约 11 秒），不会触发繁重的迁移插件子进程，从而使系统在短时间内顺利进入桌面渲染阶段。

### 构建使用方法

```sh
./lilac build userspace \
  --kernel out/latest/kernel/hakios-kernel \
  --ufs-kernel-probe --ufs-system-map configs/lilac/system-map.bin \
  --mobile-runtime --desktop-bringup --touchscreen \
  --gpu-firmware-dir local/gpu-firmware \
  --gpu-mesa-dir out/build/mesa/25.0.7/out \
  --ram-migration-state
```

## 启动分析工具

项目提供 `tools/analyze-boot.py` 辅助脚本，可解析系统启动控制台日志，提取关键检查点耗时、UFS 块 I/O 读取统计以及系统守护进程的调用状态，便于持续追踪系统性能瓶颈。
