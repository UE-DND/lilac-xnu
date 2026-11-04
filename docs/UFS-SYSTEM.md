# UFS 系统镜像映射

系统 APFS 卷以不可变普通镜像文件的形式存储于 Android userdata 分区中。XNU 内核无需挂载宿主系统的 ext4 或 f2fs 文件系统，而是依据经过完整校验的文件物理扇区映射表（FIEMAP），直接将底层扇区发布为只读虚拟块设备，进而挂载原生 APFS 系统卷。

## 物理区间映射机制

物理区间映射严格对应生成时的设备 GPT 分区布局与文件物理分配情况。若在宿主系统内移动、覆盖或改写系统镜像文件，底层物理块分配将发生改变，必须重新测量并生成映射文件。

> **注意**：由于 Android 系统默认启用基于文件的加密（FBE），系统镜像文件必须放置于独立的未加密目录中，并冻结写入权限，以保证底层物理块内容为明文。

### 映射表生成工作流

在宿主系统中确认系统镜像完整写入未加密区域后，采集文件的物理区间分布（FIEMAP），并通过 `prepare-ufs-map.py` 工具生成二进制映射表：

```sh
mkdir -p configs/lilac
python3 tools/prepare-ufs-map.py \
  --fiemap CURRENT_FIEMAP.json --layout CURRENT_GPT_LAYOUT.json \
  --image out/latest/system/system.apfs --output configs/lilac/system-map.bin
```

`prepare-ufs-map.py` 会严格检查各数据区间的属性，拒绝存在加密、共享块、延迟分配（Delayed Allocation）、文件空洞、未按 4 KiB 对齐或逻辑覆盖不完整的异常区间。

## 只读虚拟块设备发布

虚拟块设备的内核驱动实现位于 `sources/xnu/bsd/dev/lilac.image.c`（由 `kernel/lilac-image.c` 软链接引用）：

1. **参数与边界核验**：驱动加载时校验映射表的整体长度、最多 4096 个物理区间限制、逻辑地址空间的完整连续性、物理区间是否存在相互重叠，以及防范整数溢出。
2. **接口注册**：特权进程通过 `sysctl kern.lilac_ufs_image` 将校验通过的映射表注册至内核。
3. **发布设备节点**：注册成功后，内核发布 `/dev/lilac_image`（块设备）与 `/dev/rlilac_image`（字符设备），两类设备统一拒绝所有写入请求。
4. **只读缓存加速**：驱动内置了 256 个 4 KiB 块的只读缓存，用于加速 APFS 元数据及频繁读取的索引节点。若底层 UFS 驱动报告硬件错误，虚拟设备将同步进入停用保护状态。

用户可写数据卷 `/private/var` 独立挂载于内存文件系统（ramfs）之上，以确保运行期间对系统只读镜像的完全隔离。
