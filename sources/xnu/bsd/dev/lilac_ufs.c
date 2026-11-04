/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Native read-only UFS 2.1 + GPT block/character storage driver for Sony
 * Xperia XZ1 Compact (MSM8998 / lilac, UFSHCI v2.1 at 0x01da4000).
 *
 * Hardware invariants proven on device (LILAC UFS READ BATCH v2):
 *  - Sony ABL (LA2.0_P_114) leaves UFSHCI enabled at 0x01da4000 with IE == 0,
 *    no SMMU translation (/soc/ufshc@1da4000 has no iommus property), one
 *    retired slot-0 transfer descriptor at UTRLBA (4 KiB aligned inside
 *    0x99000000..0x9b000000, outside XNU's 0xa0000000..0xf7ffffff DRAM window)
 *    with its UCD at UTRLBA+0x100, and the stopped UTMRLBA page at
 *    UTRLBA+0x1000.
 *  - This driver maps both 4 KiB firmware pages non-cacheably (VM_WIMG_WCOMB),
 *    saves their original contents, borrows slot 0 for serialized single-block
 *    4 KiB READ(10) and 8-byte READ CAPACITY(10) commands on LUN 0 using the
 *    stopped TM page as DMA bounce buffer, and restores both pages on
 *    loader_return (lilac_ufs_quiesce).
 *  - Strict read-only enforcement at every layer:
 *      1. Wire whitelist (lilac_ufs_permitted) rejects every opcode except
 *         READ CAPACITY(10) (0x25) and single-block READ(10) (0x28) on LUN 0,
 *         verified by reading back the descriptor/UPIU/PRDT before ringing
 *         UTRLDBR = 1.
 *      2. MMIO write guard allows ONLY UTRLRSR (0 or 1) and UTRLDBR (1).
 *      3. VFS/devsw open(FWRITE), strategy(!B_READ), and cdev write return
 *         EROFS, and DKIOCISWRITABLE reports 0.
 */

#ifdef LILAC_UFS_HOST_TEST
#include "lilac_ufs_host_shim.h"
#else
#include <kern/clock.h>
#include <kern/locks.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <mach/vm_param.h>
#include <miscfs/devfs/devfs.h>
#include <os/atomic_private.h>
#include <pexpert/device_tree.h>
#include <pexpert/pexpert.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/kauth.h>
#include <sys/lilac_ufs.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <vm/vm_kern.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern vm_offset_t ml_io_map_unmappable(vm_offset_t phys_addr, vm_size_t size, unsigned int flags);
extern boolean_t   pmap_valid_address(uint64_t addr);
#endif /* LILAC_UFS_HOST_TEST */

#define UFSHC_EXPECTED_PA       0x01da4000ULL
#define UFSHC_MIN_REG_SIZE      0x2500ULL
#define UFSHC_MAP_SIZE          0x3000ULL

#define UFSHC_CAP               0x00u
#define UFSHC_VER               0x08u
#define UFSHC_IS                0x20u
#define UFSHC_IE                0x24u
#define UFSHC_HCS               0x30u
#define UFSHC_HCE               0x34u
#define UFSHC_UTRLBA            0x50u
#define UFSHC_UTRLBAU           0x54u
#define UFSHC_UTRLDBR           0x58u
#define UFSHC_UTRLRSR           0x60u
#define UFSHC_UTMRLBA           0x70u
#define UFSHC_UTMRLBAU          0x74u
#define UFSHC_UTMRLDBR          0x78u
#define UFSHC_UTMRLRSR          0x80u

#define UFSHC_IS_FATAL          ((1u << 2) | (1u << 11) | (1u << 12) | (1u << 16) | (1u << 17))

#define UFS_BLOCK_BYTES         4096u
#define UFS_UCD_OFF             0x100u
#define UFS_RESP_OFF            0x120u
#define UFS_PRDT_OFF            0x160u
#define UFS_MAX_GPT_ENTRIES     128u
#define UFS_MAX_GPT_ARRAY_BYTES (128u * 1024u)
#define UFS_MAX_MINORS          (1u + UFS_MAX_GPT_ENTRIES) /* 0 = lun0, 1..N = GPT partitions */
#define UFS_CREQ_CHUNK          (64u * 1024u)
#define UFS_CATALOG_MAX         8192u

struct lilac_ufs_minor {
	char     name[40];
	uint64_t first_lba;
	uint64_t blocks;
	uint32_t bsize;
};

static vm_offset_t lilac_ufs_mmio;
static vm_offset_t lilac_ufs_tr_va;
static vm_offset_t lilac_ufs_tm_va;

static uint32_t lilac_ufs_tr_pa;
static uint32_t lilac_ufs_tm_pa;
static uint32_t lilac_ufs_cmd_type;
static uint32_t lilac_ufs_last_lba;
static uint32_t lilac_ufs_block_bytes;

static uint32_t lilac_ufs_saved_tr[UFS_BLOCK_BYTES / 4];
static uint32_t lilac_ufs_saved_tm[UFS_BLOCK_BYTES / 4];
static uint32_t lilac_ufs_block_buf[UFS_BLOCK_BYTES / 4];

static struct lilac_ufs_minor lilac_ufs_minors[UFS_MAX_MINORS];
static uint32_t lilac_ufs_minor_count;  /* includes minor 0 (lun0) */
static uint32_t lilac_ufs_partitions;   /* GPT partitions only (minor_count - 1) */
static uint32_t lilac_ufs_ready;
static uint32_t lilac_ufs_mapped;
static volatile uint32_t lilac_ufs_closed;
static uint32_t lilac_ufs_restored;

static uint64_t lilac_ufs_commands;
static uint64_t lilac_ufs_bytes_read;
static uint64_t lilac_ufs_errors;
static uint64_t lilac_ufs_write_refused;

static int lilac_ufs_bmajor = -1;
static int lilac_ufs_cmajor = -1;

static char lilac_ufs_catalog[UFS_CATALOG_MAX];

#ifndef LILAC_UFS_HOST_TEST
static LCK_GRP_DECLARE(lilac_ufs_lck_grp, "lilac_ufs");
static LCK_MTX_DECLARE(lilac_ufs_mtx, &lilac_ufs_lck_grp);

SYSCTL_DECL(_kern_hakios);
SYSCTL_UINT(_kern_hakios, OID_AUTO, lilac_ufs_ready, CTLFLAG_RD | CTLFLAG_LOCKED,
    &lilac_ufs_ready, 0, "1 when lilac UFS read-only driver is active");
SYSCTL_UINT(_kern_hakios, OID_AUTO, lilac_ufs_partitions, CTLFLAG_RD | CTLFLAG_LOCKED,
    &lilac_ufs_partitions, 0, "number of validated GPT partitions on LUN 0");
SYSCTL_UINT(_kern_hakios, OID_AUTO, lilac_ufs_last_lba, CTLFLAG_RD | CTLFLAG_LOCKED,
    &lilac_ufs_last_lba, 0, "last 4 KiB LBA of LUN 0");
SYSCTL_UINT(_kern_hakios, OID_AUTO, lilac_ufs_tr_pa, CTLFLAG_RD | CTLFLAG_LOCKED,
    &lilac_ufs_tr_pa, 0, "inherited firmware UTRLBA physical address");
SYSCTL_QUAD(_kern_hakios, OID_AUTO, lilac_ufs_commands, CTLFLAG_RD | CTLFLAG_LOCKED,
    &lilac_ufs_commands, "completed read-only UFS commands");
SYSCTL_QUAD(_kern_hakios, OID_AUTO, lilac_ufs_bytes_read, CTLFLAG_RD | CTLFLAG_LOCKED,
    &lilac_ufs_bytes_read, "logical bytes served from LUN 0");
SYSCTL_QUAD(_kern_hakios, OID_AUTO, lilac_ufs_errors, CTLFLAG_RD | CTLFLAG_LOCKED,
    &lilac_ufs_errors, "failed UFS transfer attempts");
SYSCTL_QUAD(_kern_hakios, OID_AUTO, lilac_ufs_write_refused, CTLFLAG_RD | CTLFLAG_LOCKED,
    &lilac_ufs_write_refused, "write opens or write I/O requests rejected with EROFS");
SYSCTL_STRING(_kern_hakios, OID_AUTO, lilac_ufs_catalog, CTLFLAG_RD | CTLFLAG_LOCKED,
    lilac_ufs_catalog, 0, "tab-separated minor/name/first_lba/last_lba/blocks catalog");
#endif /* !LILAC_UFS_HOST_TEST */

/* ---- low-level hardware access & strict MMIO write guard ---- */

#ifndef LILAC_UFS_HOST_TEST
static inline void
lilac_ufs_barrier(void)
{
	__asm__ volatile ("dsb sy" ::: "memory");
}

static inline uint64_t
lilac_ufs_cntvct(void)
{
	uint64_t v;
	__asm__ volatile ("mrs %0, cntvct_el0" : "=r"(v));
	return v;
}

static inline uint64_t
lilac_ufs_cntfrq(void)
{
	uint64_t v;
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

static inline uint32_t
lilac_ufs_reg_read(uint32_t offset)
{
	return *(const volatile uint32_t *)(lilac_ufs_mmio + offset);
}

static inline void
lilac_ufs_reg_write(uint32_t offset, uint32_t value)
{
	if (!((offset == UFSHC_UTRLRSR && value <= 1u) ||
	      (offset == UFSHC_UTRLDBR && value == 1u))) {
		panic("lilac_ufs: forbidden MMIO write off=0x%x val=0x%x", offset, value);
	}
	*(volatile uint32_t *)(lilac_ufs_mmio + offset) = value;
	lilac_ufs_barrier();
}

static inline uint32_t
lilac_ufs_tr_load(uint32_t byte_off)
{
	return *(const volatile uint32_t *)(lilac_ufs_tr_va + byte_off);
}

static inline void
lilac_ufs_tr_store(uint32_t byte_off, uint32_t value)
{
	*(volatile uint32_t *)(lilac_ufs_tr_va + byte_off) = value;
}

static inline uint32_t
lilac_ufs_tm_load(uint32_t byte_off)
{
	return *(const volatile uint32_t *)(lilac_ufs_tm_va + byte_off);
}

static inline void
lilac_ufs_tm_store(uint32_t byte_off, uint32_t value)
{
	*(volatile uint32_t *)(lilac_ufs_tm_va + byte_off) = value;
}
#endif /* !LILAC_UFS_HOST_TEST */

/* ---- endian, CRC32 & layout/wire validation ---- */

static uint32_t
lu_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t
lu_le32(const uint8_t *p)
{
	return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}

static uint64_t
lu_le64(const uint8_t *p)
{
	return ((uint64_t)lu_le32(p + 4) << 32) | (uint64_t)lu_le32(p);
}

static void
lu_put_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static uint32_t
lu_crc32_update(uint32_t crc, const uint8_t *p, uint32_t n, bool gpt_header)
{
	for (uint32_t i = 0; i < n; i++) {
		crc ^= (gpt_header && i >= 16u && i < 20u) ? 0u : (uint32_t)p[i];
		for (int b = 0; b < 8; b++) {
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
		}
	}
	return crc;
}

static uint32_t
lu_crc32(const uint8_t *p, uint32_t n, bool gpt_header)
{
	return ~lu_crc32_update(~0u, p, n, gpt_header);
}

static bool
lu_same_queues(void)
{
	return lilac_ufs_reg_read(UFSHC_UTRLBA) == lilac_ufs_tr_pa &&
	       lilac_ufs_reg_read(UFSHC_UTRLBAU) == 0u &&
	       lilac_ufs_reg_read(UFSHC_UTMRLBA) == lilac_ufs_tm_pa &&
	       lilac_ufs_reg_read(UFSHC_UTMRLBAU) == 0u &&
	       lilac_ufs_reg_read(UFSHC_UTMRLRSR) == 0u &&
	       lilac_ufs_reg_read(UFSHC_UTMRLDBR) == 0u &&
	       lilac_ufs_reg_read(UFSHC_IE) == 0u &&
	       lilac_ufs_reg_read(UFSHC_HCE) == 1u &&
	       (lilac_ufs_reg_read(UFSHC_HCS) & 3u) == 3u;
}

static bool
lu_wait_reg(uint32_t reg, uint32_t mask, uint32_t expected, uint32_t ms)
{
	uint64_t freq = lilac_ufs_cntfrq();
	uint64_t start = lilac_ufs_cntvct();
	uint64_t budget = (freq > 0 && freq <= 1000000000ULL) ? (freq * ms / 1000ULL) : 0ULL;

	for (uint32_t n = 0; n < 10000000u; n++) {
		if ((lilac_ufs_reg_read(reg) & mask) == expected) {
			return true;
		}
		if (budget > 0 && (lilac_ufs_cntvct() - start) >= budget) {
			break;
		}
	}
	return (lilac_ufs_reg_read(reg) & mask) == expected;
}

static void
lu_load_tr_page(uint32_t *dst)
{
	for (uint32_t i = 0; i < UFS_BLOCK_BYTES / 4u; i++) {
		dst[i] = lilac_ufs_tr_load(i * 4u);
	}
}

static void
lu_load_tm_page(uint32_t *dst)
{
	for (uint32_t i = 0; i < UFS_BLOCK_BYTES / 4u; i++) {
		dst[i] = lilac_ufs_tm_load(i * 4u);
	}
}

static void
lu_store_tr_page(const uint32_t *src)
{
	for (uint32_t i = 0; i < UFS_BLOCK_BYTES / 4u; i++) {
		lilac_ufs_tr_store(i * 4u, src[i]);
	}
}

static void
lu_store_tm_page(const uint32_t *src)
{
	for (uint32_t i = 0; i < UFS_BLOCK_BYTES / 4u; i++) {
		lilac_ufs_tm_store(i * 4u, src[i]);
	}
}

static bool
lu_verify_tr_page(const uint32_t *expected)
{
	for (uint32_t i = 0; i < UFS_BLOCK_BYTES / 4u; i++) {
		if (lilac_ufs_tr_load(i * 4u) != expected[i]) {
			return false;
		}
	}
	return true;
}

static bool
lu_verify_tm_page(const uint32_t *expected)
{
	for (uint32_t i = 0; i < UFS_BLOCK_BYTES / 4u; i++) {
		if (lilac_ufs_tm_load(i * 4u) != expected[i]) {
			return false;
		}
	}
	return true;
}

/*
 * Validate ABL's retired slot-0 command before borrowing the two queue pages.
 * Never dereference its old PRDT data address or resubmit its old CDB.
 */
static bool
lu_retired_layout(const uint32_t *d, uint32_t base_pa)
{
	const uint8_t *u = (const uint8_t *)d + UFS_UCD_OFF;
	const uint8_t *v = (const uint8_t *)d + UFS_RESP_OFF;
	uint32_t dw = d[0] & ~0x10000000u;
	bool was_read = (dw == 0x05000000u && u[1] == 0x40u && u[16] == 0x28u && u[17] == 0u);
	bool was_write = (dw == 0x03000000u && u[1] == 0x20u && u[16] == 0x2au && u[17] == 8u);
	uint32_t blocks = ((uint32_t)u[23] << 8) | (uint32_t)u[24];
	uint32_t bytes = lu_be32(u + 12);
	uint32_t data_pa = d[UFS_PRDT_OFF / 4u];

	if ((!was_read && !was_write) || d[1] != 0u || d[2] != 0u || d[3] != 0u ||
	    d[4] != base_pa + UFS_UCD_OFF || d[5] != 0u ||
	    d[6] != 0x00080010u || d[7] != 0x00180001u ||
	    u[0] != 1u || u[2] != 0u || blocks == 0u || blocks > 64u ||
	    bytes != blocks * UFS_BLOCK_BYTES || u[22] != 0u ||
	    d[UFS_PRDT_OFF / 4u + 1u] != 0u || d[UFS_PRDT_OFF / 4u + 2u] != 0u ||
	    d[UFS_PRDT_OFF / 4u + 3u] != bytes - 1u ||
	    data_pa < 0x80000000u || data_pa > 0xa0000000u - bytes || (data_pa & 3u) != 0u ||
	    v[0] != 0x21u || v[1] != 0u || v[2] != 0u || v[3] != u[3] ||
	    v[6] != 0u || v[7] != 0u || v[10] != 0u || v[11] != 0u || lu_be32(v + 12) != 0u) {
		return false;
	}
	for (uint32_t i = 4u; i < 12u; i++) {
		if (u[i] != 0u) {
			return false;
		}
	}
	for (uint32_t i = 25u; i < 32u; i++) {
		if (u[i] != 0u) {
			return false;
		}
	}
	return true;
}

/*
 * Independent wire-level read-only whitelist.
 * Accepts ONLY LUN 0 READ CAPACITY(10) (0x25, 8 bytes) and single-block
 * 4 KiB READ(10) (0x28, LBA <= last_lba).
 */
static bool
lu_permitted(const uint8_t *u, uint32_t dw0, uint32_t bytes, uint32_t last_lba, uint32_t block_bytes)
{
	if ((dw0 != 0x05000000u && dw0 != 0x15000000u) ||
	    u[0] != 1u || u[1] != 0x40u || u[2] != 0u || lu_be32(u + 12) != bytes) {
		return false;
	}
	for (uint32_t i = 4u; i < 12u; i++) {
		if (u[i] != 0u) {
			return false;
		}
	}
	if (u[16] == 0x25u) {
		if (bytes != 8u) {
			return false;
		}
		for (uint32_t i = 17u; i < 32u; i++) {
			if (u[i] != 0u) {
				return false;
			}
		}
		return true;
	}
	if (u[16] != 0x28u || block_bytes != UFS_BLOCK_BYTES || bytes != block_bytes ||
	    lu_be32(u + 18) > last_lba || u[17] != 0u || u[22] != 0u ||
	    u[23] != 0u || u[24] != 1u) {
		return false;
	}
	for (uint32_t i = 25u; i < 32u; i++) {
		if (u[i] != 0u) {
			return false;
		}
	}
	return true;
}

/*
 * Execute one whitelisted read command in slot 0 while holding lilac_ufs_mtx.
 * Output lands in lilac_ufs_block_buf.
 */
static int
lu_transfer_locked(uint8_t opcode, uint32_t lba, uint32_t bytes)
{
	uint32_t words[8] = { 0 };
	uint32_t desc[8] = { 0 };
	uint32_t wire[8] = { 0 };
	uint32_t reply[8] = { 0 };
	uint8_t *u = (uint8_t *)words;
	uint8_t *resp = (uint8_t *)reply;
	uint32_t dw0 = lilac_ufs_cmd_type | 0x05000000u;

	if (lilac_ufs_closed != 0u) {
		return ENXIO;
	}
	u[0] = 1u;
	u[1] = 0x40u;
	u[3] = (uint8_t)(0x40u + ((uint32_t)lilac_ufs_commands & 0x7fu));
	lu_put_be32(u + 12, bytes);
	u[16] = opcode;
	if (opcode == 0x28u) {
		lu_put_be32(u + 18, lba);
		u[24] = 1u;
	}
	if (!lu_permitted(u, dw0, bytes, lilac_ufs_last_lba, lilac_ufs_block_bytes)) {
		lilac_ufs_errors++;
		return EINVAL;
	}
	if (!lu_same_queues() || lilac_ufs_reg_read(UFSHC_UTRLDBR) != 0u ||
	    lilac_ufs_reg_read(UFSHC_UTRLRSR) != 1u ||
	    (lilac_ufs_reg_read(UFSHC_IS) & UFSHC_IS_FATAL) != 0u) {
		lilac_ufs_errors++;
		return EIO;
	}

	lilac_ufs_reg_write(UFSHC_UTRLRSR, 0u);
	if (!lu_wait_reg(UFSHC_UTRLRSR, 1u, 0u, 100u) ||
	    !lu_same_queues() || lilac_ufs_reg_read(UFSHC_UTRLDBR) != 0u) {
		lilac_ufs_errors++;
		return EIO;
	}

	for (uint32_t off = 0u; off < 0x80u; off += 4u) {
		lilac_ufs_tr_store(UFS_UCD_OFF + off, 0u);
	}
	for (uint32_t off = 0u; off < UFS_BLOCK_BYTES; off += 4u) {
		lilac_ufs_tm_store(off, 0xa5a5a5a5u);
	}
	for (uint32_t i = 0u; i < 8u; i++) {
		lilac_ufs_tr_store(UFS_UCD_OFF + i * 4u, words[i]);
	}
	lilac_ufs_tr_store(UFS_PRDT_OFF + 0u, lilac_ufs_tm_pa);
	lilac_ufs_tr_store(UFS_PRDT_OFF + 12u, bytes - 1u);

	desc[0] = dw0;
	desc[2] = 0xfu;
	desc[4] = lilac_ufs_tr_pa + UFS_UCD_OFF;
	desc[6] = 0x00080010u;
	desc[7] = 0x00180001u;
	for (uint32_t i = 0u; i < 8u; i++) {
		lilac_ufs_tr_store(i * 4u, desc[i]);
	}
	lilac_ufs_barrier();

	for (uint32_t i = 0u; i < 8u; i++) {
		wire[i] = lilac_ufs_tr_load(UFS_UCD_OFF + i * 4u);
		if (lilac_ufs_tr_load(i * 4u) != desc[i]) {
			lilac_ufs_errors++;
			return EIO;
		}
	}
	if (memcmp(wire, words, sizeof(words)) != 0 ||
	    !lu_permitted((const uint8_t *)wire, dw0, bytes, lilac_ufs_last_lba, lilac_ufs_block_bytes) ||
	    lilac_ufs_tr_load(UFS_PRDT_OFF + 0u) != lilac_ufs_tm_pa ||
	    lilac_ufs_tr_load(UFS_PRDT_OFF + 4u) != 0u ||
	    lilac_ufs_tr_load(UFS_PRDT_OFF + 8u) != 0u ||
	    lilac_ufs_tr_load(UFS_PRDT_OFF + 12u) != bytes - 1u) {
		lilac_ufs_errors++;
		return EIO;
	}

	lilac_ufs_reg_write(UFSHC_UTRLRSR, 1u);
	if (!lu_wait_reg(UFSHC_UTRLRSR, 1u, 1u, 100u) ||
	    !lu_same_queues() || lilac_ufs_reg_read(UFSHC_UTRLDBR) != 0u) {
		lilac_ufs_errors++;
		return EIO;
	}

	lilac_ufs_barrier();
	lilac_ufs_reg_write(UFSHC_UTRLDBR, 1u);
	if (!lu_wait_reg(UFSHC_UTRLDBR, ~0u, 0u, 2000u)) {
		/* Leave queues untouched if DMA ever times out. */
		lilac_ufs_closed = 1u;
		lilac_ufs_ready = 0u;
		lilac_ufs_errors++;
		return EIO;
	}
	lilac_ufs_barrier();

	uint32_t ocs = lilac_ufs_tr_load(8u);
	uint32_t is_after = lilac_ufs_reg_read(UFSHC_IS);
	for (uint32_t i = 0u; i < 8u; i++) {
		reply[i] = lilac_ufs_tr_load(UFS_RESP_OFF + i * 4u);
	}
	if (ocs != 0u || resp[0] != 0x21u || resp[1] != 0u || resp[2] != 0u ||
	    resp[3] != u[3] || resp[6] != 0u || resp[7] != 0u ||
	    resp[10] != 0u || resp[11] != 0u || lu_be32(resp + 12) != 0u ||
	    (is_after & UFSHC_IS_FATAL) != 0u || !lu_same_queues()) {
		lilac_ufs_errors++;
		return EIO;
	}

	lu_load_tm_page(lilac_ufs_block_buf);
	lilac_ufs_commands++;
	return 0;
}

/* ---- GPT validation & partition catalog ---- */

static bool
lu_valid_gpt_header(const uint8_t *p, uint32_t own_lba, uint32_t other_lba)
{
	static const uint8_t sig[8] = { 'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T' };
	uint32_t hdr_size = lu_le32(p + 12);

	if (memcmp(p, sig, sizeof(sig)) != 0 || lu_le32(p + 8) != 0x10000u ||
	    hdr_size < 92u || hdr_size > UFS_BLOCK_BYTES || lu_le32(p + 20) != 0u ||
	    lu_le64(p + 24) != own_lba || lu_le64(p + 32) != other_lba ||
	    lu_le64(p + 40) > lu_le64(p + 48) || lu_le64(p + 40) < 2u ||
	    lu_le64(p + 48) >= lilac_ufs_last_lba ||
	    lu_le32(p + 16) != lu_crc32(p, hdr_size, true)) {
		return false;
	}
	uint32_t count = lu_le32(p + 80);
	uint32_t stride = lu_le32(p + 84);
	uint64_t bytes = (uint64_t)count * stride;
	uint64_t entry_lba = lu_le64(p + 72);
	if (count == 0u || count > UFS_MAX_GPT_ENTRIES || stride < 128u ||
	    (stride % 128u) != 0u || bytes > UFS_MAX_GPT_ARRAY_BYTES ||
	    entry_lba < 2u || entry_lba >= lilac_ufs_last_lba) {
		return false;
	}
	uint64_t blocks = (bytes + UFS_BLOCK_BYTES - 1u) / UFS_BLOCK_BYTES;
	if (own_lba == 1u) {
		if (entry_lba + blocks > lu_le64(p + 40)) {
			return false;
		}
	} else {
		if (entry_lba <= lu_le64(p + 48) || entry_lba + blocks > lilac_ufs_last_lba) {
			return false;
		}
	}
	return true;
}

static bool
lu_nonzero(const uint8_t *p, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		if (p[i] != 0u) {
			return true;
		}
	}
	return false;
}

static bool
lu_name_exists(const char *name, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++) {
		if (strcmp(lilac_ufs_minors[i].name, name) == 0) {
			return true;
		}
	}
	return false;
}

static void
lu_sanitize_gpt_name(const uint8_t *utf16le, uint32_t part_idx, uint32_t existing_count,
    char *out, size_t out_sz)
{
	char raw[37];
	size_t n = 0;

	for (uint32_t k = 0; k < 36u; k++) {
		uint32_t ch = (uint32_t)utf16le[k * 2u] | ((uint32_t)utf16le[k * 2u + 1u] << 8);
		if (ch == 0u) {
			break;
		}
		if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
		    (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.') {
			raw[n++] = (char)ch;
		} else {
			raw[n++] = '_';
		}
	}
	raw[n] = '\0';
	if (n == 0) {
		snprintf(out, out_sz, "part%u", part_idx);
	} else {
		strlcpy(out, raw, out_sz);
	}
	if (lu_name_exists(out, existing_count)) {
		snprintf(out, out_sz, "%.28s_%u", raw[0] ? raw : "part", part_idx);
	}
}

static int
lu_discover_gpt_locked(void)
{
	uint8_t primary_hdr[UFS_BLOCK_BYTES];
	uint8_t *entries = NULL;
	int err;

	if ((err = lu_transfer_locked(0x25u, 0u, 8u)) != 0) {
		return err;
	}
	const uint8_t *cap_buf = (const uint8_t *)lilac_ufs_block_buf;
	uint32_t last_lba = lu_be32(cap_buf);
	uint32_t block_bytes = lu_be32(cap_buf + 4);
	if (last_lba == ~0u || last_lba < 64u || block_bytes != UFS_BLOCK_BYTES) {
		return EIO;
	}
	lilac_ufs_last_lba = last_lba;
	lilac_ufs_block_bytes = block_bytes;

	/* Protective MBR at LBA 0 */
	if ((err = lu_transfer_locked(0x28u, 0u, UFS_BLOCK_BYTES)) != 0) {
		return err;
	}
	const uint8_t *p = (const uint8_t *)lilac_ufs_block_buf;
	if (p[510] != 0x55u || p[511] != 0xaau) {
		return EIO;
	}

	/* Primary GPT header at LBA 1 */
	if ((err = lu_transfer_locked(0x28u, 1u, UFS_BLOCK_BYTES)) != 0) {
		return err;
	}
	if (!lu_valid_gpt_header(p, 1u, last_lba)) {
		return EIO;
	}
	memcpy(primary_hdr, p, UFS_BLOCK_BYTES);

	/* Backup GPT header at last_lba */
	if ((err = lu_transfer_locked(0x28u, last_lba, UFS_BLOCK_BYTES)) != 0) {
		return err;
	}
	if (!lu_valid_gpt_header(p, last_lba, 1u) ||
	    memcmp(p + 40, primary_hdr + 40, 32) != 0 ||
	    memcmp(p + 80, primary_hdr + 80, 12) != 0) {
		return EIO;
	}

	uint64_t first_usable = lu_le64(primary_hdr + 40);
	uint64_t last_usable = lu_le64(primary_hdr + 48);
	uint32_t entry_count = lu_le32(primary_hdr + 80);
	uint32_t entry_bytes = lu_le32(primary_hdr + 84);
	uint32_t expected_crc = lu_le32(primary_hdr + 88);
	uint32_t array_bytes = entry_count * entry_bytes;
	uint32_t primary_lba = (uint32_t)lu_le64(primary_hdr + 72);
	uint32_t backup_lba = (uint32_t)lu_le64(p + 72);

	entries = kalloc_data(array_bytes, Z_WAITOK | Z_ZERO);
	if (entries == NULL) {
		return ENOMEM;
	}

	uint32_t crc_prim = ~0u;
	for (uint32_t off = 0u; off < array_bytes; off += UFS_BLOCK_BYTES) {
		uint32_t take = MIN(array_bytes - off, UFS_BLOCK_BYTES);
		if ((err = lu_transfer_locked(0x28u, primary_lba + off / UFS_BLOCK_BYTES,
		    UFS_BLOCK_BYTES)) != 0) {
			kfree_data(entries, array_bytes);
			return err;
		}
		memcpy(entries + off, p, take);
		crc_prim = lu_crc32_update(crc_prim, p, take, false);
	}
	if (~crc_prim != expected_crc) {
		kfree_data(entries, array_bytes);
		return EIO;
	}

	uint32_t crc_back = ~0u;
	for (uint32_t off = 0u; off < array_bytes; off += UFS_BLOCK_BYTES) {
		uint32_t take = MIN(array_bytes - off, UFS_BLOCK_BYTES);
		if ((err = lu_transfer_locked(0x28u, backup_lba + off / UFS_BLOCK_BYTES,
		    UFS_BLOCK_BYTES)) != 0) {
			kfree_data(entries, array_bytes);
			return err;
		}
		if (memcmp(entries + off, p, take) != 0) {
			kfree_data(entries, array_bytes);
			return EIO;
		}
		crc_back = lu_crc32_update(crc_back, p, take, false);
	}
	if (~crc_back != expected_crc) {
		kfree_data(entries, array_bytes);
		return EIO;
	}

	/* Minor 0 is always whole LUN 0 */
	memset(lilac_ufs_minors, 0, sizeof(lilac_ufs_minors));
	strlcpy(lilac_ufs_minors[0].name, "lun0", sizeof(lilac_ufs_minors[0].name));
	lilac_ufs_minors[0].first_lba = 0ULL;
	lilac_ufs_minors[0].blocks = (uint64_t)last_lba + 1ULL;
	lilac_ufs_minors[0].bsize = UFS_BLOCK_BYTES;
	uint32_t mcount = 1u;

	for (uint32_t i = 0u; i < entry_count; i++) {
		const uint8_t *e = entries + i * entry_bytes;
		if (!lu_nonzero(e, 16u)) {
			continue;
		}
		uint64_t first = lu_le64(e + 32);
		uint64_t last = lu_le64(e + 40);
		if (!lu_nonzero(e + 16, 16u) || first < first_usable ||
		    last > last_usable || first > last || mcount >= UFS_MAX_MINORS) {
			kfree_data(entries, array_bytes);
			return EIO;
		}
		for (uint32_t j = 0u; j < i; j++) {
			const uint8_t *q = entries + j * entry_bytes;
			if (lu_nonzero(q, 16u) &&
			    (memcmp(e + 16, q + 16, 16) == 0 ||
			     (first <= lu_le64(q + 40) && last >= lu_le64(q + 32)))) {
				kfree_data(entries, array_bytes);
				return EIO;
			}
		}
		struct lilac_ufs_minor *m = &lilac_ufs_minors[mcount];
		lu_sanitize_gpt_name(e + 56, mcount, mcount, m->name, sizeof(m->name));
		m->first_lba = first;
		m->blocks = last - first + 1ULL;
		m->bsize = UFS_BLOCK_BYTES;
		mcount++;
	}

	kfree_data(entries, array_bytes);
	if (mcount <= 1u) {
		return EIO;
	}
	lilac_ufs_minor_count = mcount;
	lilac_ufs_partitions = mcount - 1u;
	return 0;
}

/* ---- byte-accurate read helper across arbitrary offsets & lengths ---- */

static inline uint64_t
lu_minor_bytes(uint32_t part)
{
	return lilac_ufs_minors[part].blocks * (uint64_t)UFS_BLOCK_BYTES;
}

static int
lu_read_bytes(uint32_t part, uint64_t offset, uint8_t *dst, uint64_t len)
{
	uint64_t size;
	int err = 0;

	if (!lilac_ufs_ready || part >= lilac_ufs_minor_count) {
		return ENXIO;
	}
	size = lu_minor_bytes(part);
	if (offset > size || len > size - offset) {
		return EINVAL;
	}
	if (len == 0ULL) {
		return 0;
	}

	lck_mtx_lock(&lilac_ufs_mtx);
	while (len > 0ULL) {
		uint64_t rel_block = offset / UFS_BLOCK_BYTES;
		uint32_t skip = (uint32_t)(offset % UFS_BLOCK_BYTES);
		uint32_t take = (uint32_t)MIN((uint64_t)(UFS_BLOCK_BYTES - skip), len);
		uint64_t abs_lba = lilac_ufs_minors[part].first_lba + rel_block;

		if (abs_lba > (uint64_t)lilac_ufs_last_lba) {
			err = EINVAL;
			break;
		}
		err = lu_transfer_locked(0x28u, (uint32_t)abs_lba, UFS_BLOCK_BYTES);
		if (err != 0) {
			break;
		}
		memcpy(dst, (const uint8_t *)lilac_ufs_block_buf + skip, take);
		dst += take;
		offset += take;
		len -= take;
		lilac_ufs_bytes_read += take;
	}
	lck_mtx_unlock(&lilac_ufs_mtx);
	return err;
}

/* ---- BSD block and character device switches ---- */

static int
lilac_ufs_open(dev_t dev, int flags, __unused int devtype, __unused proc_t p)
{
	uint32_t part = (uint32_t)minor(dev);

	if (!lilac_ufs_ready || part >= lilac_ufs_minor_count) {
		return ENXIO;
	}
	if (flags & FWRITE) {
		lilac_ufs_write_refused++;
		return EROFS;
	}
	return 0;
}

static int
lilac_ufs_close(__unused dev_t dev, __unused int flags, __unused int devtype, __unused proc_t p)
{
	return 0;
}

static void
lilac_ufs_strategy(buf_t bp)
{
	uint32_t part = (uint32_t)minor(buf_device(bp));
	uint64_t offset, len, size;
	caddr_t mapped = NULL;
	int err;

	if (!lilac_ufs_ready || part >= lilac_ufs_minor_count) {
		buf_seterror(bp, ENXIO);
		buf_biodone(bp);
		return;
	}
	if ((buf_flags(bp) & B_READ) == 0) {
		lilac_ufs_write_refused++;
		buf_seterror(bp, EROFS);
		buf_setresid(bp, buf_count(bp));
		buf_biodone(bp);
		return;
	}

	size = lu_minor_bytes(part);
	offset = (uint64_t)buf_blkno(bp) * (uint64_t)lilac_ufs_minors[part].bsize;
	len = buf_count(bp);
	if (offset >= size) {
		buf_seterror(bp, offset == size ? 0 : EINVAL);
		buf_setresid(bp, buf_count(bp));
		buf_biodone(bp);
		return;
	}
	if (len > size - offset) {
		len = size - offset;
	}

	err = buf_map(bp, &mapped);
	if (err == 0) {
		err = lu_read_bytes(part, offset, (uint8_t *)mapped, len);
		buf_unmap(bp);
	}
	buf_setresid(bp, (uint32_t)(buf_count(bp) - (err == 0 ? len : 0ULL)));
	if (err != 0) {
		buf_seterror(bp, err);
	}
	buf_biodone(bp);
}

static int
lilac_ufs_ioctl(dev_t dev, u_long cmd, caddr_t data, __unused int fflag, __unused proc_t p)
{
	uint32_t part = (uint32_t)minor(dev);

	if (!lilac_ufs_ready || part >= lilac_ufs_minor_count) {
		return ENXIO;
	}
	switch (cmd) {
	case DKIOCGETBLOCKSIZE:
		*(uint32_t *)data = lilac_ufs_minors[part].bsize;
		return 0;
	case DKIOCSETBLOCKSIZE: {
		/*
		 * spec_open() temporarily sets 512-byte blocks to compute
		 * v_specdevsize and restores 4096-byte blocks immediately after.
		 */
		uint32_t want = *(uint32_t *)data;
		if (want < 512u || want > UFS_BLOCK_BYTES || (want & (want - 1u)) != 0u) {
			return EINVAL;
		}
		lilac_ufs_minors[part].bsize = want;
		return 0;
	}
	case DKIOCGETPHYSICALBLOCKSIZE:
		*(uint32_t *)data = UFS_BLOCK_BYTES;
		return 0;
	case DKIOCGETBLOCKCOUNT:
		*(uint64_t *)data = lu_minor_bytes(part) / lilac_ufs_minors[part].bsize;
		return 0;
	case DKIOCISWRITABLE:
		*(uint32_t *)data = 0u;
		return 0;
	case DKIOCGETMAXBLOCKCOUNTREAD:
		*(uint64_t *)data = UFS_CREQ_CHUNK / lilac_ufs_minors[part].bsize;
		return 0;
	default:
		return ENOTTY;
	}
}

static int
lilac_ufs_psize(dev_t dev)
{
	uint32_t part = (uint32_t)minor(dev);
	return (lilac_ufs_ready && part < lilac_ufs_minor_count) ?
	       (int)lilac_ufs_minors[part].bsize : -1;
}

/*
 * Character device read (/dev/rufs_*): kernel bounce buffer + uiomove so user
 * pages are never touched directly (ARM64 PAN) and lilac_ufs_mtx is never held
 * across a user page fault.
 */
static int
lilac_ufs_cread(dev_t dev, struct uio *uio, __unused int ioflag)
{
	uint32_t part = (uint32_t)minor(dev);
	uint64_t size;
	uint8_t *bounce;
	int err = 0;

	if (!lilac_ufs_ready || part >= lilac_ufs_minor_count) {
		return ENXIO;
	}
	if (uio_offset(uio) < 0) {
		return EINVAL;
	}
	size = lu_minor_bytes(part);
	bounce = kalloc_data(UFS_CREQ_CHUNK, Z_WAITOK);
	if (bounce == NULL) {
		return ENOMEM;
	}
	while (err == 0 && uio_resid(uio) > 0 && (uint64_t)uio_offset(uio) < size) {
		uint64_t at = (uint64_t)uio_offset(uio);
		uint64_t want = (uint64_t)uio_resid(uio);

		if (want > UFS_CREQ_CHUNK) {
			want = UFS_CREQ_CHUNK;
		}
		if (want > size - at) {
			want = size - at;
		}
		err = lu_read_bytes(part, at, bounce, want);
		if (err == 0) {
			err = uiomove((caddr_t)bounce, (int)want, uio);
		}
	}
	kfree_data(bounce, UFS_CREQ_CHUNK);
	return err;
}

static int
lilac_ufs_cwrite(__unused dev_t dev, __unused struct uio *uio, __unused int ioflag)
{
	lilac_ufs_write_refused++;
	return EROFS;
}

static const struct bdevsw lilac_ufs_bdevsw = {
	.d_open     = lilac_ufs_open,
	.d_close    = lilac_ufs_close,
	.d_strategy = lilac_ufs_strategy,
	.d_ioctl    = lilac_ufs_ioctl,
	.d_dump     = eno_dump,
	.d_psize    = lilac_ufs_psize,
	.d_type     = D_DISK,
};

static const struct cdevsw lilac_ufs_cdevsw = {
	.d_open       = lilac_ufs_open,
	.d_close      = lilac_ufs_close,
	.d_read       = lilac_ufs_cread,
	.d_write      = lilac_ufs_cwrite,
	.d_ioctl      = lilac_ufs_ioctl,
	.d_stop       = eno_stop,
	.d_reset      = eno_reset,
	.d_ttys       = NULL,
	.d_select     = eno_select,
	.d_mmap       = eno_mmap,
	.d_strategy   = eno_strat,
	.d_reserved_1 = eno_getc,
	.d_reserved_2 = eno_putc,
	.d_type       = D_DISK,
};

/* ---- catalog builder ---- */

static void
lu_build_catalog(void)
{
	size_t used = 0;

	lilac_ufs_catalog[0] = '\0';
	for (uint32_t i = 0; i < lilac_ufs_minor_count; i++) {
		const struct lilac_ufs_minor *m = &lilac_ufs_minors[i];
		uint64_t last = m->first_lba + m->blocks - 1ULL;
		int n = snprintf(lilac_ufs_catalog + used, sizeof(lilac_ufs_catalog) - used,
		    "%u\t%s\t0x%llx\t0x%llx\t0x%llx\n",
		    i, m->name, (unsigned long long)m->first_lba,
		    (unsigned long long)last, (unsigned long long)m->blocks);
		if (n <= 0 || (size_t)n >= sizeof(lilac_ufs_catalog) - used) {
			break;
		}
		used += (size_t)n;
	}
}

/* ---- public kernel entry points ---- */

bool
lilac_ufs_dev(dev_t dev)
{
	return lilac_ufs_ready != 0u &&
	       ((lilac_ufs_bmajor >= 0 && major(dev) == lilac_ufs_bmajor) ||
	        (lilac_ufs_cmajor >= 0 && major(dev) == lilac_ufs_cmajor));
}

void
lilac_ufs_report(const char *when)
{
	if (!lilac_ufs_ready) {
		return;
	}
	printf("lilac-ufs: %s: %llu cmds, %llu bytes, %llu errors, %llu writes refused\n",
	    when, (unsigned long long)lilac_ufs_commands,
	    (unsigned long long)lilac_ufs_bytes_read,
	    (unsigned long long)lilac_ufs_errors,
	    (unsigned long long)lilac_ufs_write_refused);
}

int
lilac_ufs_read_partition(const char *name, uint64_t offset, uint8_t *dst,
    uint64_t len, uint64_t *bytes)
{
	if (!lilac_ufs_ready || name == NULL) {
		return ENXIO;
	}
	for (uint32_t i = 0; i < lilac_ufs_minor_count; i++) {
		if (strcmp(lilac_ufs_minors[i].name, name) == 0) {
			uint64_t sz = lu_minor_bytes(i);
			if (bytes != NULL) {
				*bytes = sz;
			}
			if (len == 0ULL) {
				return 0;
			}
			if (dst == NULL || offset > sz || len > sz - offset) {
				return EINVAL;
			}
			return lu_read_bytes(i, offset, dst, len);
		}
	}
	return ENOENT;
}

/*
 * Called from loader_return() after psci_smp_quiesce_others() on the boot CPU;
 * other CPUs are parked until device cleanup completes, then powered off.
 * Must not acquire mutexes or allocate memory.
 */
void
lilac_ufs_quiesce(void)
{
	if (!lilac_ufs_mapped || lilac_ufs_restored) {
		return;
	}
	lilac_ufs_closed = 1u;
	lilac_ufs_ready = 0u;
	lilac_ufs_barrier();

	if (!lu_wait_reg(UFSHC_UTRLDBR, ~0u, 0u, 2000u) || !lu_same_queues()) {
		return;
	}
	lilac_ufs_reg_write(UFSHC_UTRLRSR, 0u);
	if (!lu_wait_reg(UFSHC_UTRLRSR, 1u, 0u, 100u) ||
	    lilac_ufs_reg_read(UFSHC_UTRLDBR) != 0u || !lu_same_queues()) {
		return;
	}
	lu_store_tr_page(lilac_ufs_saved_tr);
	lu_store_tm_page(lilac_ufs_saved_tm);
	lilac_ufs_barrier();
	if (!lu_verify_tr_page(lilac_ufs_saved_tr) ||
	    !lu_verify_tm_page(lilac_ufs_saved_tm)) {
		return;
	}
	lilac_ufs_reg_write(UFSHC_UTRLRSR, 1u);
	if (!lu_wait_reg(UFSHC_UTRLRSR, 1u, 1u, 100u) ||
	    lilac_ufs_reg_read(UFSHC_UTRLDBR) != 0u || !lu_same_queues()) {
		return;
	}
	lilac_ufs_restored = 1u;
}

void
lilac_ufs_init(void)
{
	DTEntry entry;
	const void *prop = NULL;
	unsigned int prop_size = 0;
	uint32_t enabled = 1u;

	if (PE_parse_boot_argn("lilac_ufs", &enabled, sizeof(enabled)) && enabled == 0u) {
		printf("lilac-ufs: disabled by lilac_ufs=0 boot-arg\n");
		return;
	}
	if (SecureDTLookupEntry(NULL, "/arm-io/ufshc", &entry) != kSuccess) {
		return;
	}
	if (SecureDTGetProperty(entry, "compatible", &prop, &prop_size) != kSuccess ||
	    prop == NULL || prop_size < sizeof("qcom,ufshc") ||
	    strncmp((const char *)prop, "qcom,ufshc", prop_size) != 0) {
		printf("lilac-ufs: /arm-io/ufshc incompatible\n");
		return;
	}
	if (SecureDTGetProperty(entry, "reg", &prop, &prop_size) != kSuccess ||
	    prop == NULL || prop_size != sizeof(uint64_t) * 2u) {
		printf("lilac-ufs: /arm-io/ufshc invalid reg property\n");
		return;
	}
	const uint64_t *reg = (const uint64_t *)prop;
	if (reg[0] != UFSHC_EXPECTED_PA || reg[1] < UFSHC_MIN_REG_SIZE) {
		printf("lilac-ufs: refusing unverified MMIO range 0x%llx/0x%llx\n",
		    (unsigned long long)reg[0], (unsigned long long)reg[1]);
		return;
	}

	lilac_ufs_mmio = ml_io_map((vm_offset_t)UFSHC_EXPECTED_PA, UFSHC_MAP_SIZE);
	if (lilac_ufs_mmio == 0) {
		printf("lilac-ufs: failed to map MMIO\n");
		return;
	}

	uint32_t cap = lilac_ufs_reg_read(UFSHC_CAP);
	uint32_t ver = lilac_ufs_reg_read(UFSHC_VER);
	uint32_t is_reg = lilac_ufs_reg_read(UFSHC_IS);
	uint32_t tr_pa = lilac_ufs_reg_read(UFSHC_UTRLBA);
	uint32_t tm_pa = lilac_ufs_reg_read(UFSHC_UTMRLBA);
	lilac_ufs_tr_pa = tr_pa;
	lilac_ufs_tm_pa = tm_pa;

	if (ver != 0x210u || cap == 0u || cap == ~0u ||
	    tr_pa < 0x99000000u || tr_pa > 0x9b000000u - UFS_BLOCK_BYTES * 2u ||
	    (tr_pa & (UFS_BLOCK_BYTES - 1u)) != 0u || tm_pa != tr_pa + UFS_BLOCK_BYTES ||
	    !lu_same_queues() || lilac_ufs_reg_read(UFSHC_UTRLDBR) != 0u ||
	    lilac_ufs_reg_read(UFSHC_UTRLRSR) != 1u || (is_reg & UFSHC_IS_FATAL) != 0u) {
		printf("lilac-ufs: unexpected controller handoff state (VER=0x%x TR=0x%x TM=0x%x IS=0x%x)\n",
		    ver, tr_pa, tm_pa, is_reg);
		return;
	}
	if (pmap_valid_address((uint64_t)tr_pa) ||
	    pmap_valid_address((uint64_t)tm_pa + UFS_BLOCK_BYTES - 1ULL)) {
		printf("lilac-ufs: queue pages 0x%x..0x%x overlap XNU managed DRAM\n",
		    tr_pa, tm_pa + UFS_BLOCK_BYTES - 1u);
		return;
	}

	lilac_ufs_tr_va = ml_io_map_unmappable((vm_offset_t)tr_pa, UFS_BLOCK_BYTES * 2u, VM_WIMG_WCOMB);
	if (lilac_ufs_tr_va == 0) {
		printf("lilac-ufs: failed to map firmware queue pages at 0x%x\n", tr_pa);
		return;
	}
	lilac_ufs_tm_va = lilac_ufs_tr_va + UFS_BLOCK_BYTES;

	lu_load_tr_page(lilac_ufs_saved_tr);
	lu_load_tm_page(lilac_ufs_saved_tm);
	lilac_ufs_cmd_type = lilac_ufs_saved_tr[0] & 0x10000000u;
	if (!lu_retired_layout(lilac_ufs_saved_tr, tr_pa) || !lu_same_queues() ||
	    lilac_ufs_reg_read(UFSHC_UTRLDBR) != 0u || lilac_ufs_reg_read(UFSHC_UTRLRSR) != 1u ||
	    !lu_verify_tr_page(lilac_ufs_saved_tr) || !lu_verify_tm_page(lilac_ufs_saved_tm)) {
		printf("lilac-ufs: retired slot-0 layout validation failed at 0x%x (DW0=0x%x)\n",
		    tr_pa, lilac_ufs_saved_tr[0]);
		return;
	}
	lilac_ufs_mapped = 1u;

	lck_mtx_lock(&lilac_ufs_mtx);
	int err = lu_discover_gpt_locked();
	lck_mtx_unlock(&lilac_ufs_mtx);
	if (err != 0) {
		printf("lilac-ufs: GPT discovery failed err=%d; restoring firmware queues\n", err);
		lilac_ufs_quiesce();
		return;
	}

	lilac_ufs_bmajor = bdevsw_add(-1, &lilac_ufs_bdevsw);
	lilac_ufs_cmajor = (lilac_ufs_bmajor < 0) ? -1 :
	    cdevsw_add_with_bdev(-1, &lilac_ufs_cdevsw, lilac_ufs_bmajor);
	if (lilac_ufs_cmajor < 0) {
		printf("lilac-ufs: bdevsw/cdevsw registration failed\n");
		lilac_ufs_quiesce();
		return;
	}

	uint32_t created_pairs = 0u;
	for (uint32_t i = 0; i < lilac_ufs_minor_count; i++) {
		char bname[48], cname[48];
		snprintf(bname, sizeof(bname), "ufs_%s", lilac_ufs_minors[i].name);
		snprintf(cname, sizeof(cname), "rufs_%s", lilac_ufs_minors[i].name);
		void *bn = devfs_make_node(makedev(lilac_ufs_bmajor, i), DEVFS_BLOCK,
		    UID_ROOT, GID_OPERATOR, 0640, "%s", bname);
		void *cn = (bn != NULL) ? devfs_make_node(makedev(lilac_ufs_cmajor, i), DEVFS_CHAR,
		    UID_ROOT, GID_OPERATOR, 0640, "%s", cname) : NULL;
		if (bn != NULL && cn != NULL) {
			created_pairs++;
		} else {
			printf("lilac-ufs: devfs node creation failed for %s\n", lilac_ufs_minors[i].name);
		}
	}

	lu_build_catalog();
	lilac_ufs_ready = 1u;

	uint64_t total_mib = (((uint64_t)lilac_ufs_last_lba + 1ULL) * UFS_BLOCK_BYTES) >> 20;
	printf("lilac-ufs: LUN0 %llu MiB (last_lba=0x%x, TR=0x%x), %u GPT partitions, %u/dev pairs RO\n",
	    (unsigned long long)total_mib, lilac_ufs_last_lba, lilac_ufs_tr_pa,
	    lilac_ufs_partitions, created_pairs);
}
