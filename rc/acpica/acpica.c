/*
Copyright (c) 2011, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <grub/dl.h>
#include <grub/err.h>
#include <grub/misc.h>

#include "datatype.h"
#include "bitsutil.h"

#include "acpi.h"
#include "accommon.h"
#include "acnamesp.h"
#include "amlresrc.h"

#include "acpica.h"
#include "acpica2.h"

GRUB_MOD_LICENSE("GPLv3+");
GRUB_MOD_DUAL_LICENSE("3-clause BSD");

ACPI_MODULE_NAME("grub2-acpica")

static U32 acpica_early_init_state = 0;
static U32 acpica_init_state = 0;
bool acpica_cpus_initialized = false;
U32 acpica_cpus_init_caps = 0;

bool IsEnabledProcessor(ACPI_HANDLE ObjHandle)
{
    bool ret = false;
    ACPI_DEVICE_INFO *Info;

    if (ACPI_SUCCESS(AcpiGetObjectInfo(ObjHandle, &Info)))
        if ((Info->Type == ACPI_TYPE_PROCESSOR) && (Info->Valid & ACPI_VALID_STA) && (Info->CurrentStatus & ACPI_STA_DEVICE_ENABLED))
            ret = true;
    ACPI_FREE(Info);

    return ret;
}

bool IsEnabledProcessorDev(ACPI_HANDLE ObjHandle)
{
    bool ret = false;
    ACPI_DEVICE_INFO *Info;

    if (ACPI_SUCCESS(AcpiGetObjectInfo(ObjHandle, &Info)))
        if ((Info->Type == ACPI_TYPE_DEVICE) && (Info->Valid & ACPI_VALID_STA) && (Info->CurrentStatus & ACPI_STA_DEVICE_ENABLED) &&
            (Info->Valid & ACPI_VALID_HID) && (grub_strncmp(Info->HardwareId.String, "ACPI0007", Info->HardwareId.Length) == 0))
            ret = true;
    ACPI_FREE(Info);

    return ret;
}

grub_err_t acpica_early_init(void)
{
    if (!acpica_early_init_state) {
        if (AcpiInitializeTables(NULL, 0, 0) != AE_OK)
            return GRUB_ERR_IO;

        acpica_early_init_state = 1;
    }

    return GRUB_ERR_NONE;
}

grub_err_t acpica_init(void)
{
    grub_err_t err;
    err = acpica_early_init();
    if (err != GRUB_ERR_NONE)
        return err;

    if (acpica_init_state == 1)
        return GRUB_ERR_NONE;

    if (AcpiInitializeSubsystem() != AE_OK)
        return GRUB_ERR_IO;

    if (AcpiLoadTables() != AE_OK)
        return GRUB_ERR_IO;

    if (AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION) != AE_OK)
        return GRUB_ERR_IO;

    if (AcpiInitializeObjects(ACPI_FULL_INITIALIZATION) != AE_OK)
        return GRUB_ERR_IO;

    acpica_init_state = 1;

    return GRUB_ERR_NONE;
}

void acpica_terminate(void)
{
    AcpiTerminate();
    acpica_init_state = 0;
    acpica_cpus_initialized = false;
}

GRUB_MOD_INIT(acpica)
{
    // Bit field that enables/disables debug output from entire subcomponents within the ACPICA subsystem.
    // AcpiDbgLevel = 0;

    // Bit field that enables/disables the various debug output levels
    // AcpiDbgLayer = 0;

    dprintf("acpica", "ACPI_CA_VERSION = %x\n", ACPI_CA_VERSION);
}

GRUB_MOD_FINI(acpica)
{
    AcpiTerminate();
}
