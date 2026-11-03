# 用户空间与系统材料

RAM APFS 中的 `/sbin/launchd` 最初是 `userland/probe-init.c` 实现的静态监督程序。它检查进程和存储后挂载 UFS 系统镜像；移动模式进一步准备 RAM 数据卷，切换根卷并执行原版 Apple launchd。流程见[移动运行时](MOBILE.md)。

## 准备 iOS 文件

现有流程使用 iOS 16.5（20F66），支持的 IPSW 标识为 iPhone10,3 和 iPhone10,6。输入文件应为 6,165,698,198 字节，SHA-256 为 `c1091c401f67e3aa7cdfeb2cc19e6c79be3c957aae4e097a019e9ccea07d52f2`。

自行取得文件后运行：

```sh
./lilac inputs ios \
  --ipsw /path/to/iPhone10,3,iPhone10,6_16.5_20F66_Restore.ipsw --unpack
```

工具先检查哈希、BuildManifest 和成员大小，再将材料提取到本地配置指定的 `ios` 目录，默认是 `local/ios/16.5-20F66/`。不加 `--unpack` 时只校验并提取 DMG。工具不会连接设备。原始系统树、SDK 和 Apple 二进制均留在本地。

43 个原始 arm64 shared cache 共 2,722,512,896 字节；当前流程保留原始 cache，不拆分为 dylib。

## 构建系统镜像

先准备内核和用户空间，再构建系统镜像：

```sh
./lilac build kernel
./lilac build userspace
./lilac build system
```

也可以通过 `--probe-binary PATH` 指定 `hello_dyn`。构建器只操作主机文件。将镜像放入设备后，必须重新测量物理区间并生成映射；见 [UFS 系统镜像](UFS-SYSTEM.md)。

当前 Fastboot 传输上限约为 16 MiB，XNU 内存窗口为 1.375 GiB。系统和 shared cache 按需从 UFS 读取，不能作为一个完整 RAMDisk 装入。
