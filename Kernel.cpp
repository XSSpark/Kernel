#include "kernel.h"

#include <boot/protocols/multiboot2.h>
#include <interrupts.hpp>
#include <memory.hpp>
#include <convert.h>
#include <printf.h>
#include <lock.hpp>
#include <debug.h>
#include <smp.hpp>
#include <cargs.h>
#include <io.h>

NewLock(KernelLock);

BootInfo *bInfo = nullptr;
Video::Display *Display = nullptr;
SymbolResolver::Symbols *KernelSymbolTable = nullptr;
Power::Power *PowerManager = nullptr;
PCI::PCI *PCIManager = nullptr;
Tasking::Task *TaskManager = nullptr;
Time::time *TimeManager = nullptr;
FileSystem::Virtual *vfs = nullptr;

KernelConfig Config;
Time::Clock BootClock;

// For the Display class. Printing on first buffer as default.
extern "C" void putchar(char c) { Display->Print(c, 0); }

EXTERNC void KPrint(const char *Format, ...)
{
    SmartLock(KernelLock);
    Time::Clock tm = Time::ReadClock();
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
    trace("Hello, World!");
    InitializeMemoryManagement(Info);
    BootClock = Time::ReadClock();
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
    KPrint("Initializing GDT and IDT");
    Interrupts::Initialize(0);
    KPrint("Initializing CPU Features");
    CPU::InitializeFeatures();
    KPrint("Loading Kernel Symbols");
    KernelSymbolTable = new SymbolResolver::Symbols((uint64_t)Info->Kernel.FileBase);
    KPrint("Reading Kernel Parameters");
    Config = ParseConfig((char *)bInfo->Kernel.CommandLine);
    KPrint("Initializing Power Manager");
    PowerManager = new Power::Power;
    KPrint("Initializing PCI Manager");
    PCIManager = new PCI::PCI;
    foreach (auto Device in PCIManager->GetDevices())
    {
        KPrint("PCI: \e8888FF%s \eCCCCCC/ \e8888FF%s \eCCCCCC/ \e8888FF%s \eCCCCCC/ \e8888FF%s \eCCCCCC/ \e8888FF%s",
               PCI::Descriptors::GetVendorName(Device->VendorID),
               PCI::Descriptors::GetDeviceName(Device->VendorID, Device->DeviceID),
               PCI::Descriptors::DeviceClasses[Device->Class],
               PCI::Descriptors::GetSubclassName(Device->Class, Device->Subclass),
               PCI::Descriptors::GetProgIFName(Device->Class, Device->Subclass, Device->ProgIF));
    }
    KPrint("Enabling Interrupts on Bootstrap Processor");
    Interrupts::Enable(0);
    KPrint("Initializing Timers");
#if defined(__amd64__)
    TimeManager = new Time::time(PowerManager->GetACPI());
#elif defined(__i386__)
    TimeManager = new Time::time(PowerManager->GetACPI());
#elif defined(__aarch64__)
    TimeManager = new Time::time(nullptr);
#endif
    KPrint("Initializing Bootstrap Processor Timer");
    Interrupts::InitializeTimer(0);
    KPrint("Initializing SMP");
    SMP::Initialize(PowerManager->GetMADT());
    TaskManager = new Tasking::Task((Tasking::IP)KernelMainThread);
    KPrint("\e058C19######## \eE85230END \e058C19########");
    CPU::Halt(true);
}
