#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <arm/thread.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.input.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include "lilac-hardware.h"
#include "lilac-qup-clock.h"
#include "lilac-qup-i2c.h"
#include "lilac-clearpad-rmi.h"
#include "lilac-clearpad-bus.h"
#include "lilac-touch-reset.h"
#include "lilac.mmio-guard.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern uint64_t kvtophys(vm_offset_t address);
extern kern_return_t thread_terminate(thread_t thread);
void lilac_touch_init(void);
bool lilac_touch_abort(arm_saved_state_t *state, uint32_t esr, vm_offset_t address);

extern int lilac_touch_read32(vm_offset_t address, uint32_t *value);
extern int lilac_touch_write32(vm_offset_t address, uint32_t value);
extern void lilac_touch_fault_load(void), lilac_touch_fault_store(void), lilac_touch_fault_return(void);
__asm__(".text\n"
    ".globl _lilac_touch_read32, _lilac_touch_fault_load, _lilac_touch_fault_return\n"
    "_lilac_touch_read32:\n"
    "_lilac_touch_fault_load: ldr w2, [x0]\n"
    "dsb sy\nstr w2, [x1]\nmov w0, #1\nret\n"
    ".globl _lilac_touch_write32, _lilac_touch_fault_store\n"
    "_lilac_touch_write32:\n"
    "_lilac_touch_fault_store: str w1, [x0]\n"
    "dsb sy\nmov w0, #1\nret\n"
    "_lilac_touch_fault_return: mov w0, #0\nret\n");

static vm_offset_t touch_gcc, touch_qup, touch_sda, touch_scl, touch_irq;
static vm_offset_t touch_reset, touch_power;
static struct lilac_touch_status touch_status = {.version = LILAC_HARDWARE_VERSION};
static LCK_GRP_DECLARE(touch_status_group, "lilac-clearpad");
static LCK_MTX_DECLARE(touch_status_lock, &touch_status_group);
static uint32_t touch_fault_esr;
static vm_offset_t touch_fault_address;

bool lilac_touch_abort(arm_saved_state_t *state, uint32_t esr, vm_offset_t address)
{
    if (!lilac_mmio_matches(get_saved_state_pc(state), esr, address,
        get_saved_state_reg(state, 0), touch_qup, (uintptr_t)lilac_touch_fault_load,
        (uintptr_t)lilac_touch_fault_store)) return false;
    touch_fault_esr = esr;
    touch_fault_address = address;
    set_saved_state_pc(state, (uintptr_t)lilac_touch_fault_return);
    return true;
}

static uint32_t touch_read_register(vm_offset_t address)
{
    uint32_t value = *(volatile uint32_t *)address;
    __asm__ volatile("dsb sy" ::: "memory");
    return value;
}

static void touch_write_register(vm_offset_t address, uint32_t value)
{
    *(volatile uint32_t *)address = value;
    __asm__ volatile("dsb sy" ::: "memory");
}

static uint32_t touch_qup_read(__unused void *context, uint32_t offset)
{
    uint32_t value = 0;
    if (!touch_fault_esr) lilac_touch_read32(touch_qup + offset, &value);
    return value;
}

static void touch_qup_write(__unused void *context, uint32_t offset, uint32_t value)
{
    if (!touch_fault_esr) lilac_touch_write32(touch_qup + offset, value);
}

static void touch_delay(__unused void *context, uint32_t microseconds)
{
    delay_for_interval(microseconds, NSEC_PER_USEC);
}

static uint32_t touch_reset_read(__unused void *context, unsigned offset)
{
    return touch_read_register((offset < 2 ? touch_reset : touch_power) + (offset & 1) * 4);
}

static void touch_reset_write(__unused void *context, unsigned offset, uint32_t value)
{
    touch_write_register((offset < 2 ? touch_reset : touch_power) + (offset & 1) * 4, value);
}

static struct lilac_qup touch_controller = {
    .read = touch_qup_read, .write = touch_qup_write,
    .delay_us = touch_delay, .clock = 0x35d
};

static int touch_transfer(void *context, uint8_t address, const uint8_t *write_data,
    size_t write_bytes, uint8_t *read_data, size_t read_bytes)
{
    int error = lilac_qup_transfer(context, address, write_data, write_bytes, read_data, read_bytes);
    if (error) printf("lilac touch: I2C failed reg=%x write=%lu read=%lu error=%d bus=%x op=%x\n",
        write_bytes ? write_data[0] : 0, write_bytes, read_bytes, error,
        touch_controller.bus_error, touch_controller.operational);
    return error;
}

static int touch_status_sysctl(__unused struct sysctl_oid *oidp,
    __unused void *arg1, __unused int arg2, struct sysctl_req *request)
{
    struct lilac_touch_status snapshot;
    lck_mtx_lock(&touch_status_lock);
    snapshot = touch_status;
    lck_mtx_unlock(&touch_status_lock);
    return SYSCTL_OUT(request, &snapshot, sizeof(snapshot));
}
SYSCTL_PROC(_kern, OID_AUTO, lilac_touch_status, CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_LOCKED,
    0, 0, touch_status_sysctl, "S,lilac_touch_status", "ClearPad hardware acquisition status");

static uint32_t touch_clock_read(__unused void *context, uint32_t offset)
{
    return touch_read_register(touch_gcc + offset);
}

static void touch_clock_write(__unused void *context, uint32_t offset, uint32_t value)
{
    touch_write_register(touch_gcc + offset, value);
}

static void touch_capture(struct lilac_touch_status *status)
{
    status->clock_ahb = touch_read_register(touch_gcc + LILAC_QUP_CLOCK_AHB);
    status->clock_core = touch_read_register(touch_gcc + LILAC_QUP_CLOCK_CORE);
    status->clock_config = touch_read_register(touch_gcc + LILAC_QUP_CLOCK_CONFIG);
    status->clock_command = touch_read_register(touch_gcc + LILAC_QUP_CLOCK_COMMAND);
    status->clock_reset = touch_read_register(touch_gcc + LILAC_QUP_CLOCK_RESET);
    status->mmio_esr = touch_fault_esr;
    status->mmio_offset = touch_fault_esr ? touch_fault_address - touch_qup : 0;
    status->gpio_irq = touch_read_register(touch_irq + 4) & 1;
    status->error = touch_controller.error;
    status->state = touch_controller.state;
    status->operational = touch_controller.operational;
    status->bus_status = touch_controller.bus_error;
    status->fifo_error = touch_controller.fifo_error;
}

static void touch_publish(const struct lilac_touch_status *status)
{
    lck_mtx_lock(&touch_status_lock);
    touch_status = *status;
    lck_mtx_unlock(&touch_status_lock);
}

static void touch_release(struct lilac_rmi_device *device)
{
    for (unsigned slot = 0; slot < LILAC_F12_SLOTS; slot++) {
        if (device->fingers.slot[slot].active)
            hakios_touch_post(HAKIOS_TOUCH_UP, slot,
                device->fingers.slot[slot].x * 65535u / 719,
                device->fingers.slot[slot].y * 65535u / 1279);
    }
    memset(&device->fingers, 0, sizeof(device->fingers));
}

static void touch_worker(__unused void *parameter, __unused wait_result_t result)
{
    struct lilac_touch_status status = {.version = LILAC_HARDWARE_VERSION, .stage = 1};
    const struct lilac_qup_clock clock = {
        .read = touch_clock_read, .write = touch_clock_write, .delay_us = touch_delay
    };
    struct lilac_clearpad_bus bus = {.xfer = touch_transfer, .ctx = &touch_controller};
    struct lilac_rmi_device device = {0};
    unsigned failures = 0;
    touch_gcc = ml_io_map(0x00100000, 0xb0000);
    touch_qup = ml_io_map(0x0c179000, 0x1000);
    touch_sda = ml_io_map(0x03d57000, 0x1000);
    touch_scl = ml_io_map(0x03d58000, 0x1000);
    touch_irq = ml_io_map(0x03d7d000, 0x1000);
    touch_reset = ml_io_map(0x03d59000, 0x1000);
    touch_power = ml_io_map(0x03585000, 0x1000);
    if (!touch_gcc || !touch_qup || !touch_sda || !touch_scl || !touch_irq || !touch_reset || !touch_power) {
        status.stage = 101; goto finished;
    }
    status.stage = 2;
    printf("lilac touch: mapped QUP=%llx expected=c179000 GCC=%llx; BLSP1 reset=%x sleep=%x vote=%x\n",
        (uint64_t)kvtophys(touch_qup), (uint64_t)kvtophys(touch_gcc),
        touch_read_register(touch_gcc + 0x17000), touch_read_register(touch_gcc + 0x17008),
        touch_read_register(touch_gcc + LILAC_QUP_CLOCK_VOTE));
    status.clock_error = lilac_qup_clock_enable(&clock);
    if (status.clock_error) { status.stage = 102; goto capture; }
    status.clock_reset = touch_read_register(touch_gcc + LILAC_QUP_CLOCK_RESET);
    if (status.clock_reset & 1) {
        touch_write_register(touch_gcc + LILAC_QUP_CLOCK_RESET, status.clock_reset & ~1u);
        touch_delay(NULL, 100);
        if (touch_read_register(touch_gcc + LILAC_QUP_CLOCK_RESET) & 1) { status.stage = 108; goto capture; }
    }
    status.state = touch_qup_read(NULL, LQ_STATE);
    printf("lilac touch: before QUP reset state=%x ESR=%x vote=%x\n", status.state, touch_fault_esr,
        touch_read_register(touch_gcc + LILAC_QUP_CLOCK_VOTE));
    if (touch_fault_esr) { status.stage = 107; goto capture; }
    if (!lilac_qup_initialize(&touch_controller)) { status.stage = 109; goto capture; }
    status.controller_version = touch_qup_read(NULL, LQ_VERSION);
    if (touch_fault_esr) { status.stage = 107; goto capture; }
    printf("lilac touch: pins reset89=%x/%x vddio133=%x/%x irq125=%x/%x sda87=%x/%x scl88=%x/%x\n",
        touch_read_register(touch_reset), touch_read_register(touch_reset + 4),
        touch_read_register(touch_power), touch_read_register(touch_power + 4),
        touch_read_register(touch_irq), touch_read_register(touch_irq + 4),
        touch_read_register(touch_sda), touch_read_register(touch_sda + 4),
        touch_read_register(touch_scl), touch_read_register(touch_scl + 4));
    const struct lilac_touch_reset_io reset_io = {
        .read = touch_reset_read, .write = touch_reset_write, .delay_us = touch_delay
    };
    int reset_error = lilac_touch_release_reset(&reset_io);
    printf("lilac touch: release reset result=%d reset89=%x irq125=%x\n", reset_error,
        touch_read_register(touch_reset + 4), touch_read_register(touch_irq + 4));
    if (reset_error) { status.stage = reset_error; goto capture; }
    for (unsigned pin = 0; pin < 2; pin++) {
        vm_offset_t address = pin ? touch_scl : touch_sda;
        uint32_t config = touch_read_register(address);
        touch_write_register(address, (config & ~0x3ffu) | (3u << 2));
    }
    status.stage = 3;
    touch_capture(&status); touch_publish(&status);
    if (!lilac_rmi_discover(lilac_clearpad_read, &bus, &device)) {
        status.stage = 103; goto capture;
    }
    status.objects = device.objects;
    status.attention = device.attention;
    uint16_t f01_data = (uint16_t)((unsigned)device.f01.page << 8 | device.f01.data);
    uint16_t f01_control = (uint16_t)((unsigned)device.f01.page << 8 | device.f01.control);
    uint8_t sensor_status, sensor_control;
    if (lilac_clearpad_read(&bus, f01_data, &sensor_status, 1) ||
        lilac_clearpad_read(&bus, f01_control, &sensor_control, 1) ||
        (sensor_status & 0x40) || (sensor_status & 15) > 1) {
        status.stage = 104; goto capture;
    }
    status.f01_status = sensor_status;
    uint8_t page[] = {255, device.f01.page};
    uint8_t wake[] = {device.f01.control, (uint8_t)((sensor_control & ~3u) | 0x80)};
    printf("lilac touch: F01 control=%x data=%x status=%x old=%x target=%x F12 objects=%x attention=%x\n",
        f01_control, f01_data, sensor_status, sensor_control, wake[1], device.objects, device.attention);
    if (touch_transfer(&touch_controller, 0x2c, page, 2, NULL, 0) ||
        touch_transfer(&touch_controller, 0x2c, wake, 2, NULL, 0)) {
        status.stage = 105; goto capture;
    }
    touch_delay(NULL, 20000);
    if (lilac_clearpad_read(&bus, f01_control, &sensor_control, 1) ||
        lilac_clearpad_read(&bus, f01_data, &sensor_status, 1)) {
        status.stage = 105; goto capture;
    }
    printf("lilac touch: F01 wake readback=%x requested=%x status=%x\n", sensor_control, wake[1], sensor_status);
    if (!lilac_rmi_configured(sensor_control, wake[1], sensor_status)) {
        status.stage = 105; goto capture;
    }
    status.f01_status = sensor_status;
    status.f01_control = sensor_control;
    status.stage = 4;
    printf("lilac touch: S3330 F12 acquisition ready, polling 8 ms; no firmware update\n");
    for (;;) {
        struct lilac_f12_event events[LILAC_F12_SLOTS];
        size_t count = 0;
        uint8_t interrupts;
        if (lilac_clearpad_read(&bus, (uint16_t)(f01_data + 1), &interrupts, 1) ||
            !lilac_rmi_poll(lilac_clearpad_read, &bus, &device, events, LILAC_F12_SLOTS, &count)) {
            status.failures++;
            if (++failures >= 5 || touch_controller.error) {
                touch_release(&device); status.active = 0;
                status.stage = 106; goto capture;
            }
        } else {
            failures = 0;
            status.reports++;
            for (size_t index = 0; index < count; index++) {
                const struct lilac_f12_event *event = &events[index];
                hakios_touch_post(event->kind, event->slot,
                    (uint32_t)event->x * 65535u / 719, (uint32_t)event->y * 65535u / 1279);
                status.events++;
                status.last_kind = event->kind; status.last_slot = event->slot;
                status.last_x = event->x; status.last_y = event->y;
            }
            status.active = 0;
            for (unsigned slot = 0; slot < LILAC_F12_SLOTS; slot++)
                if (device.fingers.slot[slot].active) status.active |= 1u << slot;
        }
        touch_capture(&status); touch_publish(&status);
        delay_for_interval(8, NSEC_PER_MSEC);
    }
capture:
    if (touch_fault_esr) status.stage = 107;
    touch_capture(&status);
finished:
    touch_publish(&status);
    printf("lilac touch: stopped stage=%u error=%u clock=%u (%s) bus=%x state=%x\n",
        status.stage, status.error, status.clock_error,
        lilac_qup_clock_error_name(status.clock_error), status.bus_status, status.state);
    if (touch_fault_esr)
        printf("lilac touch: QUP access refused offset=%x ESR=%x BCR=%x; no more QUP accesses\n",
            status.mmio_offset, status.mmio_esr, status.clock_reset);
    thread_terminate(current_thread());
}

void lilac_touch_init(void)
{
    uint32_t enabled = 0;
    thread_t worker;
    if (!PE_parse_boot_argn("lilac_touch", &enabled, sizeof(enabled)) || enabled != 1) return;
    if (kernel_thread_start(touch_worker, NULL, &worker) == KERN_SUCCESS) thread_deallocate(worker);
}
