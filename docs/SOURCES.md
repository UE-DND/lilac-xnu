# 第三方源码与许可

四个主项目以固定提交的源码快照直接纳入本仓库。完整提交、上游地址、排除项、源码归属和共享链接由根目录 `upstream.lock.json` 记录。

| 项目 | 上游基线 | 当前源码 |
| --- | --- | --- |
| HakiOS | `a48d2b29dd58da5ef322052a7f4f73d7eb8eebc4` | `sources/hakios/` |
| Apple XNU | `c54f35ca767986246321eb901baf8f5ff7923f6a`，`xnu-8796.121.2` | `sources/xnu/` |
| xperable | `0bf7060a966f91ed6251386bb6281648e416a03a` | `sources/xperable/` |
| pe-parse | `31ac5966503689d5693cd9fb520bd525a8710e17` | `third_party/pe-parse/` |
| Apple HFS | `31d5ec238dfab485e78a9a30b8b92d13e8914f03`，`hfs-627.100.6` | XNU 的 `bsd/hfs/`、`bsd/hfs_encodings/` |

XNU 包含 HakiOS、HFS 和 lilac 适配，是所有内核实现的权威源码。HakiOS 上游 `xnu/files/` 和驱动目录中的实现维护在 `sources/xnu/` 对应位置；`sources/hakios/drivers/` 保留配置元数据，不维护另一份内核实现。

各项目的 LICENSE、NOTICE、第三方许可文件及源码版权声明保留在相应目录。libdeflate 的 MIT 许可位于 `sources/xnu/bsd/miscfs/apfs/libdeflate/COPYING`。HFS 保留源码声明并受 Apple Public Source License 约束。本项目没有统一顶层许可证。

HakiOS 的预编译产物、嵌套 Git 元数据和 pe-parse 的可选 corkami 测试数据子模块不进入源码快照；依赖该数据集的上游测试默认关闭。上游完整 Git 历史不包含在本仓库中，版本追溯使用锁文件中的固定提交。

额外内核依赖的六个 Git 项目继续固定到原提交，由 `./lilac deps fetch` 下载到 `out/cache/`。Mesa 固定到 25.0.7 的归档 SHA-256，适配补丁仅应用于 `out/` 内的构建树；其 Python 依赖版本和 wheel 哈希见 `configs/mesa-python.lock.json`。

SDK、IPSW、固件和设备读回数据由使用者提供，留在 Git 之外。默认构建不下载这些材料，也不使用上游预编译内核。
