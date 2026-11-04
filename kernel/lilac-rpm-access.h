/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LILAC_RPM_ACCESS_H
#define LILAC_RPM_ACCESS_H
#include "lilac-rpm-wire.h"
/* Allowed writes are exactly the local producer index, the remote consumer
 * index, or a TX FIFO word. RX payload and remote-owned indices are read-only. */
static inline int lilac_rpm_write_offset(const struct lilac_rpm_layout *layout, uint32_t offset)
{
    if (!layout || (offset & 3)) return 0;
    return offset == layout->tx.descriptor + 4 || offset == layout->rx.descriptor ||
        (offset >= layout->tx.data && (uint64_t)offset + 4 <= (uint64_t)layout->tx.data + layout->tx.size);
}
/* Recover only a fault from our single load/store instruction and a mapped
 * register we intentionally access. A bad PC, operand or direction is never
 * a recoverable RPM probe. */
static inline int lilac_rpm_fault_matches(uint64_t pc, uint32_t esr, uint64_t address,
    uint64_t operand, uint64_t load_pc, uint64_t store_pc, uint64_t ram, uint64_t doorbell,
    int write_enabled, const struct lilac_rpm_layout *layout)
{
    if (((esr >> 26) & 63) != 0x25 || operand != address || (address & 3)) return 0;
    int write = (esr & (1u << 6)) != 0;
    if (!write)
        return pc == load_pc && ram && address >= ram && address - ram < 0x7000;
    if (pc != store_pc || !write_enabled) return 0;
    if (doorbell && address == doorbell) return 1;
    return ram && address >= ram && address - ram < 0x7000 &&
        lilac_rpm_write_offset(layout, (uint32_t)(address - ram));
}
#endif
