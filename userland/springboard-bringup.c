/* SPDX-License-Identifier: BSD-2-Clause
 * Explicit RAM desktop bring-up, using the two 20F66 hooks documented in
 * HakiOS inject_sb.c. This is a local UI shim, not an activation record or a
 * working Setup Assistant. It is only built with --desktop-bringup.
 */
#ifndef LILAC_SB_TEST
#include <objc/runtime.h>
#endif
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern const void *_dyld_get_shared_cache_range(size_t *length);
#ifndef LILAC_SB_ACTIVATION_ADDRESS
#define LILAC_SB_ACTIVATION_ADDRESS 0x1c4bc8dfcUL
#define LILAC_SB_SETUP_ADDRESS      0x1c4cdd3a4UL
#endif
#define LILAC_SB_CACHE_BASE 0x180000000UL

static int lilac_sb_console = 2;
static BOOL (*lilac_sb_original_setup)(id, SEL, unsigned long);

static void lilac_sb_activation(id self, SEL selector)
{
    (void)selector;
    /* Exact original IMP and object size are checked before installing. */
    *(int *)((char *)self + 8) = 2;
}

static BOOL lilac_sb_setup(id self, SEL selector, unsigned long reason)
{
    static unsigned said;
    if (reason && said++ < 8)
        dprintf(lilac_sb_console, "lilac desktop: suppressing Setup reason=%lu for RAM bring-up\n", reason);
    return lilac_sb_original_setup(self, selector, 0);
}

static int lilac_sb_install(void)
{
    size_t length = 0;
    const void *cache = _dyld_get_shared_cache_range(&length);
    if (!cache || !length) return 0;
    uintptr_t slide = (uintptr_t)cache - LILAC_SB_CACHE_BASE;
    Class lockdown = objc_getClass("SBLockdownManager"), setup = objc_getClass("SBSetupManager");
    Method activation = lockdown ? class_getInstanceMethod(lockdown, sel_registerName("_setupActivationState")) : NULL;
    Method required = setup ? class_getInstanceMethod(setup, sel_registerName("_setSetupRequiredReason:")) : NULL;
    /* Preflight the complete pair: an unknown cache must not get half a shim. */
    if (!activation || !required || class_getInstanceSize(lockdown) < 12 ||
        (uintptr_t)method_getImplementation(activation) != LILAC_SB_ACTIVATION_ADDRESS + slide ||
        (uintptr_t)method_getImplementation(required) != LILAC_SB_SETUP_ADDRESS + slide ||
        !method_getTypeEncoding(activation) || strcmp(method_getTypeEncoding(activation), "v16@0:8") ||
        !method_getTypeEncoding(required) || strcmp(method_getTypeEncoding(required), "B24@0:8Q16")) {
        dprintf(lilac_sb_console, "lilac desktop: 20F66 SpringBoard ABI mismatch; original Setup retained\n");
        dprintf(lilac_sb_console, "lilac desktop: activation=%#lx type=%s; setup=%#lx type=%s; lockdown size=%zu\n",
            activation ? (uintptr_t)method_getImplementation(activation) - slide : 0,
            activation && method_getTypeEncoding(activation) ? method_getTypeEncoding(activation) : "missing",
            required ? (uintptr_t)method_getImplementation(required) - slide : 0,
            required && method_getTypeEncoding(required) ? method_getTypeEncoding(required) : "missing",
            lockdown ? class_getInstanceSize(lockdown) : 0);
        return 0;
    }
    lilac_sb_original_setup = (BOOL (*)(id, SEL, unsigned long))method_getImplementation(required);
    method_setImplementation(required, (IMP)lilac_sb_setup);
    method_setImplementation(activation, (IMP)lilac_sb_activation);
    dprintf(lilac_sb_console, "lilac desktop: 20F66 hooks installed; RAM bring-up suppresses Setup; local UI state only\n");
    return 1;
}

#ifndef LILAC_SB_TEST
__attribute__((constructor))
static void lilac_sb_start(void)
{
    const char *name = getprogname();
    if (!name || strcmp(name, "SpringBoard")) return;
    int console = open("/dev/console", O_WRONLY | O_NOCTTY);
    if (console >= 0) lilac_sb_console = console;
    (void)lilac_sb_install();
}
#endif
