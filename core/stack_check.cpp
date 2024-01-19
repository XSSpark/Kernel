/*
	This file is part of Fennix Kernel.

	Fennix Kernel is free software: you can redistribute it and/or
	modify it under the terms of the GNU General Public License as
	published by the Free Software Foundation, either version 3 of
	the License, or (at your option) any later version.

	Fennix Kernel is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Fennix Kernel. If not, see <https://www.gnu.org/licenses/>.
*/

#include <types.h>
#include <debug.h>
#include <rand.hpp>

#include "../kernel.h"

/* EXTERNC */ __weak uintptr_t __stack_chk_guard = 0;

EXTERNC __weak __no_stack_protector uintptr_t __stack_chk_guard_init(void)
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

EXTERNC __constructor __no_stack_protector void __guard_setup(void)
{
	debug("__guard_setup");
	if (__stack_chk_guard == 0)
		__stack_chk_guard = __stack_chk_guard_init();
	debug("Stack guard value: %ld", __stack_chk_guard);
}

EXTERNC __weak __noreturn __no_stack_protector void __stack_chk_fail(void)
{
	TaskingPanic();
	for (short i = 0; i < 10; i++)
		error("Stack smashing detected!");
	debug("Current stack check guard value: %#lx", __stack_chk_guard);
	KPrint("\eFF0000Stack smashing detected!");

	void *Stack = nullptr;
#if defined(a86)

#if defined(a64)
	asmv("movq %%rsp, %0"
		 : "=r"(Stack));
#elif defined(a32)
	asmv("movl %%esp, %0"
		 : "=r"(Stack));
#endif

#elif defined(aa64)

	asmv("mov %%sp, %0"
		 : "=r"(Stack));

#endif
	error("Stack address: %#lx", Stack);

	if (DebuggerIsAttached)
#ifdef a86
		asmv("int $0x3");
#elif defined(aa64)
		asmv("brk #0");
#endif

	CPU::Stop();
}

// https://github.com/gcc-mirror/gcc/blob/master/libssp/ssp.c
EXTERNC __weak __noreturn __no_stack_protector void __chk_fail(void)
{
	TaskingPanic();
	for (short i = 0; i < 10; i++)
		error("Buffer overflow detected!");
	KPrint("\eFF0000Buffer overflow detected!");

#if defined(a86)
	while (1)
		asmv("cli; hlt");
#elif defined(aa64)
	asmv("wfe");
#endif
}
