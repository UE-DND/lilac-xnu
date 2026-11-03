# UFS 系统镜像

当前系统卷是 Android userdata 中的未加密、不可变 APFS 普通文件。XNU 不挂载外层 ext4，而是根据经过校验的文件物理区间发布只读虚拟块设备，再挂载 APFS。

## 设备专用映射

物理区间映射只适用于生成它时的设备、GPT、文件分配和文件内容。移动、替换、重新复制或改写文件后，必须重新测量并生成映射。普通 `/data/local/tmp` 可能使用 FBE，不能假定其物理内容为明文。

现有设备实验使用大小为 5,769,691,136 字节的镜像，分配为 49 个完整、无重叠的 4 KiB 对齐区间。这个结果仅说明当时的文件状态，不是其他设备的配置。`configs/lilac/system-map.bin` 是本机文件，已被 Git 忽略。

通过 Android 文件接口将镜像放入自有未加密目录，冻结文件，并核对完整文件和原始扇区内容后，再采集 FIEMAP。根据当前 GPT 和镜像生成映射：

```sh
mkdir -p configs/lilac
python3 tools/prepare-ufs-map.py \
  --fiemap CURRENT_FIEMAP.json --layout CURRENT_GPT_LAYOUT.json \
  --image out/latest/system/system.apfs --output configs/lilac/system-map.bin
```

工具拒绝加密、共享、延迟分配、空洞、不对齐和覆盖不完整的区间。它只生成主机配置，不写入设备。内核仍会独立校验 GPT、userdata 范围、镜像首尾 CRC 和 APFS magic。

## 只读设备

`sources/xnu/bsd/dev/lilac.image.c` 检查长度、最多 4096 个区间、完整逻辑覆盖、物理区间重叠和整数溢出；`kernel/lilac-image.c` 是指向它的链接。root 可以通过 `kern.lilac_ufs_image` 注册一次内存配置；成功后发布 `/dev/lilac_image` 和 `/dev/rlilac_image`，写请求均被拒绝。

`./lilac build system` 在独立产物目录生成 APFS 并校验完整 shared cache 的回读。主机新生成的镜像不等于设备上原来的文件，不能直接用它为旧文件重新生成映射。主机构建不进行设备写入，也不改变现有设备文件及已核验映射。

已注册镜像使用 256 个 4 KiB 只读缓存块。控制器错误会停用设备，缓存不会掩盖错误；该优化的实机速度和稳定性尚待验证。`/private/var` 单独使用 ramfs，重启后丢失。

`tools/prepare-raw-ufs-map.py` 可以规划假定从 userdata 首块开始的候选映射，但不会写设备，也不能证明 GPT 未变或镜像已经写入。清除 userdata 会删除其中的现有系统文件。
