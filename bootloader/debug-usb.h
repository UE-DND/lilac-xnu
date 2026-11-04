#ifndef LILAC_DEBUG_USB_H
#define LILAC_DEBUG_USB_H
#include <stdint.h>
void lilac_debug_usb(const void *log, uint32_t length);
void lilac_debug_display(const char *message, uint32_t value);
void lilac_debug_diagnostics(uint32_t stage, uint32_t address, uint32_t value,
    uint32_t event_count, uint32_t last_event);
int shim_debug_read32(uint64_t address, uint32_t *value);
int shim_debug_write32(uint64_t address, uint32_t value);
extern uint64_t shim_debug_fault_esr, shim_debug_fault_far;
#endif
