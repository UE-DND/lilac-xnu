# The kernel

The fixed XNU `xnu-8796.121.2` source, including HakiOS and lilac adaptations,
is maintained directly in `sources/xnu/` at the lilac-xnu root. Driver metadata
remains in `sources/hakios/drivers/`; implementation files are not mirrored.
The root `upstream.lock.json` records origins and required shared source links.

## Build

Run from the lilac-xnu root after registering local SDK inputs:

```sh
./lilac deps fetch
./lilac build kernel
```

The kernel is published as `out/latest/kernel/hakios-kernel` only after a
successful build and source-mutation check. Dependencies, generated driver
configuration and objects live in `out/`. Edit `configs/lilac/kernel.config`
for driver selection; changing exported headers rebuilds dependent libraries.
Other HakiOS documents describe upstream hardware and are historical context,
not lilac build or device-operation instructions. See the root README for
macOS and Linux prerequisites and actual validation status.

## What is added

| Files | What |
|---|---|
| `osfmk/arm64/gic_v3_irq.c`, `generic_arm64_support.c`, `psci_smp.c`, `loader_return.c` | the platform: GICv3, the timer, several CPUs through PSCI, the return to the loader with the log, the hypervisor watchdog |
| `osfmk/minicrypto/` | a replacement for corecrypto (MD5, SHA, HMAC, DRBG, AES) |
| `iokit/Kernel/GenericARMPE.cpp`, `MiniIOSurface.*`, `MiniIOMobileFramebuffer.cpp`, `MiniAppleKeyStore.cpp` | the platform expert and replacements for closed drivers: surfaces, the display, the key store |
| `bsd/miscfs/apfs/`, `bsd/miscfs/ramfs/` | read-only APFS and a writable RAM file system |
| `bsd/kern/kern_minimac.c`, `libkern/amfi/` | AMFI and Sandbox stubs |
| `bsd/dev/hakios.input.c`, `hakios.goodix.c` | input: `/dev/hakios-touch`, the Goodix GT9916 touch controller |
| `bsd/dev/hakios.cpufreq.c` | CPU clocks (Qualcomm EPSS) |
| `bsd/dev/hakios.net.c`, `hakios.virtio_net.c` | the `en0` network interface |
| `bsd/dev/hakios.adreno.c`, `iokit/Kernel/hakios.gpu.cpp` | the Adreno 642L GPU |
| `bsd/dev/hakios.wifi.c`, `hakios.wlfw.c` | Wi-Fi (WCN6750): shared memory, the radio's DSP, the driver |
| `bsd/dev/hakios.qmi.c`, `hakios.nas.c` | QMI and the NAS client |
| `bsd/dev/hakios.mpss.c` | the modem: firmware, file systems, IPA, SIM, registration |
| `bsd/dev/hakios.audio.c`, `hakios.adsp.c`, `hakios.speaker.c`, `hakios.mic.c` | sound through the audio DSP; the battery comes through it too |
| `bsd/dev/hakios.haptic.c`, `hakios.camera.c`, `hakios.ufs.c` | the vibration motor, the cameras, the UFS storage |
| `bsd/pthread/pthread_kext_static.c` | the pthread kext linked into the kernel |

The port also fixes what keeps open XNU from building and running outside Apple: the makefiles
on Linux, the `BCM2837` configuration it is based on, bugs in abandoned code.

License: a derivative work of XNU, Apple Public Source License 2.0 (`LICENSE.APSL` at the top).
