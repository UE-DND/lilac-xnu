/*
 * Return-to-loader debug channel.
 *
 * On hardware without a serial port there is no way to get the kernel log
 * out. The XNU loader therefore leaves UEFI intact (no ExitBootServices) and
 * publishes the physical address of a return stub in /chosen/loader-return.
 * When the kernel has nothing more to do (panic, halt, timer) it collects its
 * message buffer, the panic buffer and any kprintf output, maps the stub 1:1
 * through TTBR0 and jumps to it with that log. The loader then restores the UEFI environment and writes
 * the log to storage.
 *
 * Only valid while the kernel does not drive hardware that UEFI also uses.
 */
#include <mach/mach_types.h>
#include <machine/atomic.h>
#include <kern/cpu_number.h>
#include <kern/debug.h>
#include <kern/startup.h>
#include <kern/thread_call.h>
#include <kern/clock.h>
#include <arm/cpu_internal.h>
#include <arm/machine_routines.h>
#include <arm64/psci_smp.h>
#include <arm64/proc_reg.h>
#include <pexpert/pexpert.h>
#include <pexpert/arm64/board_config.h>
#include <vm/vm_page.h>
#include <vm/vm_compressor.h>

/*
 * The console log handed back to the loader. A long session writes more than fits, and both
 * ends matter: the start of the boot is kept as it is, and of everything after it the most
 * recent part (a ring); room is left for what is appended on the way out (the kernel message
 * buffer, a panic's text). loader_log_linearize() puts the pieces in order before returning.
 */
#define LOADER_LOG_SIZE (1536 * 1024)
#define LOADER_LOG_HEAD (256 * 1024)
#define LOADER_LOG_RING (1024 * 1024)

#if defined(GENERIC_GIC_V3_IRQ)
extern void gic_v3_irq_quiesce(void);
extern void gic_v3_irq_cpu_shutdown(void);
#endif

void loader_log_putc(char c);
void loader_return(void);
extern void hakios_adreno_quiesce(void);
extern void hakios_adreno_stop(void);
extern void hakios_adreno_minute(void);
extern void hakios_wifi_stop(void);
extern void hakios_wifi_quiesce(void);
extern void hakios_adsp_stop(void);
extern void hakios_adsp_quiesce(void);
extern void hakios_mpss_stop(void);
extern void hakios_mpss_quiesce(void);
extern void hakios_ddr_quiesce(void);
extern void hakios_haptic_quiesce(void);
extern void hakios_torch_quiesce(void);
extern void hakios_camera_quiesce(void);
extern void hakios_mic_quiesce(void);
extern void hakios_ufs_quiesce(void);
extern void hakios_var_save_on_return(void);   /* drivers/storage/hakios.ufs.c: the data volume into the flash */
void loader_checkpoint(uint64_t n);
void lilac_fb_early_trap(uint64_t esr, uint64_t elr, uint64_t far, uint64_t lr);
static void lilac_fb_dump_panel(const char *title, uint64_t esr, uint64_t elr, uint64_t far, uint64_t lr, bool has_regs);
extern unsigned char iso_font[256 * 16];

/* Also referenced from the early exception vector in start.s. */
char     loader_log[LOADER_LOG_SIZE];
uint32_t loader_log_len;
static bool     loader_return_entered;

/* One L1 table and one L2 table are enough to map a single 2 MB block. */
static uint64_t loader_l1[512] __attribute__((aligned(4096)));
static uint64_t loader_l2[512] __attribute__((aligned(4096)));

static uint32_t loader_log_ring_at;      /* where the next character goes in the ring */
static bool     loader_log_wrapped;
static bool     loader_log_linear;       /* the pieces have been put in order: only appending from now on */
static char     loader_log_scratch[LOADER_LOG_RING];

/*
 * Characters come from every processor, and from interrupt handlers on top of whatever was
 * writing one. The lock is of the simplest kind, good from the first line of C code to the
 * last of a panic; whoever cannot have it for a long while (its holder is gone: a panic
 * inside this very function) goes ahead without it.
 */
static uint32_t loader_log_lock;

static void loader_log_put_locked(char c);

void
loader_log_putc(char c)
{
	uint64_t daif;
	bool locked = false;

	if (c == '\r') {
		return;
	}
	daif = __builtin_arm_rsr64("DAIF");
	__builtin_arm_wsr("DAIFSet", DAIFSC_IRQF | DAIFSC_FIQF);
	for (uint32_t tries = 0; !locked && tries < 10000000; tries++) {
		locked = os_atomic_cmpxchg(&loader_log_lock, 0, 1, acquire);
	}
	loader_log_put_locked(c);
	if (locked) {
		os_atomic_store(&loader_log_lock, 0, release);
	}
	__builtin_arm_wsr64("DAIF", daif);
}

/*
 * A whole line at once, under the lock: lines written from several processors at the same time
 * (tools/latency.py's "tl:" lines, a frame's on one and a touch's on another) are not mixed up.
 */
void loader_log_line(const char *line);

void
loader_log_line(const char *line)
{
	uint64_t daif;
	bool locked = false;

	daif = __builtin_arm_rsr64("DAIF");
	__builtin_arm_wsr("DAIFSet", DAIFSC_IRQF | DAIFSC_FIQF);
	for (uint32_t tries = 0; !locked && tries < 10000000; tries++) {
		locked = os_atomic_cmpxchg(&loader_log_lock, 0, 1, acquire);
	}
	for (; *line != 0; line++) {
		if (*line != '\r') {
			loader_log_put_locked(*line);
		}
	}
	if (locked) {
		os_atomic_store(&loader_log_lock, 0, release);
	}
	__builtin_arm_wsr64("DAIF", daif);
}

static void
loader_log_put_locked(char c)
{
	if (loader_log_linear || loader_log_len < LOADER_LOG_HEAD) {
		if (loader_log_len < LOADER_LOG_SIZE) {
			loader_log[loader_log_len++] = c;
		}
	} else {
		loader_log[LOADER_LOG_HEAD + loader_log_ring_at] = c;
		if (++loader_log_ring_at == LOADER_LOG_RING) {
			loader_log_ring_at = 0;
			loader_log_wrapped = true;
		}
		if (!loader_log_wrapped) {
			loader_log_len = LOADER_LOG_HEAD + loader_log_ring_at;
		}
	}
}

/* Oldest first: the head, a note about what was dropped, then the ring from its oldest character. */
static void
loader_log_linearize(void)
{
	static const char note[] = "\n\n[... the log between the first 256 KB and the last 1 MB was dropped ...]\n\n";
	uint32_t older = LOADER_LOG_RING - loader_log_ring_at, n = LOADER_LOG_HEAD;

	if (loader_log_linear) {
		return;
	}
	loader_log_linear = true;
	if (!loader_log_wrapped) {
		return;
	}
	__builtin_memcpy(loader_log_scratch, loader_log + LOADER_LOG_HEAD + loader_log_ring_at, older);
	__builtin_memcpy(loader_log_scratch + older, loader_log + LOADER_LOG_HEAD, loader_log_ring_at);
	/* the note takes the place of the oldest characters of the ring */
	__builtin_memcpy(loader_log + n, note, sizeof(note) - 1);
	n += sizeof(note) - 1;
	__builtin_memcpy(loader_log + n, loader_log_scratch + sizeof(note) - 1, LOADER_LOG_RING - (sizeof(note) - 1));
	loader_log_len = LOADER_LOG_HEAD + LOADER_LOG_RING;
}

/* Patched by the loader: { magic, PA of its return stub, checkpoint to stop at, PA - VA }. See start.s. */
extern uint64_t loader_stub_slot[4];

/*
 * For a loader that watches from another processor core while the kernel runs (it starts the
 * kernel on a core of its choice and stays on the first one): a sign of life, and where the log
 * is, so that the log of a kernel that hangs -- or dies before it has said a word through any
 * other channel -- is not lost with it. The loader finds this by its first word in the kernel
 * it has loaded. The addresses are the kernel's virtual ones, filled in by the linker and so
 * valid from the first instruction; the loader knows the distance to the physical ones.
 */
struct loader_live {
	uint64_t magic;                 /* "XNULIVE!" */
	uint64_t heartbeat;             /* goes up every HV_WDT_PET_SECONDS while the kernel is alive */
	const char *log;                /* loader_log */
	const uint32_t *log_len;        /* how much of it is in use */
	const uint32_t *log_ring_at;    /* where the ring part is being written */
	const bool *log_wrapped;        /* the ring has gone round */
	uint64_t log_head, log_ring;    /* sizes of the two parts */
	const uint64_t *recording;      /* loader_recording: { physical address, length } of a screen recording */
	volatile uint64_t stop;         /* the loader's to write: the host has asked for the run to end */
	uint64_t magic2;                /* "XNULIVE2": what follows is there (a loader may meet an older kernel) */
	const uint64_t *sound;          /* loader_sound: the same for a recording of the sound */
};

/*
 * A screen recording (the display driver makes one with boot-arg rec_secs): where it is, for the
 * loader to hand to the host once the kernel is back. { physical address, bytes, kernel address }.
 */
uint64_t loader_recording[3];
/* ... and of the sound (/dev/hakios-audio makes one with boot-arg rec_sound). */
uint64_t loader_sound[3];

struct loader_live loader_live = {
	.magic = 0x214556494c554e58ULL, .log = loader_log, .log_len = &loader_log_len, .log_ring_at = &loader_log_ring_at,
	.log_wrapped = &loader_log_wrapped, .log_head = LOADER_LOG_HEAD, .log_ring = LOADER_LOG_RING,
	.recording = loader_recording,
	.magic2 = 0x324556494c554e58ULL, .sound = loader_sound,
};

/* Valid from the first instruction of C code: does not depend on gVirtBase/gPhysBase being set up. */
static inline uint64_t
static_kvtophys(const void *va)
{
	return (uint64_t)va + loader_stub_slot[3];
}

/*
 * Jump to the loader's return stub. reason: 0 = orderly end, 2 = checkpoint.
 * Returns only if no loader is listening.
 */
static void
loader_jump(uint64_t reason, uint64_t code)
{
#if ARM_PGSHIFT == 12
	uint64_t stub = loader_stub_slot[1];
	uint64_t tcr, t0sz;
	uint32_t own_core = 0;

	if (stub == 0 || loader_return_entered) {
		return;
	}

	tcr = __builtin_arm_rsr64("TCR_EL1");
	t0sz = tcr & 0x3f;
	/* The TTBR0 walk must start at L1 (1 GB per entry) and reach the stub. */
	if (t0sz < 25 || t0sz > 33 || (stub >> (64 - t0sz)) != 0) {
		return;
	}
	loader_return_entered = true;
#if defined(GENERIC_GIC_V3_IRQ)
	gic_v3_irq_quiesce();
	/*
	 * With a core of its own (bootcpu=) the kernel does not go back into the loader's code on
	 * this processor: the stub leaves the log where the loader's core finds it and has the
	 * firmware switch this one off. It must go as the other cores do (psci_smp_cpu_off()), with
	 * nothing pending: after a panic the "stop" signal and the timer are.
	 */
	if (PE_parse_boot_argn("bootcpu", &own_core, sizeof(own_core))) {
		__builtin_arm_wsr64("CNTV_CTL_EL0", CNTV_CTL_EL0_IMASKED);
		gic_v3_irq_cpu_shutdown();
	}
#endif

	loader_l1[stub >> 30] = static_kvtophys(loader_l2) | ARM_TTE_TYPE_TABLE | ARM_TTE_VALID;
	loader_l2[(stub >> 21) & 0x1ff] = (stub & ~0x1fffffULL) | ARM_TTE_VALID | ARM_TTE_TYPE_BLOCK |
	    ARM_TTE_BLOCK_AF | ARM_TTE_BLOCK_SH(SH_OUTER_MEMORY) | ARM_TTE_BLOCK_AP(AP_RWNA) |
	    ARM_TTE_BLOCK_ATTRINDX(CACHE_ATTRINDX_WRITEBACK);

	for (uintptr_t p = (uintptr_t)loader_l1; p < (uintptr_t)loader_l1 + sizeof(loader_l1); p += 64) {
		__asm__ volatile ("dc cvac, %0" : : "r" (p));
	}
	for (uintptr_t p = (uintptr_t)loader_l2; p < (uintptr_t)loader_l2 + sizeof(loader_l2); p += 64) {
		__asm__ volatile ("dc cvac, %0" : : "r" (p));
	}

	/*
	 * The stub turns the MMU off, and on real hardware dirty cache lines do
	 * not survive that: write the log back to RAM first.
	 */
	for (uintptr_t p = (uintptr_t)loader_log & ~63UL; p < (uintptr_t)loader_log + loader_log_len; p += 64) {
		__asm__ volatile ("dc cvac, %0" : : "r" (p));
	}
	/* ... and a screen recording, and the note of where it is */
	for (uintptr_t p = loader_recording[2] & ~63UL; p < loader_recording[2] + loader_recording[1]; p += 64) {
		__asm__ volatile ("dc cvac, %0" : : "r" (p));
	}
	__asm__ volatile ("dc cvac, %0" : : "r" ((uintptr_t)loader_recording));
	__asm__ volatile ("dc cvac, %0" : : "r" ((uintptr_t)loader_recording + 16));
	for (uintptr_t p = loader_sound[2] & ~63UL; p < loader_sound[2] + loader_sound[1]; p += 64) {
		__asm__ volatile ("dc cvac, %0" : : "r" (p));
	}
	__asm__ volatile ("dc cvac, %0" : : "r" ((uintptr_t)loader_sound));
	__asm__ volatile ("dc cvac, %0" : : "r" ((uintptr_t)loader_sound + 16));
	__asm__ volatile ("dsb sy");

	register uint64_t r_ttbr0 __asm__("x0") = static_kvtophys(loader_l1);
	register uint64_t r_log __asm__("x2") = static_kvtophys(loader_log);
	register uint64_t r_len __asm__("x3") = loader_log_len;
	register uint64_t r_tcr __asm__("x4") = tcr;
	register uint64_t r_stub __asm__("x5") = stub;
	register uint64_t r_reason __asm__("x6") = reason;
	register uint64_t r_code __asm__("x7") = code;

	__asm__ volatile (
                "msr DAIFSet, #0xf\n"
                "bic x4, x4, #0x80\n"           /* TCR_EL1.EPD0: allow TTBR0 walks */
                "msr TCR_EL1, x4\n"
                "msr TTBR0_EL1, x0\n"
                "isb\n"
                "tlbi vmalle1\n"
                "dsb sy\n"
                "isb\n"
                "mov x0, x2\n"
                "mov x1, x3\n"
                "mov x2, x6\n"
                "mov x3, x7\n"
                "br x5\n"
                : : "r" (r_ttbr0), "r" (r_log), "r" (r_len), "r" (r_tcr), "r" (r_stub), "r" (r_reason), "r" (r_code));
	__builtin_unreachable();
#else
	(void)reason; (void)code;
#endif /* ARM_PGSHIFT == 12 */
}

extern char *debug_buf_base, *debug_buf_ptr;

/* bsd/sys/msgbuf.h (not includable from osfmk) */
struct msgbuf {
	int   msg_magic;
	int   msg_size;
	int   msg_bufx;         /* write pointer */
	int   msg_bufr;         /* read pointer */
	char *msg_bufc;         /* buffer */
};
extern struct msgbuf *msgbufp;

static void
loader_log_puts(const char *s)
{
	for (; *s; s++) {
		loader_log_putc(*s);
	}
}

/*
 * Everything printf()/IOLog()/os_log wrote ends up in the kernel message
 * buffer (what dmesg shows), whether or not a console displayed it.
 */
static void
loader_log_append_msgbuf(void)
{
	struct msgbuf *mb = msgbufp;

	if (mb == NULL || mb->msg_bufc == NULL || mb->msg_size <= 0 || mb->msg_bufx < 0 || mb->msg_bufx > mb->msg_size) {
		return;
	}
	loader_log_puts("\n--- kernel message buffer ---\n");
	/* oldest data first: [bufx, size) is only meaningful once the buffer has wrapped */
	for (int pass = 0; pass < 2; pass++) {
		int from = pass == 0 ? mb->msg_bufx : 0;
		int to = pass == 0 ? mb->msg_size : mb->msg_bufx;
		for (int i = from; i < to; i++) {
			if (mb->msg_bufc[i] != 0) {
				loader_log_putc(mb->msg_bufc[i]);
			}
		}
	}
}

/* debug_buf_ptr includes binary stackshot data after the textual panic. */
static void
loader_log_append_panic_text(void)
{
	if (panic_info == NULL || panic_info->eph_magic != EMBEDDED_PANIC_MAGIC) {
		return;
	}
	uint32_t offset = panic_info->eph_panic_log_offset;
	uint32_t length = panic_info->eph_panic_log_len;
	if (offset < sizeof(*panic_info) || offset > gPanicSize ||
	    length == 0 || length > gPanicSize - offset || length >= 256 * 1024) {
		return;
	}
	/* Never consume stackshot bytes, even if a nested panic left an incoherent header. */
	uint32_t stackshot = panic_info->eph_stackshot_offset;
	if (stackshot != 0 && (stackshot < offset || length > stackshot - offset)) {
		return;
	}
	loader_log_puts("\n--- panic text ---\n");
	const char *text = (const char *)(gPanicBase + offset);
	for (uint32_t i = 0; i < length; i++) {
		loader_log_putc(text[i]);
	}
}

void
loader_return(void)
{
#if defined(GENERIC_PSCI_SMP)
	/*
	 * With more than one processor: the others switch themselves off first, and the rest of
	 * this is done by the boot processor, the one the loader gave us. This call comes back
	 * on that processor only (on another one it has it call loader_return() in its turn).
	 */
	psci_smp_stop_others();
#endif
	hakios_ufs_quiesce();          /* this kernel's requests to the storage done, UEFI's list as it was, for the log */
	hakios_adreno_quiesce();       /* the GPU block, if it was started, has tables in memory that is about to be reused */
	hakios_wifi_quiesce();         /* the Wi-Fi DSP, if it still runs (a panic), is not left to itself */
	hakios_adsp_quiesce();         /* nor the audio DSP */
	hakios_mpss_quiesce();         /* nor the modem */
	hakios_ddr_quiesce();          /* and the vote for the memory's bandwidth taken away */
	hakios_haptic_quiesce();       /* the vibration motor's chip back into reset, if it was let out */
	hakios_torch_quiesce();        /* the flashlight off, if it was lit */
	hakios_camera_quiesce();       /* the camera powered down, if it was powered up */
	hakios_mic_quiesce();          /* (what the microphones' path did, said again: it happens in the log's middle) */
	/*
	 * A panic's text is first collected in the panic buffer and only later
	 * flushed to the console; if the console itself is what failed, that
	 * never happens. Append the buffer so the loader gets it either way.
	 */
	loader_log_linearize();
	if (!loader_return_entered) {
		loader_log_append_msgbuf();
	}
	if (!loader_return_entered) {
		loader_log_append_panic_text();
	}
	lilac_fb_dump_panel(">>> XNU LOADER_RETURN / PANIC LOG <<<", 0, 0, 0, 0, false);
	loader_jump(0, 0);
}

#define LILAC_FB_BASE   0x9d400000ULL
#define LILAC_FB_WIDTH  720
#define LILAC_FB_HEIGHT 1280

static volatile uint8_t *
lilac_fb_begin(uint64_t *saved_ttbr0, uint64_t *saved_tcr, uint64_t *saved_daif)
{
	uint64_t daif = __builtin_arm_rsr64("DAIF");
	__asm__ volatile ("msr DAIFSet, #0xf" ::: "memory");
	uint64_t ttbr0 = __builtin_arm_rsr64("TTBR0_EL1");
	uint64_t tcr = __builtin_arm_rsr64("TCR_EL1");
	uint64_t t0sz = tcr & 0x3f;

	*saved_daif = daif;
	*saved_ttbr0 = ttbr0;
	*saved_tcr = tcr;

	if (t0sz < 25 || t0sz > 33) {
		return NULL;
	}

	loader_l1[LILAC_FB_BASE >> 30] = static_kvtophys(loader_l2) | ARM_TTE_TYPE_TABLE | ARM_TTE_VALID;
	for (uint64_t off = 0; off < 0x400000ULL; off += 0x200000ULL) {
		uint64_t pa = (LILAC_FB_BASE + off) & ~0x1fffffULL;
		loader_l2[(pa >> 21) & 0x1ff] = pa | ARM_TTE_VALID | ARM_TTE_TYPE_BLOCK |
		    ARM_TTE_BLOCK_AF | ARM_TTE_BLOCK_AP(AP_RWNA) |
		    ARM_TTE_BLOCK_ATTRINDX(CACHE_ATTRINDX_DISABLE);
	}
	for (uintptr_t p = (uintptr_t)loader_l1; p < (uintptr_t)loader_l1 + sizeof(loader_l1); p += 64) {
		__asm__ volatile ("dc cvac, %0" : : "r" (p));
	}
	for (uintptr_t p = (uintptr_t)loader_l2; p < (uintptr_t)loader_l2 + sizeof(loader_l2); p += 64) {
		__asm__ volatile ("dc cvac, %0" : : "r" (p));
	}
	__asm__ volatile ("dsb sy" ::: "memory");

	__builtin_arm_wsr64("TCR_EL1", tcr & ~0x80ULL);
	__builtin_arm_wsr64("TTBR0_EL1", static_kvtophys(loader_l1));
	__asm__ volatile (
		"isb\n"
		"tlbi vmalle1\n"
		"dsb sy\n"
		"isb\n"
		::: "memory"
	);
	return (volatile uint8_t *)LILAC_FB_BASE;
}

static void
lilac_fb_end(uint64_t saved_ttbr0, uint64_t saved_tcr, uint64_t saved_daif)
{
	__asm__ volatile ("dsb sy" ::: "memory");
	__builtin_arm_wsr64("TTBR0_EL1", saved_ttbr0);
	__builtin_arm_wsr64("TCR_EL1", saved_tcr);
	__asm__ volatile (
		"isb\n"
		"tlbi vmalle1\n"
		"dsb sy\n"
		"isb\n"
		::: "memory"
	);
	__builtin_arm_wsr64("DAIF", saved_daif);
}

static void
lilac_fb_px(volatile uint8_t *fb, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
	if (x < 0 || x >= LILAC_FB_WIDTH || y < 0 || y >= LILAC_FB_HEIGHT) {
		return;
	}
	size_t off = (size_t)(y * LILAC_FB_WIDTH + x) * 3u;
	fb[off + 0] = b;
	fb[off + 1] = g;
	fb[off + 2] = r;
}

static void
lilac_fb_char(volatile uint8_t *fb, int cx, int cy, char ch, int scale, uint8_t r, uint8_t g, uint8_t b)
{
	unsigned char uc = (ch >= 32 && ch <= 126) ? (unsigned char)ch : (unsigned char)'.';
	const unsigned char *glyph = iso_font + ((unsigned int)uc * 16u);
	for (int row = 0; row < 16; row++) {
		unsigned char bits = glyph[row];
		for (int col = 0; col < 8; col++) {
			if (bits & (1u << col)) {
				if (scale == 2) {
					lilac_fb_px(fb, cx + col * 2,     cy + row * 2,     r, g, b);
					lilac_fb_px(fb, cx + col * 2 + 1, cy + row * 2,     r, g, b);
					lilac_fb_px(fb, cx + col * 2,     cy + row * 2 + 1, r, g, b);
					lilac_fb_px(fb, cx + col * 2 + 1, cy + row * 2 + 1, r, g, b);
				} else {
					lilac_fb_px(fb, cx + col,     cy + row, r, g, b);
					lilac_fb_px(fb, cx + col + 1, cy + row, r, g, b);
				}
			}
		}
	}
}

static void
lilac_fb_str(volatile uint8_t *fb, int x, int y, const char *s, int scale, uint8_t r, uint8_t g, uint8_t b)
{
	int step = (scale == 2) ? 18 : 9;
	while (*s) {
		lilac_fb_char(fb, x, y, *s++, scale, r, g, b);
		x += step;
	}
}

static void
lilac_fb_hex(volatile uint8_t *fb, int x, int y, const char *label, uint64_t val, uint8_t r, uint8_t g, uint8_t b)
{
	static const char hx[] = "0123456789abcdef";
	lilac_fb_str(fb, x, y, label, 1, r, g, b);
	while (*label) {
		x += 9;
		label++;
	}
	lilac_fb_char(fb, x, y, '0', 1, r, g, b); x += 9;
	lilac_fb_char(fb, x, y, 'x', 1, r, g, b); x += 9;
	for (int sh = 60; sh >= 0; sh -= 4) {
		lilac_fb_char(fb, x, y, hx[(val >> sh) & 0xf], 1, r, g, b);
		x += 9;
	}
}

static void
lilac_fb_dump_panel(const char *title, uint64_t esr, uint64_t elr, uint64_t far, uint64_t lr, bool has_regs)
{
	uint64_t ttbr0, tcr, daif;
	volatile uint8_t *fb = lilac_fb_begin(&ttbr0, &tcr, &daif);
	if (fb == NULL) {
		return;
	}

	int y0 = 560;
	for (int y = y0; y < 1268; y++) {
		size_t off = (size_t)(y * LILAC_FB_WIDTH + 12) * 3u;
		for (int x = 12; x < 708; x++) {
			fb[off + 0] = 28;
			fb[off + 1] = 20;
			fb[off + 2] = 16;
			off += 3;
		}
	}

	lilac_fb_str(fb, 24, y0 + 8, title, 1, 255, 110, 100);
	lilac_fb_hex(fb, 24, y0 + 28, "LogLen: ", loader_log_len, 255, 220, 100);
	int y = y0 + 48;
	if (has_regs) {
		lilac_fb_hex(fb, 24,  y, "ESR: ", esr, 160, 215, 255);
		lilac_fb_hex(fb, 360, y, "ELR: ", elr, 160, 215, 255);
		y += 18;
		lilac_fb_hex(fb, 24,  y, "FAR: ", far, 160, 215, 255);
		lilac_fb_hex(fb, 360, y, "LR:  ", lr,  160, 215, 255);
		y += 22;
	}

	if (loader_log_len > 0) {
		uint32_t len = loader_log_len;
		if (len > LOADER_LOG_SIZE) len = LOADER_LOG_SIZE;
		uint32_t start = len;
		int lines = 0;
		while (start > 0 && lines < 32) {
			start--;
			if (loader_log[start] == '\n') lines++;
		}
		if (start > 0 && loader_log[start] == '\n') start++;
		int x = 24;
		for (uint32_t i = start; i < len && y <= 1246; i++) {
			char c = loader_log[i];
			if (c == '\r') continue;
			if (c == '\n') {
				y += 18;
				x = 24;
			} else {
				lilac_fb_char(fb, x, y, c, 1, 235, 240, 245);
				x += 9;
				if (x > 690) {
					y += 18;
					x = 24;
				}
			}
		}
	}
	lilac_fb_end(ttbr0, tcr, daif);
}

void
lilac_fb_early_trap(uint64_t esr, uint64_t elr, uint64_t far, uint64_t lr)
{
	loader_log_linearize();
	lilac_fb_dump_panel(">>> XNU EARLY KERNEL SYNCHRONOUS TRAP <<<", esr, elr, far, lr, true);
	for (;;) {
		__asm__ volatile ("wfe");
	}
}
void
loader_checkpoint(uint64_t n)
{
	int slot = -1;
	const char *lbl = NULL;
	switch (n) {
	case 10:  slot = 0;  lbl = "10"; break;
	case 11:  slot = 1;  lbl = "11"; break;
	case 30:  slot = 2;  lbl = "30"; break;
	case 35:  slot = 3;  lbl = "35"; break;
	case 12:  slot = 4;  lbl = "12"; break;
	case 16:  slot = 5;  lbl = "16"; break;
	case 161: slot = 6;  lbl = "V1"; break;
	case 162: slot = 7;  lbl = "V2"; break;
	case 163: slot = 8;  lbl = "V3"; break;
	case 164: slot = 9;  lbl = "V4"; break;
	case 17:  slot = 10; lbl = "17"; break;
	case 20:  slot = 11; lbl = "20"; break;
	}
	if (slot >= 0) {
		uint64_t ttbr0, tcr, daif;
		volatile uint8_t *fb = lilac_fb_begin(&ttbr0, &tcr, &daif);
		if (fb != NULL) {
			int x0 = 40 + slot * 52;
			for (int y = 264; y < 288; y++) {
				for (int x = x0; x < x0 + 44; x++) {
					lilac_fb_px(fb, x, y, 0, 145, 40);
				}
			}
			lilac_fb_str(fb, x0 + 13, 268, lbl, 1, 255, 255, 255);
			lilac_fb_end(ttbr0, tcr, daif);
		}
	}
	if (loader_stub_slot[2] == n) {
		loader_jump(2, n);
	}
}

/*
 * boot-arg loader_return_after=<seconds>: hand control back to the loader
 * after that long, whatever the kernel is doing. This is how the log gets out
 * when the kernel does not panic but simply waits (e.g. for a root device).
 */
/*
 * For trying the ways out when there is more than one processor: boot-arg return_cpu=<n> has
 * that processor make the timed return, from an interrupt handler -- any but the boot processor
 * has to hand it over (psci_smp_stop_others()) --, and return_panic=1 makes a panic of it.
 */
static void
loader_return_test(void *as_panic)
{
	if (as_panic != (void *)1) {
		panic("return_panic: the panic that was asked for, on cpu %d", cpu_number());
	}
	loader_return();
}

static void
loader_return_timer(__unused thread_call_param_t p0, __unused thread_call_param_t p1)
{
	static const char banner[] = "\n--- loader_return_after expired ---\n";
	uint32_t cpu = 0, as_panic = 0;

	for (const char *c = banner; *c; c++) {
		loader_log_putc(*c);
	}
	PE_parse_boot_argn("return_panic", &as_panic, sizeof(as_panic));
	if (!as_panic) {
		hakios_var_save_on_return();   /* the data volume kept, while the system is still whole */
		hakios_adreno_stop();          /* a GPU kept for the system is given back while there is a thread to wait in */
		hakios_wifi_stop();            /* and the Wi-Fi DSP, if it runs, shut down */
		hakios_adsp_stop();            /* and the audio DSP */
		hakios_mpss_stop();            /* and the modem */
	}
	if (PE_parse_boot_argn("return_cpu", &cpu, sizeof(cpu)) && cpu <= (uint32_t)ml_get_max_cpu_number() &&
	    cpu_xcall((int)cpu, loader_return_test, (void *)(uintptr_t)(as_panic ? 2 : 1)) == KERN_SUCCESS) {
		for (;;) {
			delay_for_interval(1000, NSEC_PER_MSEC);        /* that processor ends it */
		}
	}
	loader_return_test((void *)(uintptr_t)(as_panic ? 2 : 1));
}

__startup_func
static void
loader_return_timer_init(void)
{
	uint32_t seconds = 0;
	uint64_t deadline;

	if (!PE_parse_boot_argn("loader_return_after", &seconds, sizeof(seconds)) || seconds == 0) {
		return;
	}
	thread_call_t call = thread_call_allocate(loader_return_timer, NULL);
	clock_interval_to_deadline(seconds, NSEC_PER_SEC, &deadline);
	thread_call_enter_delayed(call, deadline);
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, loader_return_timer_init);

/*
 * A loader that watches from another core also listens to the host, and passes on a request to
 * end the run ("xl stop") by setting loader_live.stop. Look once a second.
 */
static thread_call_t loader_stop_call;

static void
loader_stop_poll(__unused thread_call_param_t p0, __unused thread_call_param_t p1)
{
	uint64_t deadline;

	if (loader_live.stop) {
		static const char banner[] = "\n--- the host asked for the end of the run: back to the loader ---\n";
		for (const char *c = banner; *c; c++) {
			loader_log_putc(*c);
		}
		hakios_var_save_on_return();
		hakios_adreno_stop();
		hakios_wifi_stop();
		hakios_adsp_stop();
		loader_return();
	}
	clock_interval_to_deadline(1, NSEC_PER_SEC, &deadline);
	thread_call_enter_delayed(loader_stop_call, deadline);
}

__startup_func
static void
loader_stop_poll_init(void)
{
	loader_stop_call = thread_call_allocate(loader_stop_poll, NULL);
	loader_stop_poll(NULL, NULL);
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, loader_stop_poll_init);

/*
 * boot-arg hv_wdt_pet=1: the loader has armed the Qualcomm hypervisor's
 * virtual watchdog (it resets the phone when a kernel hangs, so that test
 * runs need nobody at the device). Its timeout is at most 65 seconds; feed
 * it from a timer so that a healthy kernel can run for as long as it likes.
 */
#define VIRT_WDT_PET            0x86000007u
#define HV_WDT_PET_SECONDS      10

static thread_call_t hv_wdt_call;
static uint32_t hv_wdt_enabled;

#define MEMORY_REPORT_BEATS     6               /* a line about memory every so many heartbeats */

extern unsigned int memorystatus_available_pages, memorystatus_available_pages_critical,
    memorystatus_available_pages_critical_base;
extern uint32_t c_segment_pages_compressed;
extern _Atomic uint64_t ramfs_pages_in_use;
extern void MiniIOSurfaceUsage(uint32_t *count, uint64_t *bytes);

/*
 * Where the memory is, in megabytes: for reading a session's log afterwards. "available" is what
 * the kernel's own count says (free and file-backed): below the first threshold it kills idle
 * daemons, and once none is left and it is below the second, whatever is next in line -- the
 * window server included.
 */
void hakios_memory_line(char *buf, size_t len);

/* The line itself (also kern.hakios_memory, for a job list to keep: bsd/miscfs/apfs/apfs_vfsops.c) */
void
hakios_memory_line(char *buf, size_t len)
{
#define MB(pages)       ((unsigned int)(((uint64_t)(pages) * PAGE_SIZE) >> 20))
	uint32_t surfaces = 0;
	uint64_t surface_bytes = 0;

	MiniIOSurfaceUsage(&surfaces, &surface_bytes);
	snprintf(buf, len, "memory: free %u, files %u, programs %u, wired %u (%u surfaces %u, ramfs %u), compressor %u "
	    "(holding %u) MB; available %u, kills below %u (the idle) and %u (anybody)\n",
	    MB(vm_page_free_count), MB(vm_page_pageable_external_count), MB(vm_page_pageable_internal_count),
	    MB(vm_page_wire_count), surfaces, (unsigned int)(surface_bytes >> 20),
	    MB(os_atomic_load(&ramfs_pages_in_use, relaxed)),
	    MB(VM_PAGE_COMPRESSOR_COUNT), MB(c_segment_pages_compressed),
	    MB(memorystatus_available_pages),
	    MB(memorystatus_available_pages_critical), MB(memorystatus_available_pages_critical_base));
#undef MB
}

static void
memory_report(void)
{
	char line[256];

	hakios_memory_line(line, sizeof(line));
	printf("%s", line);
	hakios_adreno_minute();                /* (and how warm the GPU is, if it is in use) */
}

/* Every HV_WDT_PET_SECONDS: feed the watchdog if there is one, and show any watching loader a sign of life. */
static void
hv_wdt_pet(__unused thread_call_param_t p0, __unused thread_call_param_t p1)
{
	uint64_t deadline;

	if (hv_wdt_enabled) {
		register uint64_t x0 __asm__("x0") = VIRT_WDT_PET;
		register uint64_t x1 __asm__("x1") = 0;
		register uint64_t x2 __asm__("x2") = 0;
		register uint64_t x3 __asm__("x3") = 0;

		__asm__ volatile ("smc #0" : "+r" (x0), "+r" (x1), "+r" (x2), "+r" (x3) : :
		    "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17");
	}
	if (++loader_live.heartbeat % MEMORY_REPORT_BEATS == 0) {
		memory_report();
	}

	clock_interval_to_deadline(HV_WDT_PET_SECONDS, NSEC_PER_SEC, &deadline);
	thread_call_enter_delayed(hv_wdt_call, deadline);
}

__startup_func
static void
hv_wdt_pet_init(void)
{
	PE_parse_boot_argn("hv_wdt_pet", &hv_wdt_enabled, sizeof(hv_wdt_enabled));
	hv_wdt_call = thread_call_allocate(hv_wdt_pet, NULL);
	hv_wdt_pet(NULL, NULL);
}
STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, hv_wdt_pet_init);
