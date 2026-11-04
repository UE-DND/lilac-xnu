# 第三方源码与许可

本项目将四个核心上游项目的源码快照以固定提交基线直接纳入仓库管理。相关的提交哈希、上游仓库地址、排除清单、源码归属及符号链接定义统一由根目录下的 [upstream.lock.json](../upstream.lock.json) 记录。

| 项目 | 上游基线 | 当前源码 |
| --- | --- | --- |
| HakiOS | `a48d2b29dd58da5ef322052a7f4f73d7eb8eebc4` | `sources/hakios/` |
| Apple XNU | `c54f35ca767986246321eb901baf8f5ff7923f6a`，`xnu-8796.121.2` | `sources/xnu/` |
| xperable | `0bf7060a966f91ed6251386bb6281648e416a03a` | `sources/xperable/` |
| pe-parse | `31ac5966503689d5693cd9fb520bd525a8710e17` | `third_party/pe-parse/` |
| Apple HFS | `31d5ec238dfab485e78a9a30b8b92d13e8914f03`，`hfs-627.100.6` | XNU 的 `bsd/hfs/`、`bsd/hfs_encodings/` |

## 源码整合与维护机制

`sources/xnu/` 是内核构建的权威源码目录，整合了 Apple 原始 XNU、HakiOS、HFS 以及本项目对 MSM8998 与 Xperia XZ1 Compact 的适配补丁。`sources/hakios/` 仅保留构建辅助工具与驱动配置元数据，不重复维护内核实现代码。

各子目录完整保留了上游项目的 LICENSE、NOTICE、第三方许可文件及头部版权声明：
- `sources/xnu/` 遵循 Apple Public Source License（APSL 2.0）。
- APFS 模块中的 libdeflate 遵循 MIT 许可证（详见 `sources/xnu/bsd/miscfs/apfs/libdeflate/COPYING`）。
- HFS 模块保留原始版权声明并受 APSL 约束。
- `third_party/pe-parse/` 遵循 MIT 许可证。
- 本项目整体未设置统一的顶层许可证，各模块分别受其对应开源许可证约束。

## 外部依赖与离线构建

内核构建所需的额外依赖项（如 libdispatch、libpthread、AvailabilityVersions 等）在 `upstream.lock.json` 中锁定了具体的提交哈希，并在执行 `./lilac deps fetch` 时下载至本地 `out/cache/` 目录。

Mesa 图形库固定至 25.0.7 归档版本，其构建补丁由工具链在编译期动态应用于 `out/` 工作目录，其 Python 依赖项与二进制 wheel 哈希记录于 `configs/mesa-python.lock.json`。

所有专有系统材料（SDK、IPSW 镜像、Sony 固件二进制以及特定设备导出的物理映射数据）均由使用者自行准备，不随代码仓库分发。
