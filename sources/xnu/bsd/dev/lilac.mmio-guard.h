#ifndef LILAC_MMIO_GUARD_H
#define LILAC_MMIO_GUARD_H
#include <stdint.h>

static inline int lilac_mmio_matches(uint64_t pc, uint32_t esr, uint64_t address,
    uint64_t operand, uint64_t base, uint64_t load_pc, uint64_t store_pc)
{
    uint64_t expected = (esr & (1u << 6)) ? store_pc : load_pc;
    return pc == expected && address == operand && base && address >= base &&
        address - base < 0x600 && !(address & 3) && ((esr >> 26) & 63) == 0x25;
}

static inline int lilac_gpu_smmu_matches(uint64_t pc, uint32_t esr, uint64_t address,
    uint64_t operand, uint64_t base, uint64_t load_pc)
{
    if (!base || pc != load_pc || ((esr >> 26) & 63) != 0x25 || (esr & (1u << 6)) ||
        address != operand || address < base) return 0;
    uint64_t offset = address - base;
    return offset == 0x20 || offset == 0x24 || offset == 0x28 || offset == 0x3c ||
        offset == 0 || offset == 0x48 ||
        (offset >= 0x800 && offset <= 0x808 && !(offset & 3)) ||
        (offset >= 0xc00 && offset <= 0xc08 && !(offset & 3)) ||
        (offset >= 0x1000 && offset <= 0x1008 && !(offset & 3)) ||
        offset == 0x8000 || offset == 0x8020 || offset == 0x8024 || offset == 0x8030 || offset == 0x8058;
}

static inline int lilac_gpu_rpm_matches(uint64_t pc, uint32_t esr, uint64_t address,
    uint64_t operand, uint64_t base, uint64_t load_pc)
{
    return base && pc == load_pc && ((esr >> 26) & 63) == 0x25 && !(esr & (1u << 6)) &&
        address == operand && address >= base && address - base < 0x7000 && !(address & 3);
}
#endif
