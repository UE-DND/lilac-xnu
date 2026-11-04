#include "mini.h"
#include "../shared/lilac-hardware.h"
#include "../kernel/lilac-qup-clock.h"
#include "../kernel/lilac-a540-cx.h"

void bzero(void *address, size_t bytes)
{
    memset(address, 0, bytes);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc; (void)argv; (void)envp;
    int console = open("/dev/console", O_RDWR, 0);
    if (console < 0) return 90;
    for (int descriptor = 0; descriptor < 3; descriptor++)
        if (dup2(console, descriptor) < 0) return 91;
    if (console > 2) close(console);
    unsigned previous_stage = ~0u, previous_events = ~0u;
    unsigned samples = 0;
    struct lilac_gpu_snapshot gpu = {0};
    size_t bytes = sizeof(gpu);
    puts("LILAC HARDWARE BRING-UP: RAM only, no firmware update");
    if (!sysctlbyname("kern.lilac_gpu_snapshot", &gpu, &bytes, NULL, 0) &&
        bytes == sizeof(gpu) && gpu.version == LILAC_HARDWARE_VERSION) {
        printf("GPU snapshot available=%u CX=%x GX=%x\n", gpu.available, gpu.cx_gdsc, gpu.gx_gdsc);
        printf("GPU core=%x timer=%x isense=%x clamp=%x\n",
            gpu.core_clock, gpu.timer_clock, gpu.isense_clock, gpu.clamp);
    }
    struct lilac_a540_cx_result cx = {0};
    bytes = sizeof(cx);
    if (!sysctlbyname("kern.lilac_gpu_cx_result", &cx, &bytes, NULL, 0) && bytes == sizeof(cx))
        printf("GPU CX result=%u before=%x hw=%x voted=%x on=%x restored=%x after=%x\n",
            cx.result, cx.before, cx.status_before, cx.voted, cx.status_on, cx.restored, cx.status_after);
    struct lilac_gpu_tz_status tz = {0}; bytes = sizeof(tz);
    if (!sysctlbyname("kern.lilac_gpu_tz_status", &tz, &bytes, NULL, 0) && bytes == sizeof(tz) && tz.attempted)
        printf("GPU TZ ABI=%u probe64=%d/%llu probe32=%d/%llu init=%d/%llu mem=%d/%llu auth=%d/%llu supported=%d/%llu\n",
            tz.convention, tz.probe64, tz.probe64_value, tz.probe32, tz.probe32_value,
            tz.init, tz.init_value, tz.mem_setup, tz.mem_setup_value, tz.authenticate, tz.authenticate_value,
            tz.supported, tz.supported_value);
    struct lilac_gpu_smmu_status smmu = {0}; bytes = sizeof(smmu);
    if (!sysctlbyname("kern.lilac_gpu_smmu_status", &smmu, &bytes, NULL, 0) && bytes == sizeof(smmu))
        printf("GPU SMMU ahb=%x attempted=%u readable=%u id=%x/%x ESR=%x offset=%x\n",
            smmu.ahb, smmu.attempted, smmu.readable, smmu.idr0, smmu.idr1, smmu.esr, smmu.fault_offset);
    puts("GPU acceleration is NOT enabled by this probe.");
    puts("This is the text test page; it will NOT open the desktop.");
    for (;;) {
        struct lilac_touch_status touch = {0};
        bytes = sizeof(touch);
        if (sysctlbyname("kern.lilac_touch_status", &touch, &bytes, NULL, 0) ||
            bytes != sizeof(touch) || touch.version != LILAC_HARDWARE_VERSION) {
            puts("touch status unavailable");
            usleep(1000000); continue;
        }
        if (touch.stage != previous_stage || touch.events != previous_events) {
            printf("TOUCH stage=%u error=%u ver=%x polls=%u events=%u\n",
                touch.stage, touch.error, touch.controller_version, touch.reports, touch.events);
            printf("  clk=%x/%x cfg=%x cmd=%x irq=%u qup=%x bus=%x fifo=%x\n",
                touch.clock_ahb, touch.clock_core, touch.clock_config, touch.clock_command, touch.gpio_irq,
                touch.state, touch.bus_status, touch.fifo_error);
            printf("  mask=%x type=%u slot=%u x=%u y=%u failures=%u\n",
                touch.active, touch.last_kind, touch.last_slot, touch.last_x, touch.last_y, touch.failures);
            if (touch.stage != previous_stage) {
                if (touch.stage >= 101) {
                    puts("TOUCH STOPPED: waiting or tapping will NOT retry initialization.");
                    if (touch.stage == 107)
                        printf("  MMIO access blocked: offset=%x ESR=%x reset=%x; no further QUP access\n",
                            touch.mmio_offset, touch.mmio_esr, touch.clock_reset);
                    if (touch.stage == 102)
                        printf("  clock error=%u: %s\n", touch.clock_error,
                            lilac_qup_clock_error_name(touch.clock_error));
                } else if (touch.stage == 4) {
                    puts("TOUCH READY: tap, drag, release; USB captures the event log after return.");
                } else if (touch.stage == 0) {
                    puts("TOUCH DISABLED for this boot.");
                }
            }
            previous_stage = touch.stage; previous_events = touch.events;
        }
        if (++samples % 50 == 0)
            printf("PROBE ALIVE sample=%u touch=%s stage=%u polls=%u events=%u\n", samples,
                touch.stage >= 101 ? "STOPPED" : touch.stage == 4 ? "READY" : "NOT READY",
                touch.stage, touch.reports, touch.events);
        usleep(100000);
    }
}
