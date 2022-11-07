#include <exec.hpp>

#include <memory.hpp>
#include <lock.hpp>
#include <msexec.h>
#include <cwalk.h>
#include <elf.h>

#include "../kernel.h"
#include "../Fex.hpp"

using namespace Tasking;

namespace Execute
{
    SpawnData Spawn(char *Path, uint64_t Arg0, uint64_t Arg1)
    {
        SpawnData ret = {.Status = ExStatus::Unknown,
                         .Process = nullptr,
                         .Thread = nullptr};
        FileSystem::FILE *ExFile = vfs->Open(Path);
        if (ExFile->Status == FileSystem::FileStatus::OK)
        {
            if (ExFile->Node->Flags == FileSystem::NodeFlags::FS_FILE)
            {
                BinaryType Type = GetBinaryType(Path);
                switch (Type)
                {
                case BinaryType::BinTypeFex:
                {
                    Fex *FexHdr = (Fex *)ExFile->Node->Address;
                    if (FexHdr->Type == FexFormatType::FexFormatType_Executable)
                    {
                        const char *BaseName;
                        cwk_path_get_basename(Path, &BaseName, nullptr);
                        PCB *Process = TaskManager->CreateProcess(TaskManager->GetCurrentProcess(), BaseName, TaskTrustLevel::User);

                        void *BaseImage = KernelAllocator.RequestPages(TO_PAGES(ExFile->Node->Length));
                        memcpy(BaseImage, (void *)ExFile->Node->Address, ExFile->Node->Length);

                        Memory::Virtual pva = Memory::Virtual(Process->PageTable);
                        for (uint64_t i = 0; i < TO_PAGES(ExFile->Node->Length); i++)
                            pva.Map((void *)((uint64_t)BaseImage + (i * PAGE_SIZE)), (void *)((uint64_t)BaseImage + (i * PAGE_SIZE)), Memory::PTFlag::RW | Memory::PTFlag::US);

                        TCB *Thread = TaskManager->CreateThread(Process,
                                                                (IP)FexHdr->Pointer,
                                                                Arg0, Arg1,
                                                                (IPOffset)BaseImage,
                                                                TaskArchitecture::x64,
                                                                TaskCompatibility::Native);
                        ret.Process = Process;
                        ret.Thread = Thread;
                        ret.Status = ExStatus::OK;
                        goto Exit;
                    }
                    ret.Status = ExStatus::InvalidFileHeader;
                    goto Exit;
                }
                case BinaryType::BinTypeELF:
                {
                    const char *BaseName;
                    cwk_path_get_basename(Path, &BaseName, nullptr);
                    PCB *Process = TaskManager->CreateProcess(TaskManager->GetCurrentProcess(), BaseName, TaskTrustLevel::User);

                    void *BaseImage = KernelAllocator.RequestPages(TO_PAGES(ExFile->Node->Length));
                    memcpy(BaseImage, (void *)ExFile->Node->Address, ExFile->Node->Length);

                    Memory::Virtual pva = Memory::Virtual(Process->PageTable);
                    for (uint64_t i = 0; i < TO_PAGES(ExFile->Node->Length); i++)
                        pva.Map((void *)((uint64_t)BaseImage + (i * PAGE_SIZE)), (void *)((uint64_t)BaseImage + (i * PAGE_SIZE)), Memory::PTFlag::RW | Memory::PTFlag::US);

                    Elf64_Ehdr *ELFHeader = (Elf64_Ehdr *)BaseImage;

                    TaskArchitecture Arch = TaskArchitecture::x64;
                    TaskCompatibility Comp = TaskCompatibility::Native;
                    if (ELFHeader->e_machine == EM_386)
                        Arch = TaskArchitecture::x32;
                    else if (ELFHeader->e_machine == EM_AMD64)
                        Arch = TaskArchitecture::x64;
                    else if (ELFHeader->e_machine == EM_AARCH64)
                        Arch = TaskArchitecture::ARM64;
                    else
                        Arch = TaskArchitecture::UnknownArchitecture;

                    // TODO: Should I care about this?
                    if (ELFHeader->e_ident[EI_CLASS] == ELFCLASS32)
                    {
                        if (ELFHeader->e_ident[EI_DATA] == ELFDATA2LSB)
                            fixme("ELF32 LSB");
                        else if (ELFHeader->e_ident[EI_DATA] == ELFDATA2MSB)
                            fixme("ELF32 MSB");
                        else
                            fixme("ELF32 Unknown");
                    }
                    else if (ELFHeader->e_ident[EI_CLASS] == ELFCLASS64)
                    {
                        if (ELFHeader->e_ident[EI_DATA] == ELFDATA2LSB)
                            fixme("ELF64 LSB");
                        else if (ELFHeader->e_ident[EI_DATA] == ELFDATA2MSB)
                            fixme("ELF64 MSB");
                        else
                            fixme("ELF64 Unknown");
                    }
                    else
                        fixme("Unknown ELF");

                    if (ELFHeader->e_type == ET_EXEC)
                    {
                        trace("Executable");
                        Elf64_Phdr *pheader = (Elf64_Phdr *)(((char *)BaseImage) + ELFHeader->e_phoff);
                        void *Address = nullptr;
                        for (int i = 0; i < ELFHeader->e_phnum; i++, pheader++)
                        {
                            if (pheader->p_type != PT_LOAD)
                                continue;
                            Address = (void *)((uint64_t)pheader->p_vaddr + pheader->p_memsz);
                        }
                        void *Offset = KernelAllocator.RequestPages(TO_PAGES((uint64_t)Address));

                        for (uint64_t i = 0; i < TO_PAGES((uint64_t)Address); i++)
                            pva.Map((void *)((uint64_t)Offset + (i * PAGE_SIZE)), (void *)((uint64_t)Offset + (i * PAGE_SIZE)), Memory::PTFlag::RW | Memory::PTFlag::US);

                        pheader = (Elf64_Phdr *)(((char *)BaseImage) + ELFHeader->e_phoff);
                        for (int i = 0; i < ELFHeader->e_phnum; i++, pheader++)
                        {
                            if (pheader->p_type != PT_LOAD)
                                continue;
                            void *dst = (void *)((uint64_t)pheader->p_vaddr + (uint64_t)Offset);
                            memset(dst, 0, pheader->p_memsz);
                            memcpy(dst, ((char *)BaseImage) + pheader->p_offset, pheader->p_filesz);
                        }

                        TCB *Thread = TaskManager->CreateThread(Process,
                                                                (IP)ELFHeader->e_entry,
                                                                Arg0, Arg1,
                                                                (IPOffset)Offset,
                                                                Arch,
                                                                Comp);
                        ret.Process = Process;
                        ret.Thread = Thread;
                        ret.Status = ExStatus::OK;
                        goto Exit;
                    }
                    else if (ELFHeader->e_type == ET_DYN)
                    {
                        fixme("Shared Object");
                    }
                    else if (ELFHeader->e_type == ET_REL)
                    {
                        trace("Relocatable");
                        void *EP = ELFLoadRel(ELFHeader);
                        if (EP == (void *)0xdeadbeef || EP == 0x0)
                        {
                            ret.Status = ExStatus::InvalidFileEntryPoint;
                            goto Exit;
                        }
                        TCB *Thread = TaskManager->CreateThread(Process,
                                                                (IP)EP,
                                                                Arg0, Arg1,
                                                                (IPOffset)BaseImage,
                                                                Arch,
                                                                Comp);
                        ret.Process = Process;
                        ret.Thread = Thread;
                        ret.Status = ExStatus::OK;
                        goto Exit;
                    }
                    else if (ELFHeader->e_type == ET_CORE)
                    {
                        fixme("Core");
                    }
                    else
                    {
                        fixme("Unknown");
                    }

                    ret.Status = ExStatus::InvalidFileHeader;
                    goto Exit;
                }
                default:
                    ret.Status = ExStatus::Unsupported;
                    goto Exit;
                }
                goto Exit;
            }
        }
        else if (ExFile->Status == FileSystem::FileStatus::NOT_FOUND)
        {
            ret.Status = ExStatus::InvalidFilePath;
            goto Exit;
        }
        else
        {
            ret.Status = ExStatus::InvalidFile;
            goto Exit;
        }

    Exit:
        if (ret.Status != ExStatus::OK)
            if (ret.Process)
                ret.Process->Status = TaskStatus::Terminated;
        vfs->Close(ExFile);
        return ret;
    }
}
