#include "../api.hpp"

#include <interrupts.hpp>
#include <memory.hpp>
#include <task.hpp>
#include <lock.hpp>
#include <printf.h>
#include <cwalk.h>
#include <md5.h>

#include "../../../kernel.h"
#include "../../../DAPI.hpp"
#include "../../../Fex.hpp"

namespace Driver
{
    DriverCode Driver::DriverLoadBindInterrupt(void *DrvExtHdr, uint64_t DriverAddress, uint64_t Size, bool IsElf)
    {
        Memory::Tracker *Tracker = new Memory::Tracker;
        Fex *fex = (Fex *)Tracker->RequestPages(TO_PAGES(Size));
        memcpy(fex, (void *)DriverAddress, Size);
        FexExtended *fexExtended = (FexExtended *)((uint64_t)fex + EXTENDED_SECTION_ADDRESS);
        debug("Driver allocated at %#lx-%#lx", fex, (uint64_t)fex + Size);
#ifdef DEBUG
        uint8_t *result = md5File((uint8_t *)fex, Size);
        debug("MD5: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
              result[0], result[1], result[2], result[3], result[4], result[5], result[6], result[7],
              result[8], result[9], result[10], result[11], result[12], result[13], result[14], result[15]);
        kfree(result);
#endif
        KernelAPI *KAPI = (KernelAPI *)Tracker->RequestPages(TO_PAGES(sizeof(KernelAPI)));

        if (CallDriverEntryPoint(fex, KAPI) != DriverCode::OK)
        {
            delete Tracker;
            return DriverCode::DRIVER_RETURNED_ERROR;
        }
        debug("Starting driver %s (offset: %#lx)", fexExtended->Driver.Name, fex);

        KernelCallback *KCallback = (KernelCallback *)Tracker->RequestPages(TO_PAGES(sizeof(KernelCallback)));

        switch (fexExtended->Driver.Type)
        {
        case FexDriverType::FexDriverType_Generic:
        {
            fixme("Generic driver: %s", fexExtended->Driver.Name);
            DriverFile *DrvFile = new DriverFile;
            DrvFile->DriverUID = this->DriverUIDs - 1;
            DrvFile->Address = (void *)fex;
            DrvFile->MemTrk = Tracker;
            Drivers.push_back(DrvFile);
            break;
        }
        case FexDriverType::FexDriverType_Display:
        {
            fixme("Display driver: %s", fexExtended->Driver.Name);
            delete Tracker;
            break;
        }
        case FexDriverType::FexDriverType_Network:
        {
            fixme("Network driver: %s", fexExtended->Driver.Name);
            delete Tracker;
            break;
        }
        case FexDriverType::FexDriverType_Storage:
        {
            for (unsigned long i = 0; i < sizeof(((FexExtended *)DrvExtHdr)->Driver.Bind.Interrupt.Vector) / sizeof(((FexExtended *)DrvExtHdr)->Driver.Bind.Interrupt.Vector[0]); i++)
            {
                if (((FexExtended *)DrvExtHdr)->Driver.Bind.Interrupt.Vector[i] == 0)
                    break;

                fixme("TODO: MULTIPLE BIND INTERRUPT VECTORS %d", ((FexExtended *)DrvExtHdr)->Driver.Bind.Interrupt.Vector[i]);
            }

            fixme("Not implemented");
            delete Tracker;
            break;

            KCallback->RawPtr = nullptr;
            KCallback->Reason = CallbackReason::ConfigurationReason;
            int CallbackRet = ((int (*)(KernelCallback *))((uint64_t)fexExtended->Driver.Callback + (uint64_t)fex))(KCallback);
            if (CallbackRet == DriverReturnCode::NOT_IMPLEMENTED)
            {
                error("Driver %s does not implement the configuration callback", fexExtended->Driver.Name);
                delete Tracker;
                break;
            }
            else if (CallbackRet != DriverReturnCode::OK)
            {
                error("Driver %s returned error %d", fexExtended->Driver.Name, CallbackRet);
                delete Tracker;
                break;
            }

            DriverFile *DrvFile = new DriverFile;
            DrvFile->DriverUID = this->DriverUIDs - 1;
            DrvFile->Address = (void *)fex;
            DrvFile->MemTrk = Tracker;
            Drivers.push_back(DrvFile);
            break;
        }
        case FexDriverType::FexDriverType_FileSystem:
        {
            fixme("Filesystem driver: %s", fexExtended->Driver.Name);
            delete Tracker;
            break;
        }
        case FexDriverType::FexDriverType_Input:
        {
            DriverInterruptHook *InterruptHook = nullptr;
            if (((FexExtended *)DrvExtHdr)->Driver.Bind.Interrupt.Vector[0] != 0)
                InterruptHook = new DriverInterruptHook(((FexExtended *)DrvExtHdr)->Driver.Bind.Interrupt.Vector[0] + 32, // x86
                                                        (void *)((uint64_t)fexExtended->Driver.Callback + (uint64_t)fex),
                                                        KCallback);

            for (unsigned long i = 0; i < sizeof(((FexExtended *)DrvExtHdr)->Driver.Bind.Interrupt.Vector) / sizeof(((FexExtended *)DrvExtHdr)->Driver.Bind.Interrupt.Vector[0]); i++)
            {
                if (((FexExtended *)DrvExtHdr)->Driver.Bind.Interrupt.Vector[i] == 0)
                    break;
                // InterruptHook = new DriverInterruptHook(((FexExtended *)DrvExtHdr)->Driver.Bind.Interrupt.Vector[i] + 32, // x86
                //                                         (void *)((uint64_t)fexExtended->Driver.Callback + (uint64_t)fex),
                //                                         KCallback);
                fixme("TODO: MULTIPLE BIND INTERRUPT VECTORS %d", ((FexExtended *)DrvExtHdr)->Driver.Bind.Interrupt.Vector[i]);
            }

            KCallback->RawPtr = nullptr;
            KCallback->Reason = CallbackReason::ConfigurationReason;
            int CallbackRet = ((int (*)(KernelCallback *))((uint64_t)fexExtended->Driver.Callback + (uint64_t)fex))(KCallback);
            if (CallbackRet == DriverReturnCode::NOT_IMPLEMENTED)
            {
                error("Driver %s does not implement the configuration callback", fexExtended->Driver.Name);
                delete InterruptHook;
                delete Tracker;
                break;
            }
            else if (CallbackRet != DriverReturnCode::OK)
            {
                error("Driver %s returned error %d", fexExtended->Driver.Name, CallbackRet);
                delete InterruptHook;
                delete Tracker;
                break;
            }

            memset(KCallback, 0, sizeof(KernelCallback));
            KCallback->Reason = CallbackReason::InterruptReason;

            DriverFile *DrvFile = new DriverFile;
            DrvFile->DriverUID = this->DriverUIDs - 1;
            DrvFile->Address = (void *)fex;
            DrvFile->MemTrk = Tracker;
            DrvFile->InterruptHook[0] = InterruptHook;
            Drivers.push_back(DrvFile);
            break;
        }
        case FexDriverType::FexDriverType_Audio:
        {
            fixme("Audio driver: %s", fexExtended->Driver.Name);
            delete Tracker;
            break;
        }
        default:
        {
            warn("Unknown driver type: %d", fexExtended->Driver.Type);
            delete Tracker;
            break;
        }
        }

        return DriverCode::OK;
    }
}
