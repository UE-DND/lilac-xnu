#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
typedef bool BOOL;
typedef void *id;
typedef const char *SEL;
typedef void (*IMP)(void);
typedef unsigned Class;
typedef struct method { IMP implementation; const char *types; } *Method;
static unsigned long last_reason;
static BOOL setup_result;
static unsigned changes, fault;
static void original_activation(id self, SEL selector) { (void)self; (void)selector; }
static BOOL original_setup(id self, SEL selector, unsigned long reason) { (void)self; (void)selector; last_reason = reason; return setup_result; }
static struct method activation_method, setup_method;
static const void *_dyld_get_shared_cache_range(size_t *length)
{
    *length = fault == 2 ? 0 : 0x50000000;
    return fault == 1 ? NULL : (void *)0x180000000UL;
}
static Class objc_getClass(const char *name)
{
    return !strcmp(name, "SBLockdownManager") ? (fault == 3 ? 0 : 1) : (fault == 4 ? 0 : 2);
}
static SEL sel_registerName(const char *name) { return name; }
static Method class_getInstanceMethod(Class cls, SEL selector)
{
    (void)selector;
    return cls == 1 ? (fault == 5 ? NULL : &activation_method) : (fault == 6 ? NULL : &setup_method);
}
static size_t class_getInstanceSize(Class cls) { assert(cls == 1); return fault == 7 ? 8 : 16; }
static IMP method_getImplementation(Method m) { return m->implementation; }
static const char *method_getTypeEncoding(Method m) { return m->types; }
static IMP method_setImplementation(Method m, IMP imp)
{
    IMP old = m->implementation; m->implementation = imp; changes++; return old;
}
#define LILAC_SB_TEST 1
#define LILAC_SB_ACTIVATION_ADDRESS ((uintptr_t)original_activation)
#define LILAC_SB_SETUP_ADDRESS ((uintptr_t)original_setup)
#include "../../userland/springboard-bringup.c"

int main(void)
{
    for (fault = 0; fault <= 13; fault++) {
        changes = 0;
        activation_method = (struct method){(IMP)original_activation, "v16@0:8"};
        setup_method = (struct method){(IMP)original_setup, "B24@0:8Q16"};
        if (fault == 8) activation_method.implementation = (IMP)original_setup;
        if (fault == 9) setup_method.implementation = (IMP)original_activation;
        if (fault == 10) activation_method.types = NULL;
        if (fault == 11) setup_method.types = "v24@0:8i16";
        if (fault == 12) setup_method.types = "v24@0:8Q16";
        if (fault == 13) setup_method.types = "B24@0:8q16";
        assert(lilac_sb_install() == !fault);
        assert(changes == (fault ? 0 : 2));
        if (!fault) {
            struct { uint64_t isa; int state; int guard; } manager = {0x1234, 0, 0x5678};
            ((void (*)(id, SEL))activation_method.implementation)(&manager, "_setupActivationState");
            assert(manager.state == 2 && manager.isa == 0x1234 && manager.guard == 0x5678);
            for (unsigned value = 0; value < 2; value++) {
                setup_result = value;
                last_reason = 99;
                BOOL result = ((BOOL (*)(id, SEL, unsigned long))setup_method.implementation)(
                    &manager, "_setSetupRequiredReason:", ~0UL);
                assert(last_reason == 0 && result == setup_result);
            }
        }
    }
    puts("PASS: complete SpringBoard ABI preflight, no partial hook on mismatch, bounded local state and original setup call");
    return 0;
}
