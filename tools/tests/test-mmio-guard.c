#include <assert.h>
#include <stdio.h>
#include "../../sources/xnu/bsd/dev/lilac.mmio-guard.h"

int main(void)
{
    const uint64_t base = 0xfffffff110000000ull, load = 0xfffffff007800000ull, store = load + 32;
    for (unsigned ec = 0; ec < 64; ec++) {
        for (unsigned write = 0; write < 2; write++) {
            uint32_t esr = (ec << 26) | (write << 6) | 0x10;
            for (unsigned offset = 0; offset <= 0x600; offset++) {
                uint64_t pc = write ? store : load;
                assert(lilac_mmio_matches(pc, esr, base + offset, base + offset, base, load, store) ==
                    (ec == 0x25 && offset < 0x600 && !(offset & 3)));
                assert(!lilac_mmio_matches(pc + 4, esr, base + offset, base + offset, base, load, store));
                assert(!lilac_mmio_matches(pc, esr, base + offset, base + offset + 4, base, load, store));
            }
        }
    }
    assert(!lilac_mmio_matches(load, 0x94000010, base - 4, base - 4, base, load, store));
    assert(!lilac_mmio_matches(load, 0x94000010, 0, 0, 0, load, store));
    for (unsigned ec = 0; ec < 64; ec++) for (unsigned write = 0; write < 2; write++)
        for (unsigned offset = 0; offset <= 0x28; offset++) {
            uint32_t esr = (ec << 26) | (write << 6) | 0x10;
            int expected = ec == 0x25 && !write && (offset == 0 || offset == 0x20 || offset == 0x24 || offset == 0x28);
            assert(lilac_gpu_smmu_matches(load, esr, base + offset, base + offset, base, load) == expected);
            assert(!lilac_gpu_smmu_matches(load + 4, esr, base + offset, base + offset, base, load));
            assert(!lilac_gpu_smmu_matches(load, esr, base + offset, base + offset + 4, base, load));
        }
    const unsigned allowed[] = {0x3c, 0x48, 0x800, 0x804, 0x808, 0xc00, 0xc04, 0xc08,
        0x1000, 0x1004, 0x1008, 0x8000, 0x8020, 0x8024, 0x8030, 0x8058};
    for (unsigned i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++)
        assert(lilac_gpu_smmu_matches(load, 0x94000010, base + allowed[i], base + allowed[i], base, load));
    assert(!lilac_gpu_smmu_matches(load, 0x94000010, base + 0x9000, base + 0x9000, base, load));
    for (unsigned offset = 0; offset <= 0x7000; offset++) {
        assert(lilac_gpu_rpm_matches(load, 0x94000010, base + offset, base + offset, base, load) ==
            (offset < 0x7000 && !(offset & 3)));
        assert(!lilac_gpu_rpm_matches(load, 0x94000050, base + offset, base + offset, base, load));
        assert(!lilac_gpu_rpm_matches(load + 4, 0x94000010, base + offset, base + offset, base, load));
    }
    puts("PASS: bounded QUP, SMMU and read-only RPM fault guards");
}
