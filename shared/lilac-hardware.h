#ifndef LILAC_HARDWARE_H
#define LILAC_HARDWARE_H
#include <stdint.h>

#define LILAC_HARDWARE_VERSION 3u

struct lilac_touch_status {
    uint32_t version, stage, error, controller_version;
    uint32_t state, operational, bus_status, fifo_error;
    uint32_t clock_ahb, clock_core, clock_config, gpio_irq;
    uint32_t reports, events, failures, active;
    uint32_t last_kind, last_slot, last_x, last_y;
    uint32_t f01_status, f01_control, objects, attention;
    uint32_t clock_error, clock_command;
    uint32_t clock_reset, mmio_esr, mmio_offset;
};

struct lilac_gpu_snapshot {
    uint32_t version, available, cx_gdsc, gx_gdsc;
    uint32_t core_clock, timer_clock, isense_clock, clamp;
};

struct lilac_gpu_tz_status {
    uint32_t attempted, convention;
    int32_t probe64, probe32, init, mem_setup, authenticate, supported;
    uint64_t probe64_value, probe32_value, init_value, mem_setup_value, authenticate_value, supported_value;
};
struct lilac_gpu_smmu_status {
    uint32_t ahb, attempted, readable, idr0, idr1, esr, fault_offset;
};
#endif
