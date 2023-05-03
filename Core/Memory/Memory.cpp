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

#include <memory.hpp>

#include <convert.h>
#include <lock.hpp>
#include <debug.h>
#ifdef DEBUG
#include <uart.hpp>
#endif

#include "HeapAllocators/Xalloc/Xalloc.hpp"
#include "../Library/liballoc_1_1.h"
#include "../../kernel.h"

// #define DEBUG_ALLOCATIONS_SL 1
// #define DEBUG_ALLOCATIONS 1

#ifdef DEBUG_ALLOCATIONS
#define memdbg(m, ...)       \
    debug(m, ##__VA_ARGS__); \
    __sync
#else
#define memdbg(m, ...)
#endif

#ifdef DEBUG_ALLOCATIONS_SL
NewLock(AllocatorLock);
NewLock(OperatorAllocatorLock);
#endif

using namespace Memory;

Physical KernelAllocator;
PageTable *KernelPageTable = nullptr;
bool Page1GBSupport = false;
bool PSESupport = false;

static MemoryAllocatorType AllocatorType = MemoryAllocatorType::Pages;
Xalloc::V1 *XallocV1Allocator = nullptr;

#ifdef DEBUG
NIF void tracepagetable(PageTable *pt)
{
    for (int i = 0; i < 512; i++)
    {
#if defined(a64)
        if (pt->Entries[i].Present)
            debug("Entry %03d: %x %x %x %x %x %x %x %p-%#llx", i,
                  pt->Entries[i].Present, pt->Entries[i].ReadWrite,
                  pt->Entries[i].UserSupervisor, pt->Entries[i].WriteThrough,
                  pt->Entries[i].CacheDisable, pt->Entries[i].Accessed,
                  pt->Entries[i].ExecuteDisable, pt->Entries[i].Address << 12,
                  pt->Entries[i]);
#elif defined(a32)
#elif defined(aa64)
#endif
    }
}
#endif

NIF void MapFromZero(PageTable *PT, BootInfo *Info)
{
    debug("Mapping from 0x0 to %#llx", Info->Memory.Size);
    Virtual va = Virtual(PT);
    size_t MemSize = Info->Memory.Size;

    if (Page1GBSupport && PSESupport)
    {
        /* Map the first 100MB of memory as 4KB pages */

        // uintptr_t Physical4KBSectionStart = 0x10000000;
        // va.Map((void *)0,
        //        (void *)0,
        //        Physical4KBSectionStart,
        //        PTFlag::RW);

        // va.Map((void *)Physical4KBSectionStart,
        //        (void *)Physical4KBSectionStart,
        //        MemSize - Physical4KBSectionStart,
        //        PTFlag::RW,
        //        Virtual::MapType::OneGB);

        va.Map((void *)0, (void *)0, MemSize, PTFlag::RW);
    }
    else
        va.Map((void *)0, (void *)0, MemSize, PTFlag::RW);

    va.Unmap((void *)0);
}

NIF void MapFramebuffer(PageTable *PT, BootInfo *Info)
{
    debug("Mapping Framebuffer");
    Virtual va = Virtual(PT);
    int itrfb = 0;
    while (1)
    {
        if (!Info->Framebuffer[itrfb].BaseAddress)
            break;

        va.OptimizedMap((void *)Info->Framebuffer[itrfb].BaseAddress,
                        (void *)Info->Framebuffer[itrfb].BaseAddress,
                        Info->Framebuffer[itrfb].Pitch * Info->Framebuffer[itrfb].Height,
                        PTFlag::RW | PTFlag::US | PTFlag::G);
        itrfb++;

#ifdef DEBUG
        if (EnableExternalMemoryTracer)
        {
            char LockTmpStr[64];
            strcpy_unsafe(LockTmpStr, __FUNCTION__);
            strcat_unsafe(LockTmpStr, "_memTrk");
            mExtTrkLock.TimeoutLock(LockTmpStr, 10000);
            sprintf(mExtTrkLog, "Rsrv( %p %ld )\n\r",
                    Info->Framebuffer[itrfb].BaseAddress,
                    (Info->Framebuffer[itrfb].Pitch * Info->Framebuffer[itrfb].Height) + PAGE_SIZE);
            UniversalAsynchronousReceiverTransmitter::UART mTrkUART = UniversalAsynchronousReceiverTransmitter::UART(UniversalAsynchronousReceiverTransmitter::COM3);
            for (short i = 0; i < MEM_TRK_MAX_SIZE; i++)
            {
                if (mExtTrkLog[i] == '\r')
                    break;
                mTrkUART.Write(mExtTrkLog[i]);
            }
            mExtTrkLock.Unlock();
        }
#endif
    }
}

NIF void MapKernel(PageTable *PT, BootInfo *Info)
{
    debug("Mapping Kernel");
    uintptr_t KernelStart = (uintptr_t)&_kernel_start;
    uintptr_t KernelTextEnd = (uintptr_t)&_kernel_text_end;
    uintptr_t KernelDataEnd = (uintptr_t)&_kernel_data_end;
    uintptr_t KernelRoDataEnd = (uintptr_t)&_kernel_rodata_end;
    uintptr_t KernelEnd = (uintptr_t)&_kernel_end;
    uintptr_t KernelFileStart = (uintptr_t)Info->Kernel.FileBase;
    uintptr_t KernelFileEnd = KernelFileStart + Info->Kernel.Size;

    debug("File size: %ld KB", TO_KB(Info->Kernel.Size));
    debug(".text size: %ld KB", TO_KB(KernelTextEnd - KernelStart));
    debug(".data size: %ld KB", TO_KB(KernelDataEnd - KernelTextEnd));
    debug(".rodata size: %ld KB", TO_KB(KernelRoDataEnd - KernelDataEnd));
    debug(".bss size: %ld KB", TO_KB(KernelEnd - KernelRoDataEnd));

    uintptr_t BaseKernelMapAddress = (uintptr_t)Info->Kernel.PhysicalBase;
    uintptr_t k;
    Virtual va = Virtual(PT);

    /* Text section */
    for (k = KernelStart; k < KernelTextEnd; k += PAGE_SIZE)
    {
        va.Map((void *)k, (void *)BaseKernelMapAddress, PTFlag::RW | PTFlag::G);
        KernelAllocator.ReservePage((void *)BaseKernelMapAddress);
        BaseKernelMapAddress += PAGE_SIZE;
    }

    /* Data section */
    for (k = KernelTextEnd; k < KernelDataEnd; k += PAGE_SIZE)
    {
        va.Map((void *)k, (void *)BaseKernelMapAddress, PTFlag::RW | PTFlag::G);
        KernelAllocator.ReservePage((void *)BaseKernelMapAddress);
        BaseKernelMapAddress += PAGE_SIZE;
    }

    /* Read only data section */
    for (k = KernelDataEnd; k < KernelRoDataEnd; k += PAGE_SIZE)
    {
        va.Map((void *)k, (void *)BaseKernelMapAddress, PTFlag::G);
        KernelAllocator.ReservePage((void *)BaseKernelMapAddress);
        BaseKernelMapAddress += PAGE_SIZE;
    }

    /* BSS section */
    for (k = KernelRoDataEnd; k < KernelEnd; k += PAGE_SIZE)
    {
        va.Map((void *)k, (void *)BaseKernelMapAddress, PTFlag::RW | PTFlag::G);
        KernelAllocator.ReservePage((void *)BaseKernelMapAddress);
        BaseKernelMapAddress += PAGE_SIZE;
    }

    /* Kernel file */
    for (k = KernelFileStart; k < KernelFileEnd; k += PAGE_SIZE)
    {
        va.Map((void *)k, (void *)k, PTFlag::G);
        KernelAllocator.ReservePage((void *)k);
    }

#ifdef DEBUG
    if (EnableExternalMemoryTracer)
    {
        char LockTmpStr[64];
        strcpy_unsafe(LockTmpStr, __FUNCTION__);
        strcat_unsafe(LockTmpStr, "_memTrk");
        mExtTrkLock.TimeoutLock(LockTmpStr, 10000);
        sprintf(mExtTrkLog, "Rsrv( %p %ld )\n\r",
                Info->Kernel.PhysicalBase,
                Info->Kernel.Size);
        UniversalAsynchronousReceiverTransmitter::UART mTrkUART = UniversalAsynchronousReceiverTransmitter::UART(UniversalAsynchronousReceiverTransmitter::COM3);
        for (short i = 0; i < MEM_TRK_MAX_SIZE; i++)
        {
            if (mExtTrkLog[i] == '\r')
                break;
            mTrkUART.Write(mExtTrkLog[i]);
        }

        sprintf(mExtTrkLog, "Rsrv( %p %ld )\n\r",
                Info->Kernel.VirtualBase,
                Info->Kernel.Size);
        mExtTrkLock.Unlock();
        for (short i = 0; i < MEM_TRK_MAX_SIZE; i++)
        {
            if (mExtTrkLog[i] == '\r')
                break;
            mTrkUART.Write(mExtTrkLog[i]);
        }
    }
#endif
}

NIF void InitializeMemoryManagement(BootInfo *Info)
{
#ifdef DEBUG
    for (uint64_t i = 0; i < Info->Memory.Entries; i++)
    {
        uintptr_t Base = r_cst(uintptr_t, Info->Memory.Entry[i].BaseAddress);
        size_t Length = Info->Memory.Entry[i].Length;
        uintptr_t End = Base + Length;
        const char *Type = "Unknown";

        switch (Info->Memory.Entry[i].Type)
        {
        case likely(Usable):
            Type = "Usable";
            break;
        case Reserved:
            Type = "Reserved";
            break;
        case ACPIReclaimable:
            Type = "ACPI Reclaimable";
            break;
        case ACPINVS:
            Type = "ACPI NVS";
            break;
        case BadMemory:
            Type = "Bad Memory";
            break;
        case BootloaderReclaimable:
            Type = "Bootloader Reclaimable";
            break;
        case KernelAndModules:
            Type = "Kernel and Modules";
            break;
        case Framebuffer:
            Type = "Framebuffer";
            break;
        default:
            break;
        }

        debug("%ld: %p-%p %s",
              i,
              Base,
              End,
              Type);
    }
#endif
    trace("Initializing Physical Memory Manager");
    // KernelAllocator = Physical(); <- Already called in the constructor
    KernelAllocator.Init(Info);
    debug("Memory Info: %lldMB / %lldMB (%lldMB reserved)",
          TO_MB(KernelAllocator.GetUsedMemory()),
          TO_MB(KernelAllocator.GetTotalMemory()),
          TO_MB(KernelAllocator.GetReservedMemory()));

    /* -- Debugging --
        size_t bmap_size = KernelAllocator.GetPageBitmap().Size;
        for (size_t i = 0; i < bmap_size; i++)
        {
            bool idx = KernelAllocator.GetPageBitmap().Get(i);
            if (idx == true)
                debug("Page %04d: %#lx", i, i * PAGE_SIZE);
        }

        inf_loop debug("Alloc.: %#lx", KernelAllocator.RequestPage());
    */

    trace("Initializing Virtual Memory Manager");
    KernelPageTable = (PageTable *)KernelAllocator.RequestPages(TO_PAGES(PAGE_SIZE + 1));
    memset(KernelPageTable, 0, PAGE_SIZE);

    if (strcmp(CPU::Vendor(), x86_CPUID_VENDOR_AMD) == 0)
    {
        CPU::x86::AMD::CPUID0x80000001 cpuid;
        cpuid.Get();
        PSESupport = cpuid.EDX.PSE;
        Page1GBSupport = cpuid.EDX.Page1GB;
    }
    else if (strcmp(CPU::Vendor(), x86_CPUID_VENDOR_INTEL) == 0)
    {
        CPU::x86::Intel::CPUID0x80000001 cpuid;
        cpuid.Get();
        fixme("Intel PSE support");
    }

    if (Page1GBSupport && PSESupport)
    {
        debug("1GB Page Support Enabled");
#if defined(a64)
        CPU::x64::CR4 cr4 = CPU::x64::readcr4();
        cr4.PSE = 1;
        CPU::x64::writecr4(cr4);
#elif defined(a32)
        CPU::x32::CR4 cr4 = CPU::x32::readcr4();
        cr4.PSE = 1;
        CPU::x32::writecr4(cr4);
#elif defined(aa64)
#endif
    }

    MapFromZero(KernelPageTable, Info);
    MapFramebuffer(KernelPageTable, Info);
    MapKernel(KernelPageTable, Info);

    trace("Applying new page table from address %#lx", KernelPageTable);
#ifdef DEBUG
    tracepagetable(KernelPageTable);
#endif
#if defined(a86)
    asmv("mov %0, %%cr3" ::"r"(KernelPageTable));
#elif defined(aa64)
    asmv("msr ttbr0_el1, %0" ::"r"(KernelPageTable));
#endif
    debug("Page table updated.");
    if (strstr(Info->Kernel.CommandLine, "xallocv1"))
    {
        XallocV1Allocator = new Xalloc::V1((void *)KERNEL_HEAP_BASE, false, false);
        AllocatorType = MemoryAllocatorType::XallocV1;
        trace("XallocV1 Allocator initialized (%p)", XallocV1Allocator);
    }
    else if (strstr(Info->Kernel.CommandLine, "liballoc11"))
    {
        AllocatorType = MemoryAllocatorType::liballoc11;
    }
}

void *malloc(size_t Size)
{
#ifdef DEBUG_ALLOCATIONS_SL
    SmartLockClass lock___COUNTER__(AllocatorLock, (KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown"));
#endif
    memdbg("malloc(%d)->[%s]", Size, KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown");

    void *ret = nullptr;
    switch (AllocatorType)
    {
    case MemoryAllocatorType::Pages:
    {
        ret = KernelAllocator.RequestPages(TO_PAGES(Size + 1));
        memset(ret, 0, Size);
        break;
    }
    case MemoryAllocatorType::XallocV1:
    {
        ret = XallocV1Allocator->malloc(Size);
        break;
    }
    case MemoryAllocatorType::liballoc11:
    {
        ret = PREFIX(malloc)(Size);
        memset(ret, 0, Size);
        break;
    }
    default:
        throw;
    }
#ifdef DEBUG
    if (EnableExternalMemoryTracer)
    {
        char LockTmpStr[64];
        strcpy_unsafe(LockTmpStr, __FUNCTION__);
        strcat_unsafe(LockTmpStr, "_memTrk");
        mExtTrkLock.TimeoutLock(LockTmpStr, 10000);
        sprintf(mExtTrkLog, "malloc( %ld )=%p~%p\n\r",
                Size,
                ret, __builtin_return_address(0));
        UniversalAsynchronousReceiverTransmitter::UART mTrkUART = UniversalAsynchronousReceiverTransmitter::UART(UniversalAsynchronousReceiverTransmitter::COM3);
        for (short i = 0; i < MEM_TRK_MAX_SIZE; i++)
        {
            if (mExtTrkLog[i] == '\r')
                break;
            mTrkUART.Write(mExtTrkLog[i]);
        }
        mExtTrkLock.Unlock();
    }
#endif
    return ret;
}

void *calloc(size_t n, size_t Size)
{
#ifdef DEBUG_ALLOCATIONS_SL
    SmartLockClass lock___COUNTER__(AllocatorLock, (KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown"));
#endif
    memdbg("calloc(%d, %d)->[%s]", n, Size, KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown");

    void *ret = nullptr;
    switch (AllocatorType)
    {
    case MemoryAllocatorType::Pages:
    {
        ret = KernelAllocator.RequestPages(TO_PAGES(n * Size + 1));
        memset(ret, 0, n * Size);
        break;
    }
    case MemoryAllocatorType::XallocV1:
    {
        ret = XallocV1Allocator->calloc(n, Size);
        break;
    }
    case MemoryAllocatorType::liballoc11:
    {
        void *ret = PREFIX(calloc)(n, Size);
        memset(ret, 0, Size);
        return ret;
    }
    default:
        throw;
    }
#ifdef DEBUG
    if (EnableExternalMemoryTracer)
    {
        char LockTmpStr[64];
        strcpy_unsafe(LockTmpStr, __FUNCTION__);
        strcat_unsafe(LockTmpStr, "_memTrk");
        mExtTrkLock.TimeoutLock(LockTmpStr, 10000);
        sprintf(mExtTrkLog, "calloc( %ld %ld )=%p~%p\n\r",
                n, Size,
                ret, __builtin_return_address(0));
        UniversalAsynchronousReceiverTransmitter::UART mTrkUART = UniversalAsynchronousReceiverTransmitter::UART(UniversalAsynchronousReceiverTransmitter::COM3);
        for (short i = 0; i < MEM_TRK_MAX_SIZE; i++)
        {
            if (mExtTrkLog[i] == '\r')
                break;
            mTrkUART.Write(mExtTrkLog[i]);
        }
        mExtTrkLock.Unlock();
    }
#endif
    return ret;
}

void *realloc(void *Address, size_t Size)
{
#ifdef DEBUG_ALLOCATIONS_SL
    SmartLockClass lock___COUNTER__(AllocatorLock, (KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown"));
#endif
    memdbg("realloc(%#lx, %d)->[%s]", Address, Size, KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown");

    void *ret = nullptr;
    switch (AllocatorType)
    {
    case unlikely(MemoryAllocatorType::Pages):
    {
        ret = KernelAllocator.RequestPages(TO_PAGES(Size + 1)); // WARNING: Potential memory leak
        memset(ret, 0, Size);
        break;
    }
    case MemoryAllocatorType::XallocV1:
    {
        ret = XallocV1Allocator->realloc(Address, Size);
        break;
    }
    case MemoryAllocatorType::liballoc11:
    {
        void *ret = PREFIX(realloc)(Address, Size);
        memset(ret, 0, Size);
        return ret;
    }
    default:
        throw;
    }
#ifdef DEBUG
    if (EnableExternalMemoryTracer)
    {
        char LockTmpStr[64];
        strcpy_unsafe(LockTmpStr, __FUNCTION__);
        strcat_unsafe(LockTmpStr, "_memTrk");
        mExtTrkLock.TimeoutLock(LockTmpStr, 10000);
        sprintf(mExtTrkLog, "realloc( %p %ld )=%p~%p\n\r",
                Address, Size,
                ret, __builtin_return_address(0));
        UniversalAsynchronousReceiverTransmitter::UART mTrkUART = UniversalAsynchronousReceiverTransmitter::UART(UniversalAsynchronousReceiverTransmitter::COM3);
        for (short i = 0; i < MEM_TRK_MAX_SIZE; i++)
        {
            if (mExtTrkLog[i] == '\r')
                break;
            mTrkUART.Write(mExtTrkLog[i]);
        }
        mExtTrkLock.Unlock();
    }
#endif
    return ret;
}

void free(void *Address)
{
#ifdef DEBUG_ALLOCATIONS_SL
    SmartLockClass lock___COUNTER__(AllocatorLock, (KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown"));
#endif
    memdbg("free(%#lx)->[%s]", Address, KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown");

    switch (AllocatorType)
    {
    case unlikely(MemoryAllocatorType::Pages):
    {
        KernelAllocator.FreePage(Address); // WARNING: Potential memory leak
        break;
    }
    case MemoryAllocatorType::XallocV1:
    {
        XallocV1Allocator->free(Address);
        break;
    }
    case MemoryAllocatorType::liballoc11:
    {
        PREFIX(free)
        (Address);
        break;
    }
    default:
        throw;
    }
#ifdef DEBUG
    if (EnableExternalMemoryTracer)
    {
        char LockTmpStr[64];
        strcpy_unsafe(LockTmpStr, __FUNCTION__);
        strcat_unsafe(LockTmpStr, "_memTrk");
        mExtTrkLock.TimeoutLock(LockTmpStr, 10000);
        sprintf(mExtTrkLog, "free( %p )~%p\n\r",
                Address,
                __builtin_return_address(0));
        UniversalAsynchronousReceiverTransmitter::UART mTrkUART = UniversalAsynchronousReceiverTransmitter::UART(UniversalAsynchronousReceiverTransmitter::COM3);
        for (short i = 0; i < MEM_TRK_MAX_SIZE; i++)
        {
            if (mExtTrkLog[i] == '\r')
                break;
            mTrkUART.Write(mExtTrkLog[i]);
        }
        mExtTrkLock.Unlock();
    }
#endif
}

void *operator new(size_t Size)
{
#ifdef DEBUG_ALLOCATIONS_SL
    SmartLockClass lock___COUNTER__(OperatorAllocatorLock, (KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown"));
#endif
    memdbg("new(%d)->[%s]", Size, KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown");

    void *ret = malloc(Size);
#ifdef DEBUG
    if (EnableExternalMemoryTracer)
    {
        char LockTmpStr[64];
        strcpy_unsafe(LockTmpStr, __FUNCTION__);
        strcat_unsafe(LockTmpStr, "_memTrk");
        mExtTrkLock.TimeoutLock(LockTmpStr, 10000);
        sprintf(mExtTrkLog, "new( %ld )=%p~%p\n\r",
                Size,
                ret, __builtin_return_address(0));
        UniversalAsynchronousReceiverTransmitter::UART mTrkUART = UniversalAsynchronousReceiverTransmitter::UART(UniversalAsynchronousReceiverTransmitter::COM3);
        for (short i = 0; i < MEM_TRK_MAX_SIZE; i++)
        {
            if (mExtTrkLog[i] == '\r')
                break;
            mTrkUART.Write(mExtTrkLog[i]);
        }
        mExtTrkLock.Unlock();
    }
#endif
    return ret;
}

void *operator new[](size_t Size)
{
#ifdef DEBUG_ALLOCATIONS_SL
    SmartLockClass lock___COUNTER__(OperatorAllocatorLock, (KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown"));
#endif
    memdbg("new[](%d)->[%s]", Size, KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown");

    void *ret = malloc(Size);
#ifdef DEBUG
    if (EnableExternalMemoryTracer)
    {
        char LockTmpStr[64];
        strcpy_unsafe(LockTmpStr, __FUNCTION__);
        strcat_unsafe(LockTmpStr, "_memTrk");
        mExtTrkLock.TimeoutLock(LockTmpStr, 10000);
        sprintf(mExtTrkLog, "new[]( %ld )=%p~%p\n\r",
                Size,
                ret, __builtin_return_address(0));
        UniversalAsynchronousReceiverTransmitter::UART mTrkUART = UniversalAsynchronousReceiverTransmitter::UART(UniversalAsynchronousReceiverTransmitter::COM3);
        for (short i = 0; i < MEM_TRK_MAX_SIZE; i++)
        {
            if (mExtTrkLog[i] == '\r')
                break;
            mTrkUART.Write(mExtTrkLog[i]);
        }
        mExtTrkLock.Unlock();
    }
#endif
    return ret;
}

void *operator new(unsigned long Size, std::align_val_t Alignment)
{
#ifdef DEBUG_ALLOCATIONS_SL
    SmartLockClass lock___COUNTER__(OperatorAllocatorLock, (KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown"));
#endif
    memdbg("new(%d, %d)->[%s]", Size, Alignment, KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown");
    fixme("operator new with alignment(%#lx) is not implemented", Alignment);

    void *ret = malloc(Size);
#ifdef DEBUG
    if (EnableExternalMemoryTracer)
    {
        char LockTmpStr[64];
        strcpy_unsafe(LockTmpStr, __FUNCTION__);
        strcat_unsafe(LockTmpStr, "_memTrk");
        mExtTrkLock.TimeoutLock(LockTmpStr, 10000);
        sprintf(mExtTrkLog, "new( %ld %#lx )=%p~%p\n\r",
                Size, (uintptr_t)Alignment,
                ret, __builtin_return_address(0));
        UniversalAsynchronousReceiverTransmitter::UART mTrkUART = UniversalAsynchronousReceiverTransmitter::UART(UniversalAsynchronousReceiverTransmitter::COM3);
        for (short i = 0; i < MEM_TRK_MAX_SIZE; i++)
        {
            if (mExtTrkLog[i] == '\r')
                break;
            mTrkUART.Write(mExtTrkLog[i]);
        }
        mExtTrkLock.Unlock();
    }
#endif
    return ret;
}

void operator delete(void *Pointer)
{
#ifdef DEBUG_ALLOCATIONS_SL
    SmartLockClass lock___COUNTER__(OperatorAllocatorLock, (KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown"));
#endif
    memdbg("delete(%#lx)->[%s]", Pointer, KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown");

    free(Pointer);
#ifdef DEBUG
    if (EnableExternalMemoryTracer)
    {
        char LockTmpStr[64];
        strcpy_unsafe(LockTmpStr, __FUNCTION__);
        strcat_unsafe(LockTmpStr, "_memTrk");
        mExtTrkLock.TimeoutLock(LockTmpStr, 10000);
        sprintf(mExtTrkLog, "delete( %p )~%p\n\r",
                Pointer,
                __builtin_return_address(0));
        UniversalAsynchronousReceiverTransmitter::UART mTrkUART = UniversalAsynchronousReceiverTransmitter::UART(UniversalAsynchronousReceiverTransmitter::COM3);
        for (short i = 0; i < MEM_TRK_MAX_SIZE; i++)
        {
            if (mExtTrkLog[i] == '\r')
                break;
            mTrkUART.Write(mExtTrkLog[i]);
        }
        mExtTrkLock.Unlock();
    }
#endif
}

void operator delete[](void *Pointer)
{
#ifdef DEBUG_ALLOCATIONS_SL
    SmartLockClass lock___COUNTER__(OperatorAllocatorLock, (KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown"));
#endif
    memdbg("delete[](%#lx)->[%s]", Pointer, KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown");

    free(Pointer);
#ifdef DEBUG
    if (EnableExternalMemoryTracer)
    {
        char LockTmpStr[64];
        strcpy_unsafe(LockTmpStr, __FUNCTION__);
        strcat_unsafe(LockTmpStr, "_memTrk");
        mExtTrkLock.TimeoutLock(LockTmpStr, 10000);
        sprintf(mExtTrkLog, "delete[]( %p )~%p\n\r",
                Pointer,
                __builtin_return_address(0));
        UniversalAsynchronousReceiverTransmitter::UART mTrkUART = UniversalAsynchronousReceiverTransmitter::UART(UniversalAsynchronousReceiverTransmitter::COM3);
        for (short i = 0; i < MEM_TRK_MAX_SIZE; i++)
        {
            if (mExtTrkLog[i] == '\r')
                break;
            mTrkUART.Write(mExtTrkLog[i]);
        }
        mExtTrkLock.Unlock();
    }
#endif
}

void operator delete(void *Pointer, long unsigned int Size)
{
    UNUSED(Size);
#ifdef DEBUG_ALLOCATIONS_SL
    SmartLockClass lock___COUNTER__(OperatorAllocatorLock, (KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown"));
#endif
    memdbg("delete(%#lx, %d)->[%s]", Pointer, Size, KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown");

    free(Pointer);
#ifdef DEBUG
    if (EnableExternalMemoryTracer)
    {
        char LockTmpStr[64];
        strcpy_unsafe(LockTmpStr, __FUNCTION__);
        strcat_unsafe(LockTmpStr, "_memTrk");
        mExtTrkLock.TimeoutLock(LockTmpStr, 10000);
        sprintf(mExtTrkLog, "delete( %p %ld )~%p\n\r",
                Pointer, Size,
                __builtin_return_address(0));
        UniversalAsynchronousReceiverTransmitter::UART mTrkUART = UniversalAsynchronousReceiverTransmitter::UART(UniversalAsynchronousReceiverTransmitter::COM3);
        for (short i = 0; i < MEM_TRK_MAX_SIZE; i++)
        {
            if (mExtTrkLog[i] == '\r')
                break;
            mTrkUART.Write(mExtTrkLog[i]);
        }
        mExtTrkLock.Unlock();
    }
#endif
}

void operator delete[](void *Pointer, long unsigned int Size)
{
    UNUSED(Size);
#ifdef DEBUG_ALLOCATIONS_SL
    SmartLockClass lock___COUNTER__(OperatorAllocatorLock, (KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown"));
#endif
    memdbg("delete[](%#lx, %d)->[%s]", Pointer, Size, KernelSymbolTable ? KernelSymbolTable->GetSymbolFromAddress((uintptr_t)__builtin_return_address(0)) : "Unknown");

    free(Pointer);
#ifdef DEBUG
    if (EnableExternalMemoryTracer)
    {
        char LockTmpStr[64];
        strcpy_unsafe(LockTmpStr, __FUNCTION__);
        strcat_unsafe(LockTmpStr, "_memTrk");
        mExtTrkLock.TimeoutLock(LockTmpStr, 10000);
        sprintf(mExtTrkLog, "delete[]( %p %ld )~%p\n\r",
                Pointer, Size,
                __builtin_return_address(0));
        UniversalAsynchronousReceiverTransmitter::UART mTrkUART = UniversalAsynchronousReceiverTransmitter::UART(UniversalAsynchronousReceiverTransmitter::COM3);
        for (short i = 0; i < MEM_TRK_MAX_SIZE; i++)
        {
            if (mExtTrkLog[i] == '\r')
                break;
            mTrkUART.Write(mExtTrkLog[i]);
        }
        mExtTrkLock.Unlock();
    }
#endif
}
