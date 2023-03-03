#include <types.h>
#include <debug.h>
#include <rand.hpp>

#include "../kernel.h"

/* EXTERNC */ __attribute__((weak)) uintptr_t __stack_chk_guard = 0;

EXTERNC __attribute__((weak, no_stack_protector)) uintptr_t __stack_chk_guard_init(void)
{
    int MaxRetries = 0;
#if UINTPTR_MAX == UINT32_MAX
    uint32_t num;
Retry:
    num = Random::rand32();
    if (num < 0x1000 && MaxRetries++ < 10)
        goto Retry;
    return num;

#else
    uint64_t num;
Retry:
    num = Random::rand64();
    if (num < 0x100000 && MaxRetries++ < 10)
        goto Retry;
    return num;
#endif
}

EXTERNC __attribute__((constructor, no_stack_protector)) void __guard_setup(void)
{
    debug("StackGuard: __guard_setup");
    if (__stack_chk_guard == 0)
        __stack_chk_guard = __stack_chk_guard_init();
    debug("Stack guard value: %ld", __stack_chk_guard);
}

EXTERNC __attribute__((weak, noreturn, no_stack_protector)) void __stack_chk_fail(void)
{
    TaskingPanic();
    for (short i = 0; i < 10; i++)
        error("Stack smashing detected!");
    debug("Current stack check guard value: %#lx", __stack_chk_guard);
    KPrint("\eFF0000Stack smashing detected!");

#if defined(__amd64__) || defined(__i386__)
    void *Stack = nullptr;
    asmv("movq %%rsp, %0"
         : "=r"(Stack));
    error("Stack address: %#lx", Stack);

    while (1)
        asmv("cli; hlt");
#elif defined(__aarch64__)
    asmv("wfe");
#endif
    CPU::Stop();
}

// https://github.com/gcc-mirror/gcc/blob/master/libssp/ssp.c
EXTERNC __attribute__((weak, noreturn, no_stack_protector)) void __chk_fail(void)
{
    TaskingPanic();
    for (short i = 0; i < 10; i++)
        error("Buffer overflow detected!");
    KPrint("\eFF0000Buffer overflow detected!");

#if defined(__amd64__) || defined(__i386__)
    while (1)
        asmv("cli; hlt");
#elif defined(__aarch64__)
    asmv("wfe");
#endif
}
