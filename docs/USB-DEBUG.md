# RAM USB 调试

## 状态与边界

这是 BootShim 返回后日志通道，不是运行中的 XNU USB 驱动。2026-11-03 已在 SO-02K、p114 上完成三种实机闭环：USB 自检、内核定时返回、受控 panic。三次均通过 USB 保存并校验日志，然后直接返回原 Sony Fastboot、复核设备身份；不需要 ADB 或人工按键。验证范围仅限当前固件与单核 RAM 配置。

`bootloader/debug-usb.c` 使用 DWC3 轮询、独立 DMA 缓冲和 Fastboot 格式的 USB bulk 接口。只在显式启用且 XNU 已返回后运行；自检模式不进入 XNU。主机可读取日志、校验并确认接收，随后请求回到 Sony Fastboot。它不提供刷写、擦除、任意内存访问或固件更新命令。当前只接受已核验的 Lilac DTB 和单核启动；没有运行中的实时日志或硬锁死恢复能力。

寄存器与协议参考固定 Linux v6.12 的 DWC3、QUSB2 和 MSM8998 GCC 实现，重启流程参考固定 Sony 内核的 `msm-poweroff.c`、`qpnp-power-on.c` 和 SPMI PMIC arbiter。提交和各文件 SHA-256 在 `upstream.lock.json`；参考文件不是构建时下载依赖。

## 构建和采集

先测试日志传输，不启动 XNU：

```sh
./lilac build ramboot --kernel out/latest/kernel/hakios-kernel --usb-self-test
./lilac device run --build out/latest/ramboot \
  --fastboot /path/to/platform-tools/fastboot \
  --serial BH905S2WB5 --timeout 120 --reboot-bootloader
```

测试内核返回路径：

```sh
./lilac build kernel
./lilac build hardware --kernel out/latest/kernel/hakios-kernel \
  --touch --gpu-snapshot --usb-debug --return-after 60
./lilac device run --build out/latest/hardware \
  --fastboot /path/to/platform-tools/fastboot \
  --serial BH905S2WB5 --timeout 600 --reboot-bootloader
```

`--return-after` 是内核定时返回，不保证死锁时也能返回。省略 `--reboot-bootloader` 时只保存日志。已经运行的调试镜像用 `device collect`，不要重新发送启动命令。构建目录必须保留与镜像匹配的清单和 `SHA256SUMS`。

主机将命令、设备身份、镜像 SHA-256、传输日志和结果写入新的 `out/runs/*-device-run/` 或 `*-device-collect/`。只有 `result.json` 的 `status=log_saved_fastboot_verified` 才表示完整闭环成功；还必须独立检查 `kernel.log` 中内核或触摸的实际结果。

## 校验与重启规则

1. 验证 Sony 的 serial、product、p114 bootloader 和已解锁状态，不改变锁状态。
2. 校验镜像哈希，使用普通 p114 xperable RAM 启动，不使用 readonly survey 或 exploit 准备流程。
3. 等待调试 USB 枚举，核对 product 和构建 `run-id`，读取有界日志长度与 CRC-32。
4. 日志先写入 `.partial`，校验完整性、`fsync` 并发布为 `kernel.log`，保存 SHA-256 回执。
5. 明确请求重启时才发送带 CRC 的 `oem log-ack`，再发 `reboot-bootloader`。设备在本次完整上传与确认之前拒绝重启命令。
6. 检查 SPMI 版本、PON 通道所有权与 PMIC Gen2 类型；仅配置主 PMIC 的暖复位、Sony bootloader 原因和 IMEM 原因，逐项读回，再释放 PS_HOLD。准备失败不复位，并尽力恢复原寄存器值。主机等待原 Sony serial 重新出现并复查身份，不能据发送命令就宣告成功。

损坏日志、run-id 不符、磁盘写入失败、未枚举或超时都保留失败记录，不自动复位、擦除日志或写分区。USB 失败后屏幕保留阶段、最后寄存器和值、事件计数、ESR/FAR。BootShim 原有屏幕日志作为最后后备。重启依赖固件行为；如果系统未按请求返回 Fastboot，主机报告失败，不尝试固件更新来绕过问题。

## 实机记录（2026-11-03）

设备 serial 为 `BH905S2WB5`，product 为 `SO-02K`，bootloader 为 `1306-5035_X_Boot_MSM8998_LA2.0_P_114`。以下目录内的 `result.json` 保存完整命令、镜像哈希与设备身份；`receipt.json` 保存日志长度、CRC-32 与 SHA-256。

| 场景 | 构建目录（`out/runs/`） | 设备报告（`out/runs/`） | 日志字节数 |
| --- | --- | --- | --- |
| 不进入 XNU 的 USB 自检 | `usb-loop-selftest-04` | `20261103T130915.830625Z-device-run` | 68 |
| 触摸故障隔离、90 秒定时返回 | `usb-touch-guard-probe-04` | `20261103T131130.056934Z-device-run` | 20317 |
| 显式 `return_panic=1`、20 秒受控 panic | `usb-panic-loop-01` | `20261103T131811.180937Z-device-run` | 21865 |

第三次日志含 `--- panic text ---` 和 `return_panic: the panic that was asked for`，不是触摸驱动再次崩溃。第二次日志持续输出 `PROBE ALIVE`，随后定时返回，没有 `panic(cpu ...)`；触摸在 QUP `+0x404` 捕获外部访问异常并停止，尚未就绪，详见 [TOUCH.md](TOUCH.md)。

早期照片的 `stage=6 / register=0x0a80c80c / value=0x1006 / last event=0x101` 对应 USB reset 后重复提交 EP0 SETUP、STARTTRANSFER 无资源。复位时保留有效 SETUP；中断中的数据／状态和 bulk 传输通过 ENDTRANSFER 结束。模拟器现在会拒绝重复 START，并覆盖连续 100 次复位。

`usb-loop-selftest-03` 首次成功回传日志，但仅 IMEM + PSCI 复位进入了 Android，随后由主机显式 ADB 返回 Fastboot；报告已标为 `log_saved_adb_assisted_fastboot`，不计入直接重启验收。正式路径不采用该回退，也不调用 ADB。

## 本地主机测试

`./lilac test host` 包含调试协议命令白名单、上传／确认／重启状态、DWC3 资源所有权与复位、MMIO 异常与超时、DTB 资源拒绝检查、PMIC 重启准备逐操作失败注入与恢复，以及主机采集器的内容不符、落盘失败和重启超时检查。模拟测试不代替 PHY、电源域、DMA、USB 枚举和 PS_HOLD 的实机验证。
