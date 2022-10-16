#include "kernel.h"

#include <interrupts.hpp>
#include <memory.hpp>
#include <string.h>
#include <printf.h>
#include <lock.hpp>
#include <time.hpp>
#include <debug.h>
#include <smp.hpp>
#include <io.h>

NEWLOCK(KernelLock);

BootInfo *bInfo = nullptr;
Video::Display *Display = nullptr;
SymbolResolver::Symbols *KernelSymbolTable = nullptr;
Power::Power *PowerManager = nullptr;
PCI::PCI *PCIManager = nullptr;

Time BootClock;

// For the Display class. Printing on first buffer as default.
extern "C" void putchar(char c) { Display->Print(c, 0); }

EXTERNC void KPrint(const char *Format, ...)
{
    SMARTLOCK(KernelLock);
    Time tm = ReadClock();
    printf_("\eCCCCCC[\e00AEFF%02ld:%02ld:%02ld\eCCCCCC] ", tm.Hour, tm.Minute, tm.Second);
    va_list args;
    va_start(args, Format);
    vprintf_(Format, args);
    va_end(args);
    putchar('\n');
    Display->SetBuffer(0);
}

EXTERNC void Entry(BootInfo *Info)
{
    InitializeMemoryManagement(Info);
    trace("Hello, World!");
    BootClock = ReadClock();
    bInfo = (BootInfo *)KernelAllocator.RequestPages(TO_PAGES(sizeof(BootInfo)));
    memcpy(bInfo, Info, sizeof(BootInfo));
    debug("BootInfo structure is at %p", bInfo);
    Display = new Video::Display(bInfo->Framebuffer[0]);
    printf_("\eFFFFFF%s - %s [\e058C19%s\eFFFFFF]\n", KERNEL_NAME, KERNEL_VERSION, GIT_COMMIT_SHORT);
    /**************************************************************************************/
    KPrint("Time: \e8888FF%02d:%02d:%02d %02d/%02d/%02d UTC",
           BootClock.Hour, BootClock.Minute, BootClock.Second,
           BootClock.Day, BootClock.Month, BootClock.Year);
    KPrint("CPU: \e8822AA%s \e8888FF%s (\e058C19%s\e8888FF)", CPU::Vendor(), CPU::Name(), CPU::Hypervisor());
    GetCPU(0)->ID = 0;
    GetCPU(0)->IsActive = true;
    GetCPU(0)->Checksum = CPU_DATA_CHECKSUM;
    KPrint("Initializing GDT and IDT");
    Interrupts::Initialize(0);
    KPrint("Initializing CPU features");
    CPU::InitializeFeatures();
    KPrint("Loading kernel symbols");
    KernelSymbolTable = new SymbolResolver::Symbols((uint64_t)Info->Kernel.FileBase);
    KPrint("Initializing Power Manager");
    PowerManager = new Power::Power;
    KPrint("Initializing PCI Manager");
    PCIManager = new PCI::PCI;
    foreach (auto hdr in PCIManager->GetDevices())
    {
        KPrint("Found PCI device: \e8888FF%s \eCCCCCC/ \e8888FF%s \eCCCCCC/ \e8888FF%s \eCCCCCC/ \e8888FF%s \eCCCCCC/ \e8888FF%s",
               PCI::Descriptors::GetVendorName(hdr->VendorID),
               PCI::Descriptors::GetDeviceName(hdr->VendorID, hdr->DeviceID),
               PCI::Descriptors::DeviceClasses[hdr->Class],
               PCI::Descriptors::GetSubclassName(hdr->Class, hdr->Subclass),
               PCI::Descriptors::GetProgIFName(hdr->Class, hdr->Subclass, hdr->ProgIF));
    }
    KPrint("Enabling interrupts");
    Interrupts::Enable();
    KPrint("Initializing SMP");
    SMP::Initialize(PowerManager->GetMADT());
    KPrint("Initializing timer");
    Interrupts::InitializeTimer();
    KPrint("\e058C19######## \eE85230END \e058C19########");
    // CPU::Interrupts(CPU::Enable);
    asmv("int $0x1");
    while (1)
        CPU::Halt();
}

EXTERNC void arm64Entry(uint64_t dtb_ptr32, uint64_t x1, uint64_t x2, uint64_t x3)
{
    trace("Hello, World!");
    while (1)
        CPU::Halt();
}

EXTERNC void x32Entry(uint64_t Data)
{
    trace("Hello, World!");
    while (1)
        CPU::Halt();
}
