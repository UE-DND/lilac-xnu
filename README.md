# lilac-xnu

将 XNU 和 Darwin 移植到 Sony Xperia XZ1 Compact（`lilac`，MSM8998）的实验项目。项目包含 BootShim、只读 UFS、软件显示及用户空间诊断。原版 Apple launchd、部分 Foundation/IOSurface 检查和 S3330 多点触摸采集已在设备运行；A540 的 Mesa shader/纹理像素及 GPU 桌面合成现已通过真机验证（固定低频、单环）；桌面触摸端到端验收、持久可写数据卷及完整系统服务尚未完成，见[项目状态](docs/STATUS.md)。

## 源码与环境

XNU、HakiOS、xperable 和 pe-parse 的固定源码直接包含在仓库内。检出后不需要子模块或源码补丁步骤，可以直接修改和提交源码。版本、来源及许可证见 [upstream.lock.json](upstream.lock.json) 和[第三方源码](docs/SOURCES.md)。

统一入口为 `./lilac`，需要 Python 3.9 或以上版本。主机构建需要 Git、Clang、Make、CMake、pkg-config、OpenSSL 和 libusb。macOS 需要 Xcode；BootShim 需要 LLVM 的 Clang、LLD、llvm-objcopy 和 llvm-nm。Linux 还需要 flex、bison、m4、autoconf、automake、cctools/Theos。iOS 解包需要支持 APFS 的 7-Zip 23 或以上版本。

SDK、IPSW、Sony 固件、设备 DTB 和物理区间映射由使用者提供，不随源码分发。在 `local/config.json` 登记路径，格式见 [configs/local.example.json](configs/local.example.json)。显式命令参数优先于本地配置，本地配置优先于默认值。Linux 可增加 `theos` 路径，默认使用 `~/theos`。

```sh
./lilac sources check
./lilac doctor
./lilac deps fetch
./lilac deps build
```

`deps fetch` 只获取锁定的额外开源依赖；`--offline` 检查已有缓存。构建阶段不下载缺失依赖或 SDK。依赖获取和构建分开，便于离线使用。

`doctor` 默认检查通用主机条件，同时列出各目标缺失项；`doctor --target kernel` 等命令将指定目标缺少的材料或工具视为失败。普通构建不要求工作树干净。工作区写操作互斥，避免多个构建争用依赖和缓存。

将 SDK、IPSW 和设备材料放在 `local/`，或在 `local/config.json` 登记已有的独立材料路径。构建工具不会自动寻找其他工作区的 SDK 或预编译产物。第三方子目录保留上游参考资料，本项目受支持的构建和设备操作以本 README 为准。

## 构建

```sh
./lilac build kernel
./lilac build ramboot
./lilac inputs ios --ipsw /path/to/iOS-16.5.ipsw --unpack
./lilac build userspace
./lilac build system
./lilac build xperable
```

`build ramboot`、`build userspace` 默认使用最近一次成功构建的内核，也可用 `--kernel PATH` 指定。各目标支持 `--output PATH`，指定的目录必须不存在；默认在 `out/runs/` 下创建独立目录。仅成功构建会更新 `out/latest/<目标>`。

移动运行时使用已有设备专用映射：

```sh
./lilac build userspace \
  --ufs-kernel-probe --ufs-system-map configs/lilac/system-map.bin \
  --mobile-runtime
```

保留 `--dtb`、`--gpu-firmware-dir`、`--mesa-library`、`--desktop-attempt`、`--fast-boot` 和 `--smp` 等实验选项。完整参数见 `./lilac build userspace --help` 和 `./lilac build ramboot --help`。系统材料和区间映射的要求见[用户空间](docs/USERLAND.md)及 [UFS 系统镜像](docs/UFS-SYSTEM.md)。

可选 Mesa 构建：

```sh
./lilac deps fetch --with-mesa
./lilac build mesa
```

Mesa 的 Python 依赖、Meson 和 Ninja 使用锁定版本并校验 wheel 哈希，只安装到 `out/` 下的独立环境。还需本机提供 Bison 3 或更新版本、flex 及 LLVM 工具；macOS 可用 `PATH="$(brew --prefix bison)/bin:$PATH" ./lilac build mesa` 选择已安装的新版 Bison。Mesa 不承诺逐字节复现。

下载工具位于 `out/latest/xperable/xperable`，默认编译 p114 目标。使用 `--loader /path/to/LinuxLoader-p114.pe` 将已提取且与设备 ABL 匹配的模块复制到产物目录；构建工具不下载固件，也不自动连接设备。原始协议参数见其源码目录中的 `README.rst`。

## RAM USB 调试

```sh
./lilac build hardware --kernel out/latest/kernel/hakios-kernel \
  --touch --gpu-snapshot --usb-debug --return-after 90
./lilac device run --build out/latest/hardware \
  --fastboot /path/to/platform-tools/fastboot --serial BH905S2WB5 \
  --timeout 600 --reboot-bootloader
```

该目标是文字诊断页，不启动桌面。内核定时返回或 panic 后，通过 USB 保存并校验日志，再返回 Sony Fastboot；省略 `--reboot-bootloader` 则只采集。已在当前 p114 固件验证，不提供内核运行中的实时日志或硬锁死恢复，不刷写分区。USB 自检、现有镜像采集和验收记录见 [RAM USB 调试](docs/USB-DEBUG.md)。

## 测试与目录

```sh
./lilac test host
./lilac build userspace --ufs-kernel-probe \
  --ufs-system-map configs/lilac/system-map.bin \
  --mobile-runtime --desktop-attempt --fast-boot
./lilac test integration --build out/latest/userspace
```

主机组不需要手机或真实系统材料；集成组需要本地 SDK、字体、GPU 固件及指定构建产物。缺失条件明确记为跳过，不能算通过。报告写入 `out/runs/`。主机测试通过不代表实机启动通过。

| 路径 | 维护内容 |
| --- | --- |
| `sources/xnu/` | 实际参与编译的完整内核 |
| `sources/hakios/` | 构建支持、APFS 工具和用户空间源码 |
| `sources/xperable/`、`third_party/pe-parse/` | 下载工具和 PE 解析器 |
| `bootloader/`、`kernel/`、`userland/` | lilac 适配和诊断代码 |
| `shared/` | 共享接口和 UFS 传输实现；固定路径通过相对链接引用 |
| `configs/`、`tools/lilaclib/` | 可提交配置、路径解析和构建编排 |
| `local/` | 忽略的个人配置及输入材料 |
| `out/` | 忽略的缓存、工具链、生成配置、日志和产物 |

构建入口检查源码在执行前后是否变化，失败时也检查。`./lilac clean` 仅清理构建缓存，保留本地输入、依赖和历史成功产物。验证范围与实机结果见[验证记录](docs/VALIDATION.md)。

贡献前请阅读[贡献指南](CONTRIBUTING.md)；其他说明见[文档目录](docs/README.md)。
