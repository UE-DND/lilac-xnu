# 工具目录

- 本目录：独立运行的分析、镜像和存储准备工具，以及 A540 固件检查模块。
- `lilaclib/`：`./lilac` 使用的构建、配置和测试编排模块。
- `tests/`：Python 和 C 测试；通过 `./lilac test host` 或 `./lilac test integration` 运行。集成测试可能需要本地材料或构建产物。

新增测试时使用 `test-名称.py` 或 `test-名称.c` 命名；集成测试还需在 `lilaclib/testing.py` 中登记。

`./lilac device run|collect` 是实验中的 RAM USB 日志采集器；必须传入构建、Fastboot 路径和设备序列号。只有明确指定 `--reboot-bootloader` 才在日志验证并落盘后请求重启。协议、限制和实机验证状态见 `docs/USB-DEBUG.md`。

`python3 tools/check-a540-run.py <device-run目录>` 校验 A540 真机日志中的认证、渲染像素、持续桌面显示、最终故障状态和清理；USB 回收成功或 CP 自检不单独算 GPU 验收通过。
