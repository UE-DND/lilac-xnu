# 用户空间与系统材料

在系统引导初期，RAMDisk 中的 `/sbin/launchd` 首先由 `userland/probe-init.c` 编译生成的静态监督程序接管。该监督程序负责自检存储与虚拟块设备，挂载 UFS 中的 APFS 系统镜像，初始化基于 RAM 的 `/private/var` 易失数据卷，随后执行根卷切换并将 PID 1 控制权移交至原版 Apple `launchd`。系统运行时的详细时序请参阅[移动运行时与 launchd](MOBILE.md)。

## 准备 iOS 系统材料

系统运行环境基于 iOS 16.5（版本号 20F66，适配机型标识包括 iPhone10,3 与 iPhone10,6）构建。

准备好对应机型的官方 IPSW 固件文件后，使用 `./lilac` 命令提取并解包系统根文件系统（RootFS）：

```sh
./lilac inputs ios \
  --ipsw /path/to/iPhone10,3,iPhone10,6_16.5_20F66_Restore.ipsw --unpack
```

解包工具会首先核验固件文件的完整性、BuildManifest 清单及各成员大小，随后将 RootFS 提取至本地配置指定的目录（默认位于 `local/ios/16.5-20F66/`）。若不指定 `--unpack` 参数，则仅校验并解包原始 DMG 镜像。

### 动态共享缓存（Shared Cache）机制

iOS 16.5 包含完整的原始 arm64 Shared Cache。构建流程直接保留完整的 Shared Cache 二进制文件，不进行拆解或修改，以保证动态链接器 `dyld` 具备最佳的加载与链接性能。

## 构建系统镜像

准备好 SDK 与 iOS 系统材料后，执行系统镜像构建：

```sh
# 1. 编译 XNU 内核，并自动准备工具链依赖
./lilac build kernel

# 2. 打包生成 APFS 系统磁盘镜像
./lilac build system

# 3. 按需编译用户空间 RAM 启动镜像
./lilac build userspace
```

`build system` 会独立编译测试程序 `hello_dyn`，在输出目录中生成完整的 APFS 格式磁盘镜像，并自动校验 Shared Cache 及系统二进制的完整性。它不依赖 `userspace` 的中间产物；也可通过 `--probe-binary PATH` 使用已有程序。如果只构建系统镜像，可用 `./lilac deps build` 准备工具链，无需编译内核。

`build userspace` 会自动调用 RAM 镜像打包流程，无需预先构建基础 RAM 镜像。单独调试底层引导时使用 `./lilac debug ramboot`，硬件探测使用 `./lilac debug hardware`。

由于 Sony Fastboot 协议对单次 RAM 镜像传输大小有限制，且 XNU 内存管理窗口配置为 1.375 GiB，容量庞大的 iOS 系统镜像无法整体放入 RAMDisk 启动，必须将其部署至 UFS 存储并通过物理扇区映射表按需分页加载，具体部署细节见 [UFS 系统镜像映射](UFS-SYSTEM.md)。
