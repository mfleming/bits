/*
Copyright (c) 2012, Intel Corporation
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

#include <grub/extcmd.h>
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/normal.h>
#include <grub/file.h>
#include <grub/machine/memory.h>
#include <grub/memory.h>

#include "datatype.h"
#include "acpicode.h"
#include "acpidecode.h"
#include "bitsutil.h"
#include "smp.h"
#include "ppmsetup.h"
#include "ppmstart.h"
#include "placepe.h"
#include "acpica2.h"

GRUB_MOD_LICENSE("GPLv3+");
GRUB_MOD_DUAL_LICENSE("3-clause BSD");

#define stringize_aux(s) #s
#define stringize(s) stringize_aux(s)
#define CURRENT_PPM_RCM_INTERFACE_SPECIFICATION_STR stringize(CURRENT_PPM_RCM_INTERFACE_SPECIFICATION)
#define CURRENT_PPM_RCM_INTERFACE_MINOR_REVISION_STR stringize(CURRENT_PPM_RCM_INTERFACE_MINOR_REVISION)

static unsigned int numSockets = 2;
static short AcpiEnable;
static short DebugEnable;
static short VerboseEnable;
static short PstateEnable;
static short CstateEnable;
static short TstateEnable;
static short TurboEnable;
static short SsdtInsertEnable;
static U32 pmbase;
static U32 ssdt_size;
static U32 ssdt_addr;
static short c6Enable;
static short c7Enable;
static short c3;
static short cpuNamespace;
static U32 RealModeAddr;
static short RealModeEnable;
static short PerfWattOptEnable = 0;

PROCESSOR_NUMBER_TO_NAMESEG cpu_map[CPU_MAP_LIMIT];
unsigned int cpu_map_count;

static void print_nameseg(U32 i)
{
    grub_printf("%c%c%c%c",
                (int)(i & 0x000000ff),
                (int)((i & 0x0000ff00) >> 8),
                (int)((i & 0x00ff0000) >> 16),
                (int)(i >> 24));
}

typedef asmlinkage U32(*rcm_func) (PPM_SETUP_OPTIONS *);

static void dump_exit_state(struct ppm_setup_options *options)
{
    if ((options->exit_state.return_status == 1) &&
        (options->exit_state.error_code_count == 1) &&
        (options->exit_state.error_codes[0] == EXIT_CODE_PPM_COMPLETED)) {
        grub_printf("Processor Power Management reference code completed successfully.\n");
        return;
    }

    grub_printf("exit_state.return_status = %08xh\n", options->exit_state.return_status);

    if ((options->exit_state.return_status == 0) || (options->exit_state.error_code_count != 0)) {
        grub_printf("exit_state.error_code_count = %u\n", options->exit_state.error_code_count);
        {
            const char *error_strings[] = {
                "",
                "EXIT_CODE_PPM_COMPLETED=1",
                "EXIT_CODE_FAILED_BUILD_PSTATES=2",
                "EXIT_CODE_PPM_EIST_DISABLED=3",
                "EXIT_CODE_FAILED_FIND_ACPI_TABLES=4",
                "EXIT_CODE_FAILED_PROCESS_MADT=5",
                "EXIT_CODE_FAILED_PROCESS_MADT_INFO=6",
                "EXIT_CODE_FAILED_PROCESS_SSDT=7",
                "EXIT_CODE_FAILED_HOST_INIT=8",
                "EXIT_CODE_FAILED_COLLECT_SOCKET_INFO=9",
                "EXIT_CODE_FAILED_DETERMINE_CONFIGURATION=0x0a",
                "EXIT_CODE_NO_SSDT_CREATED=0x0b",
                "EXIT_CODE_FAILED_BUILD_CSTATES=0x0c",
                "EXIT_CODE_FAILED_BUILD_TSTATES=0x0d",
                "EXIT_CODE_FAILED_FIND_PKG_INDEX_FROM_LAPIC=0x0e",
                "EXIT_CODE_FAILED_INVALID_SSDT_LOCATION_FLAG=0x0f",
                "EXIT_CODE_FAILED_NO_LAPIC_FOUND_IN_MADT=0x10",
                "EXIT_CODE_FAILED_SSDT_SIZE_EXCEEDED=0x11",
                "EXIT_CODE_FAILED_BUILD_SSDT=0x12",
                "EXIT_CODE_MAX_CORES_EXCEEDED=0x13",
                "EXIT_CODE_FAILED_FIND_CPU_SCOPE_NAME_SEG=0x14",
                "EXIT_CODE_FAILED_UPDATE_FADT=0x15",
                "EXIT_CODE_GPF_DETECTED=0x16",
                "EXIT_CODE_INVALID_SSDT_ADDR=0x17",
                "EXIT_CODE_INVALID_SSDT_LEN=0x18",
                "EXIT_CODE_PCIE_SAVE_RESTORE_FAILURE=0x19",
                "EXIT_CODE_LOCKED_REGISTER_OR_INTERFACE=0x1A",
                "EXIT_CODE_UNSUPPORTED_PROCESSOR=0x1B",
            };

            U32 i;
            for (i = 0; i < options->exit_state.error_code_count; i++)
            {
                U32 error_code = options->exit_state.error_codes[i];
                grub_printf("exit_state.error_codes[%u] = %04xh %s\n",
                            i, error_code, error_code < ARRAY_SIZE(error_strings) ? error_strings[error_code] : "");
            }
        }
    }
}

static U32 _prepareToCallPpmRefCode(rcm_func entry_point)
{
    struct ppm_setup_options *options;
    unsigned char buffer_below_1M[8192];
    U32 status;

    if ((U32) (buffer_below_1M + sizeof(buffer_below_1M)) >= 0x100000) {
        grub_printf("Internal error: stack not below 1MB\n");
        return 0;
    }

    options = (struct ppm_setup_options *)grub_malloc(sizeof(struct ppm_setup_options));
    if (!options)
        return 0;
    grub_memset(options, 0, sizeof(struct ppm_setup_options));

    {
        grub_uint32_t total_size;
        static grub_uint8_t *rmcb_base = 0;
        static int rcm_mmap;

        // Find a rmode-segment-aligned zone in conventional memory big
        total_size = 16;
        if (!rmcb_base)
            rmcb_base = grub_mmap_malign_and_register(16, total_size, &rcm_mmap, GRUB_MEMORY_RESERVED, GRUB_MMAP_MALLOC_LOW);
        if (!rmcb_base)
            return grub_error(GRUB_ERR_OUT_OF_MEMORY, "Could not reserve memory for the real mode callback");

        dprintf("rcm", "Real mode callback reserved memory at %p\n", rmcb_base);
        RealModeEnable = 1;
        RealModeAddr = ((grub_uint32_t) rmcb_base) << 12;
        dprintf("rcm", "RMCB: %04lx:%04lx\n", (unsigned long)((RealModeAddr >> 16) & 0x0ffff), (unsigned long)(RealModeAddr & 0x0ffff));

        // @@: pause
        //     jmp @b
        rmcb_base[0] = 0xF3;
        rmcb_base[1] = 0x90;
        rmcb_base[2] = 0xEB;
        rmcb_base[3] = 0xFC;
    }

    //-----------//

    // This 32-bit physical memory address specifies a read-write memory region
    // below 1MB. Minimum size is 8KB.  This memory is used by the callback as
    // the SIPI target and stack for each AP. This region is not required to be
    // cacheable.
    options->mem_region_below_1M = (U32) buffer_below_1M;

    // Number of CPU sockets which exist on the platform
    // Impacts the number of processor bus numbers below the max_bus_num that will be checked.
    // max_bus_num is obtained directly from the processor itself via a PCIEXBAR MSR.
    // If options.num_sockets = 1, then only max_bus_num will be checked for processors.
    // If options.num_sockets = 2, then max_bus_num and max_bus_num-1 will be checked for processors.
    // Example: A single socket platform using bus number 0xfe must specify two sockets
    options->num_sockets = numSockets;

    // Desired state for the P-state initialization
    // 1=enabled; 0=disabled
    options->pstates_enabled = PstateEnable ? 1 : 0;

    // Desired state for the P-state hardware coordination
    // ACPI_PSD_COORD_TYPE_SW_ALL = 0xFC
    // ACPI_PSD_COORD_TYPE_SW_ANY = 0xFD
    // ACPI_PSD_COORD_TYPE_HW_ALL = 0xFE
    options->pstate_coordination = 0xFE;

    // Desired state for the Turbo state initialization
    // 1=enabled; 0=disabled
    options->turbo_enabled = TurboEnable ? 1 : 0;

    // Desired state for the C-state initialization
    // 1=enabled; 0=disabled
    options->cstates_enabled = CstateEnable ? 1 : 0;

    // Desired state for the C1E initialization
    // 1=enabled; 0=disabled
    options->c1e_enabled = 1;

    // Desired state for the Nehalem core C3 state included in the _CST
    // 0= Nehalem core C3 cannot be used as an ACPI C state
    // 2= Nehalem core C3 can be used as an ACPI C2 state
    // 3= Nehalem core C3 can be used as an ACPI C3 state
    options->c3_enabled = c3;

    // Desired state for the Nehalem core C6 state included in the _CST as an
    // ACPI C3 state.
    // 1= Nehalem core C6 can be used as an ACPI C3 state
    // 0= Nehalem core C6 cannot be used as an ACPI C3 state
    options->c6_enabled = c6Enable ? 1 : 0;

    // Desired state for the Nehalem core C7 state included in the _CST as an
    // ACPI C3 state.
    // 1= Nehalem core C7 can be used as an ACPI C7 state
    // 0= Nehalem core C7 cannot be used as an ACPI C7 state
    options->c7_enabled = c7Enable ? 1 : 0;

    // Desired state for providing alternate ACPI _CST structure using MWAIT
    // extensions
    // 1= Alternate _CST using MWAIT extension is enabled for OSPM use
    // 0= Alternate _CST using MWAIT extension is disabled for OSPM use
    options->mwait_enabled = 1;

    // Power management base address used for processors
    options->pmbase = pmbase;

    // Desired state for the C-state package limit.
    // Note: The C-state package limit may be further limited by the
    // capabilities of the processor
    // 000b = C0 (No package C-state support)
    // 001b = C1 (Behavior is the same as 000b)
    // 010b = C3
    // 011b = C6
    // 100b = C7
    // 111b = No package C-state limit
    options->package_cstate_limit = 7;

    // Desired state for the T-state initialization
    // 1=enabled; 0=disabled
    options->tstates_enabled = TstateEnable ? 1 : 0;

    // This 32-bit physical memory address specifies a read-write memory region
    // for the SSDT ACPI structure built by the PPM RC.
    options->ssdt_mem_addr = ssdt_addr;

    // This value specifies the size of the SSDT memory region.
    options->ssdt_mem_size = ssdt_size;

    // This value specifies the PPM RCM behavior related to creation and
    // incorporation of the new SSDT ACPI structure. See definition of the
    // SSDT_LOC_FLAG for acceptable values.
    options->ssdt_loc_flag = SsdtInsertEnable ? SSDT_LOC_FLAG_ADDR_PROVIDED_INSERT : SSDT_LOC_FLAG_ADDR_PROVIDED_NO_INSERT;

    // This value specifies the PPM RCM behavior related to creation and
    // incorporation of the new SSDT ACPI structure. If all power management
    // features are disabled by input options, the SSDT can still be created
    // for debug review.
    // 1 = Create SSDT even if all power management features are disabled
    // 0 = Do not create SSDT if all power management features are disabled
    options->ssdt_force_creation = 1;

    options->cpu_namespace_flag = cpuNamespace;

    options->ppm_rcm_interface_specification = CURRENT_PPM_RCM_INTERFACE_SPECIFICATION << 16 |
                                               CURRENT_PPM_RCM_INTERFACE_MINOR_REVISION;
    dprintf("rcm", "ppm_rcm_interface_specification = %x\n", options->ppm_rcm_interface_specification);

    options->realmode_callback_flag = RealModeEnable;

    options->realmode_callback_address = RealModeAddr;

    options->cpu_map_count = cpu_map_count;
    options->cpu_map = cpu_map;

    // This flag indicates whether or not PPM RC should update an existing ACPI FADT.
    // modify_fadt_flag = 0 means do not modify existing ACPI FADT structure
    // modify_fadt_flag = 1 means do check and if needed, modify existing ACPI FADT structure
    // FIXME: add an option for this
    options->modify_fadt_flag = 0;

    options->performance_per_watt = PerfWattOptEnable ? 1 : 0;

    options->acpi_access = AcpiEnable;

    options->logical_cpu_count = smp_init();

    /* Launch the PPM Initialization Reference Code */
    status = entry_point(options);
    dump_exit_state(options);

    smp_phantom_init();
    acpica_terminate();

    grub_free(options);

    return status;
}

static struct acpi_tables *init_acpi(void)
{
    static struct acpi_tables acpi_tables;
    static U32 acpi_init_state = 0;
    U32 num_tables, index;
    U8 *current;
    ACPI_TABLE_HEADER *header;

    if (acpi_init_state)
        return &acpi_tables;

    acpi_init_state = FindAcpiTables(&acpi_tables);
    if (!acpi_init_state) {
        grub_error(GRUB_ERR_IO, "Failed to find ACPI tables");
        return NULL;
    }

    dprintf("rcm_acpi", "Found ACPI tables\n");
    dprintf("rcm_acpi", "RSD  = %p\n", acpi_tables.RsdPointer);

    dprintf("rcm_acpi", "RSDT = %p\n", acpi_tables.RsdtPointer);
    num_tables = get_num_tables(acpi_tables.RsdtPointer);
    for (index = 0; index < num_tables; index++) {
        current = (U8 *) acpi_tables.RsdtPointer->TableOffsetEntry[index];
        current = decodeTableHeader(current, &header);
        dprintf("rcm_acpi", "RSDT[%u] = %p  ", index, header);
        dprint_nameseg(*(U32 *) header->Signature);
        dprintf("rcm_acpi", "\n");
    }

    dprintf("rcm_acpi", "XSDT = %p\n", acpi_tables.XsdtPointer);
    num_tables = get_num_tables64(acpi_tables.XsdtPointer);
    for (index = 0; index < num_tables; index++) {
        U64 ptr = acpi_tables.XsdtPointer->TableOffsetEntry[index];
        dprintf("rcm_acpi", "XSDT[%u] = 0x%llx  ", index, ptr);
        if (ptr <= GRUB_ULONG_MAX)
            dprint_nameseg(*(U32 *) ((ACPI_TABLE_HEADER *) (unsigned long)ptr)->Signature);
        else
            dprintf("rcm_acpi", "(beyond addressable memory in this CPU mode)");
        dprintf("rcm_acpi", "\n");
    }

    dprintf("rcm_acpi", "DSDT = %p\n", acpi_tables.DsdtPointer);
    dprintf("rcm_acpi", "FACP = %p\n", acpi_tables.FacpPointer);
    dprintf("rcm_acpi", "FACS = %p\n", acpi_tables.FacsPointer);
    dprintf("rcm_acpi", "MADT = %p\n", acpi_tables.MadtPointer);

    return &acpi_tables;
}

static grub_err_t generate_cpu_map_from_acpi(void)
{
    struct acpi_tables *acpi_tables;
    PROCESSOR_NUMBER_TO_NAMESEG *map = cpu_map;
    U32 processor_namespace = 0;
    U32 cpu;
    U8 *current, *end;
    ACPI_TABLE_HEADER *header;
    struct acpi_namespace ns;

    dprintf("rcm_acpi", "Attempting to autodetect CPU map from ACPI DSDT; wish me luck\n");

    acpi_tables = init_acpi();
    if (!acpi_tables)
        return grub_errno;

    current = (U8 *) acpi_tables->DsdtPointer;
    current = decodeTableHeader(current, &header);
    end = current - sizeof(*header) + header->Length;
    ns.depth = 0;
    acpi_processor_count = 0;
    parse_acpi_termlist(&ns, current, end);

    if (acpi_processor_count > CPU_MAP_LIMIT)
        return grub_error(GRUB_ERR_IO, "Too many processors for PPM code; found %u processors", acpi_processor_count);
    if (acpi_processor_count == 0)
        return grub_error(GRUB_ERR_IO, "Found no processors in ACPI");
    for (cpu = 0; cpu < acpi_processor_count; cpu++) {
        U32 nameseg;
        if (acpi_processors[cpu].pmbase) {
            U32 cpu_pmbase = acpi_processors[cpu].pmbase - 0x10;
            if (pmbase && cpu_pmbase != pmbase)
                return grub_error(GRUB_ERR_IO, "Found inconsistent pmbase addresses in ACPI: 0x%x and 0x%x", pmbase, cpu_pmbase);
            pmbase = cpu_pmbase;
        }
        if (acpi_processors[cpu].ns.depth > MAX_SUPPORTED_CPU_NAMESEGS + 1)
            return grub_error(GRUB_ERR_IO, "Processor path too deep for PPM; depth %u", acpi_processors[cpu].ns.depth);
        if (processor_namespace && acpi_processors[cpu].ns.nameseg[0] != processor_namespace)
            return grub_error(GRUB_ERR_IO, "Processor namespaces inconsistent");
        processor_namespace = acpi_processors[cpu].ns.nameseg[0];
        map->acpi_processor_number = acpi_processors[cpu].id;
        map->seg_count = acpi_processors[cpu].ns.depth - 1;
        for (nameseg = 0; nameseg < map->seg_count; nameseg++)
            map->nameseg[nameseg] = acpi_processors[cpu].ns.nameseg[nameseg + 1];
        map++;
    }
    if (!pmbase)
        return grub_error(GRUB_ERR_IO, "No pmbase found in ACPI");
    if (processor_namespace == NAMESEG("_PR_"))
        cpuNamespace = CPU_NAMESPACE_PR;
    else if (processor_namespace == NAMESEG("_SB_"))
        cpuNamespace = CPU_NAMESPACE_SB;
    else
        return grub_error(GRUB_ERR_IO, "Found processors in invalid namespace; not _PR_ or _SB_");
    cpu_map_count = map - cpu_map;

    return GRUB_ERR_NONE;
}

static void *get_empty_ssdt(void)
{
    static struct acpi_table_header *ssdt = 0;
    int ssdt_mmap;

    if (ssdt)
        return ssdt;

    ssdt = grub_mmap_malign_and_register(16, sizeof(*ssdt), &ssdt_mmap, GRUB_MEMORY_ACPI, 0);
    if (!ssdt) {
        grub_error(GRUB_ERR_OUT_OF_MEMORY, "Could not reserve memory for an override SSDT");
        return NULL;
    }

    buildTableHeader(ssdt, NAMESEG("SSDT"), NAMESEG64("OVERRIDE"));
    ssdt->Length = sizeof(*ssdt);
    ssdt->Checksum = 0;
    ssdt->Checksum = 0 - GetChecksum(ssdt, ssdt->Length);

    return ssdt;
}

static const struct grub_arg_option options[] = {
#define OPTION_SSDT_ADDR 0
    {"ssdt-addr", 'a', 0, "SSDT address", "ADDR", ARG_TYPE_STRING},
#define OPTION_PMBASE 1
    {"pmbase", 'b', 0, "PMBase address", "ADDR", ARG_TYPE_STRING},
#define OPTION_CSTATE_DISABLE 2
    {"cstate-disable", 'c', 0, "Disable C-states (default=enabled)", 0, 0},
#define OPTION_DEBUG 3
    {"debug", 'd', 0, "Display debug output (default=disabled)", 0, 0},
#define OPTION_C6_DISABLE 4
    {"c6-disable", 'e', 0, "Disable use of C6 as ACPI C3 (default=enabled)", 0, 0},
#define OPTION_C7_DISABLE 5
    {"c7-disable", 'f', 0, "Disable use of C7 as ACPI C3 (default=enabled)", 0, 0},
#define OPTION_C3 6
    {"c3", 'g', 0, "Set C3 Usage (default=0)", "NUM", ARG_TYPE_INT},
#define OPTION_REAL_MODE_CALLBACK 7
    {"real-mode-cb", 'k', 0, "Force Real Mode Callback Address (default=none)", "ADDR", ARG_TYPE_STRING},
#define OPTION_CPU_MAP 8
    {"cpu-map", 'm', 0, "Read CPU map from file", "FILE", ARG_TYPE_STRING},
#define OPTION_CPU_NAMESPACE_SB 9
    {"cpu-namespace-sb", 'n', 0, "Force CPU ACPI Namespace as _SB (default=_PR)", 0, 0},
#define OPTION_PSTATE_DISABLE 10
    {"pstate-disable", 'p', 0, "Disable P-states (default=enabled)", 0, 0},
#define OPTION_SSDT_BUFFER_SIZE 11
    {"ssdt-size", 'r', 0, "Size for SSDT buffer", "SIZE", ARG_TYPE_STRING},
#define OPTION_SSDT_INSERT 12
    {"ssdt-insert", 's', 0, "Enable SSDT Insert (default=disabled)", 0, 0},
#define OPTION_TSTATE_DISABLE 13
    {"tstate-disable", 't', 0, "Disable T-states (default=enabled)", 0, 0},
#define OPTION_TURBO_DISABLE 14
    {"turbo-disable", 'u', 0, "Disable Turbo Mode (default=enabled)", 0, 0},
#define OPTION_VERBOSE 15
    {"verbose", 'v', 0, "Display verbose output (default=disabled)", 0, 0},
#define OPTION_ACPI_DISABLE 16
    {"acpi-disable", 'x', 0, "Disable ACPI processing (default=enabled)", 0, 0},
    {0, 0, 0, 0, 0, 0}
};

static void *buffer;

static grub_err_t uninit_err(grub_err_t passthrough_errno)
{
    grub_free(buffer);
    return passthrough_errno;
}

static grub_err_t uninit(void)
{
    return uninit_err(grub_errno);
}

static grub_err_t grub_cmd_runppm(struct grub_extcmd_context *context, int argc, char **args)
{
    struct grub_arg_list *state = context->state;
    grub_file_t file;
    rcm_func entry_point = PpmStart;
    static void *ssdt_buffer = NULL;

    buffer = NULL;
    acpi_ns_found = 1;

    if (argc > 1)
        return uninit_err(grub_error(GRUB_ERR_BAD_ARGUMENT, "Usage: runppm [RCM_FILE]"));
    else if (argc == 1)
    {
        grub_ssize_t bytes_read;
        file = grub_file_open(args[0]);
        if (!file)
            return uninit();

        buffer = grub_malloc(grub_file_size(file));
        if (!buffer)
            return uninit();

        bytes_read = grub_file_read(file, buffer, grub_file_size(file));
        if (bytes_read < 0 || (grub_off_t) bytes_read != grub_file_size(file))
            return uninit_err(grub_error(GRUB_ERR_FILE_READ_ERROR, "Couldn't read file"));

        grub_file_close(file);

        entry_point = placepe(buffer, (U32) buffer);
        if (!entry_point)
            return uninit_err(grub_error(GRUB_ERR_BAD_FILE_TYPE, "PE relocation failed"));
    }

    AcpiEnable = !state[OPTION_ACPI_DISABLE].set;

    if (AcpiEnable) {
        ssdt_size = 128 * 1024;
        if (state[OPTION_SSDT_BUFFER_SIZE].set)
            if (strtou32_h(state[OPTION_SSDT_BUFFER_SIZE].arg, &ssdt_size) != GRUB_ERR_NONE)
                return uninit();

        ssdt_addr = 0;
        if (state[OPTION_SSDT_ADDR].set)
            if (strtou32_h(state[OPTION_SSDT_ADDR].arg, &ssdt_addr) != GRUB_ERR_NONE)
                return uninit();

        if (ssdt_addr == 0) {
            if (!ssdt_buffer) {
                static int ssdt_mmap;
                ssdt_buffer = grub_mmap_malign_and_register(16, ssdt_size, &ssdt_mmap, GRUB_MEMORY_ACPI, 0);

                if (!ssdt_buffer)
                    return uninit();
            }
            ssdt_addr = (U32) ssdt_buffer;
        }
    } else {
        ssdt_addr = 0;
        ssdt_size = 0;
    }

    pmbase = 0;
    if (state[OPTION_PMBASE].set)
        if (strtou32_h(state[OPTION_PMBASE].arg, &pmbase) != GRUB_ERR_NONE)
            return uninit();

    RealModeEnable = state[OPTION_REAL_MODE_CALLBACK].set;
    RealModeAddr = 0;
    if (RealModeEnable)
        if (strtou32_h(state[OPTION_REAL_MODE_CALLBACK].arg, &RealModeAddr) != GRUB_ERR_NONE)
            return uninit();

    CstateEnable = !state[OPTION_CSTATE_DISABLE].set;

    DebugEnable = state[OPTION_DEBUG].set;

    c6Enable = !state[OPTION_C6_DISABLE].set;

    c7Enable = !state[OPTION_C7_DISABLE].set;

    c3 = 0;
    if (state[OPTION_C3].set)
        c3 = grub_strtoul(state[OPTION_C3].arg, 0, 0);

    cpuNamespace = state[OPTION_CPU_NAMESPACE_SB].set ? CPU_NAMESPACE_SB : CPU_NAMESPACE_PR;

    PstateEnable = !state[OPTION_PSTATE_DISABLE].set;

    SsdtInsertEnable = state[OPTION_SSDT_INSERT].set;

    TstateEnable = !state[OPTION_TSTATE_DISABLE].set;

    TurboEnable = !state[OPTION_TURBO_DISABLE].set;

    VerboseEnable = state[OPTION_VERBOSE].set;

    if (AcpiEnable)
    {
        if (state[OPTION_CPU_MAP].set) {
            char *cpu_map_file_buffer;
            char *pos, *end;
            grub_off_t cpu_map_file_size;
            grub_ssize_t bytes_read;

            file = grub_file_open(state[OPTION_CPU_MAP].arg);
            if (!file)
                return uninit();
            cpu_map_file_size = grub_file_size(file);
            pos = cpu_map_file_buffer = grub_malloc(cpu_map_file_size);
            if (!cpu_map_file_buffer)
                return uninit();
            bytes_read = grub_file_read(file, cpu_map_file_buffer, cpu_map_file_size);
            if (bytes_read < 0 || (grub_off_t) bytes_read != cpu_map_file_size)
                return uninit_err(grub_error(GRUB_ERR_FILE_READ_ERROR, "Couldn't read CPU map file"));

            grub_file_close(file);

            cpu_map_count = 0;
            while (pos < cpu_map_file_buffer + cpu_map_file_size) {
                if (cpu_map_count == CPU_MAP_LIMIT)
                    return uninit_err(grub_error(GRUB_ERR_FILE_READ_ERROR, "Too many entries in CPU map file"));
                {
                    cpu_map[cpu_map_count].acpi_processor_number = grub_strtoul(pos, &end, 0);
                    if (grub_errno != GRUB_ERR_NONE || end == pos || *end != ' ') {
                        grub_printf("cpu_map_count=%02u\n", cpu_map_count);
                        return uninit_err(grub_error(GRUB_ERR_FILE_READ_ERROR, "Couldn't parse CPU number from CPU map file"));
                    }
                    pos = end + 1;
                }
                {
                    cpu_map[cpu_map_count].seg_count = grub_strtoul(pos, &end, 0);
                    if (grub_errno != GRUB_ERR_NONE || end == pos || *end != ' ')
                        return uninit_err(grub_error(GRUB_ERR_FILE_READ_ERROR, "Couldn't parse NAMESEG count from CPU map file"));
                    pos = end;
                }
                if (cpu_map[cpu_map_count].seg_count > MAX_SUPPORTED_CPU_NAMESEGS)
                    return uninit_err(grub_error(GRUB_ERR_FILE_READ_ERROR, "NAMESEG count from CPU map file is greater than supported value"));
                {
                    U32 j;
                    for (j = 0; j < cpu_map[cpu_map_count].seg_count; j++) {
                        if (pos + 5 > cpu_map_file_buffer + cpu_map_file_size)
                            return uninit_err(grub_error(GRUB_ERR_FILE_READ_ERROR, "Hit end of CPU map file when reading nameseg"));
                        if (*pos != ' ')
                            return uninit_err(grub_error(GRUB_ERR_FILE_READ_ERROR, "No space between nameseg in CPU map file"));
                        pos++;
                        cpu_map[cpu_map_count].nameseg[j] = NAMESEG(pos);
                        pos += 4;
                    } // end for
                    if (*pos != '\n') {
                        grub_printf("cpu_map_count=%02u\n", cpu_map_count);
                        return uninit_err(grub_error(GRUB_ERR_FILE_READ_ERROR, "No newline after nameseg in CPU map file"));
                    }
                    pos++;
                    cpu_map_count++;
                } // end for
            }
            grub_free(cpu_map_file_buffer);
        } else {
            generate_cpu_map_from_acpi();
            if (grub_errno != GRUB_ERR_NONE)
                return uninit_err(grub_errno);
        }

        if (DebugEnable) {
            unsigned int i;
            grub_printf("CPU map has %u entries\n", cpu_map_count);
            for (i = 0; i < cpu_map_count; i++) {
                grub_printf("0x%02x (%u) -> ", cpu_map[i].acpi_processor_number, cpu_map[i].seg_count);
                {
                    unsigned int j;
                    for (j = 0; j < cpu_map[i].seg_count; j++) {
                        print_nameseg(cpu_map[i].nameseg[j]);
                        grub_printf(" ");
                    }
                }
                grub_printf("\n");
            }
        }
    }
    dprintf("rcm", "Calling PPM entry point %p\n", entry_point);

    if (!_prepareToCallPpmRefCode(entry_point))
        return uninit_err(grub_error(GRUB_ERR_BAD_DEVICE, "PPM RCM failed"));

    if (AcpiEnable)
    {
        struct acpi_tables *acpi_tables;
        void *empty_ssdt;
        U32 num_tables, i;
        U8 *current, *end;
        ACPI_TABLE_HEADER *header;
        struct acpi_namespace ns;
        bool inserted_my_ssdt;

        dprintf("rcm_ssdt", "Making corrections for conflicting SSDT\n");

        acpi_tables = init_acpi();
        if (!acpi_tables)
            return uninit();

        empty_ssdt = get_empty_ssdt();
        if (!empty_ssdt)
            return uninit();

        inserted_my_ssdt = SsdtInsertEnable;
        num_tables = get_num_tables(acpi_tables->RsdtPointer);
        for (i = 0; i < num_tables; i++) {
            current = (U8 *) acpi_tables->RsdtPointer->TableOffsetEntry[i];
            current = decodeTableHeader(current, &header);
            end = current - sizeof(*header) + header->Length;
            ns.depth = 0;
            dprintf("rcm_acpi", "RSDT[%u] = %p  ", i, header);
            dprint_nameseg(*(U32 *) header->Signature);
            dprintf("rcm_acpi", "\n");

            if (*(U32 *) header->Signature != NAMESEG("SSDT"))
                continue;

            dprintf("rcm_ssdt", "Checking SSDT at %p\n", header);

            acpi_ns_found = 0;
            parse_acpi_termlist(&ns, current, end);

            if (acpi_ns_found || header == empty_ssdt) {
                dprintf("rcm_ssdt", "Found SSDT containing a processor namespace\n");
                if (SsdtInsertEnable) {
                    if (i != num_tables - 1)
                        acpi_tables->RsdtPointer->TableOffsetEntry[i] = (U32) empty_ssdt;
                    else if (acpi_tables->RsdtPointer->TableOffsetEntry[i] != ssdt_addr)
                        grub_printf("Error: PPM code told to insert itself but last table in RSDT not the new SSDT");
                } else {
                    acpi_tables->RsdtPointer->TableOffsetEntry[i] = inserted_my_ssdt ? (U32) empty_ssdt : ssdt_addr;
                    inserted_my_ssdt = true;
                }
            }
            acpi_ns_found = 1;
        }

        if (inserted_my_ssdt)
            SetChecksum(&acpi_tables->RsdtPointer->Header);
        else
            InsertSsdt(acpi_tables->RsdtPointer, (ACPI_TABLE_SSDT *) ssdt_addr);

        inserted_my_ssdt = SsdtInsertEnable;
        num_tables = get_num_tables64(acpi_tables->XsdtPointer);
        for (i = 0; i < num_tables; i++) {
            U64 ptr = acpi_tables->XsdtPointer->TableOffsetEntry[i];
            if (ptr <= GRUB_ULONG_MAX) {
                current = (U8 *) (unsigned long)ptr;
                current = decodeTableHeader(current, &header);
                end = current - sizeof(*header) + header->Length;
                ns.depth = 0;
                dprintf("rcm_acpi", "XSDT[%u] = %p  ", i, header);
                dprint_nameseg(*(U32 *) header->Signature);
                dprintf("rcm_acpi", "\n");

                if (*(U32 *) header->Signature != NAMESEG("SSDT"))
                    continue;

                dprintf("rcm_ssdt", "Checking SSDT at %p\n", header);

                acpi_ns_found = 0;
                parse_acpi_termlist(&ns, current, end);

                if (acpi_ns_found || header == empty_ssdt) {
                    dprintf("rcm_ssdt", "Found SSDT containing a processor namespace\n");
                    if (SsdtInsertEnable) {
                        if (i != num_tables - 1)
                            acpi_tables->XsdtPointer->TableOffsetEntry[i] = (U32) empty_ssdt;
                        else if (acpi_tables->XsdtPointer->TableOffsetEntry[i] != ssdt_addr)
                            grub_printf("Error: PPM code told to insert itself but last table in XSDT not the new SSDT");
                    } else {
                        acpi_tables->XsdtPointer->TableOffsetEntry[i] = inserted_my_ssdt ? (U32) empty_ssdt : ssdt_addr;
                        inserted_my_ssdt = true;
                    }
                }
                acpi_ns_found = 1;
            } else
                grub_printf("Table in XSDT outside 32-bit addressable memory: 0x%llx\n", ptr);
        }
        if (inserted_my_ssdt)
            SetChecksum(&acpi_tables->XsdtPointer->Header);
        else
            InsertSsdt64(acpi_tables->XsdtPointer, (ACPI_TABLE_SSDT *) ssdt_addr);
    }

    return uninit_err(GRUB_ERR_NONE);
}

static void *file_to_buffer(const char *filename)
{
    grub_file_t file;
    grub_off_t file_size;
    void *buf;
    grub_ssize_t bytes_read;

    file = grub_file_open(filename);
    if (!file) {
        grub_error(GRUB_ERR_FILE_READ_ERROR, "Failed to open file: %s", filename);
        return 0;
    }

    file_size = grub_file_size(file);
    buf = grub_malloc(file_size);
    if (!buf) {
        grub_error(GRUB_ERR_OUT_OF_MEMORY, "Out of memory");
        return 0;
    }

    bytes_read = grub_file_read(file, buf, file_size);
    if (bytes_read < 0 || (grub_off_t) bytes_read != file_size) {
        grub_error(GRUB_ERR_FILE_READ_ERROR, "Couldn't read file: %s", filename);
        grub_file_close(file);
        return 0;
    }

    grub_file_close(file);

    return buf;
}

static void print_namespace(const struct acpi_namespace *ns)
{
    U32 i;
    grub_printf("\\");
    for (i = 0; i < ns->depth; i++) {
        if (i != 0)
            grub_printf(".");
        print_nameseg(ns->nameseg[i]);
    }
}

static grub_err_t grub_cmd_cpu_acpi(struct grub_extcmd_context *context, int argc, char **args)
{
    U32 status;
    ACPI_TABLES acpi_tables;
    U8 *current;
    U8 *end;
    void *buf = NULL;
    U32 index;
    U32 num_tables;

    (void)context;

    if (argc > 1)
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Need 0 or 1 argument: [DSDT_FILE]");
    else if (argc == 1) {
        if ((buf = file_to_buffer(args[0])) == NULL)
            return grub_errno;
        current = buf;
    } else {
        if ((status = FindAcpiTables(&acpi_tables)) == 0) {
            grub_printf("Failed to find ACPI tables\n");
            return GRUB_ERR_NONE;
        }

        grub_printf("Found ACPI tables\n");
        grub_printf("RSD  = %p\n", acpi_tables.RsdPointer);

        grub_printf("RSDT = %p\n", acpi_tables.RsdtPointer);
        num_tables = get_num_tables(acpi_tables.RsdtPointer);
        for (index = 0; index < num_tables; index++) {
            ACPI_TABLE_HEADER *header = (ACPI_TABLE_HEADER *) acpi_tables.RsdtPointer->TableOffsetEntry[index];
            grub_printf("RSDT[%u] = %p  ", index, (void *)header);
            print_nameseg(*(U32 *) header->Signature);
            grub_printf("\n");

            if (*(U32 *) header->Signature == NAMESEG("SSDT")) {
                ACPI_TABLE_HEADER *tableHeader;
                struct acpi_namespace ns;

                current = (U8 *) header;
                ns.depth = 0;
                current = decodeTableHeader(current, &tableHeader);
                end = current - sizeof(*tableHeader) + tableHeader->Length;

                parse_acpi_termlist(&ns, current, end);
            }
        }

        grub_printf("XSDT = %p\n", acpi_tables.XsdtPointer);
        num_tables = get_num_tables64(acpi_tables.XsdtPointer);
        for (index = 0; index < num_tables; index++) {
            U64 ptr = acpi_tables.XsdtPointer->TableOffsetEntry[index];
            grub_printf("XSDT[%u] = 0x%llx  ", index, ptr);
            if (ptr <= GRUB_ULONG_MAX)
                print_nameseg(*(U32 *) ((ACPI_TABLE_HEADER *) (unsigned long)ptr)->Signature);
            else
                grub_printf("(beyond addressable memory in this CPU mode)");
            grub_printf("\n");
        }

        grub_printf("DSDT = %p\n", acpi_tables.DsdtPointer);
        grub_printf("FACP = %p\n", acpi_tables.FacpPointer);
        grub_printf("FACS = %p\n", acpi_tables.FacsPointer);
        grub_printf("MADT = %p\n", acpi_tables.MadtPointer);

        current = (U8 *) acpi_tables.DsdtPointer;
    }

    {
        ACPI_TABLE_HEADER *tableHeader;
        struct acpi_namespace ns;
        U32 cpu;

        ns.depth = 0;
        current = decodeTableHeader(current, &tableHeader);
        end = current - sizeof(*tableHeader) + tableHeader->Length;

        acpi_processor_count = 0;
        parse_acpi_termlist(&ns, current, end);

        grub_printf("Found %u processor structures\n", acpi_processor_count);
        for (cpu = 0; cpu < acpi_processor_count; cpu++) {
            grub_printf("%u pmbase=0x%x id=0x%x ", cpu, acpi_processors[cpu].pmbase, acpi_processors[cpu].id);
            print_namespace(&acpi_processors[cpu].ns);
            grub_printf("\n");
        }
    }

    grub_free(buf);

    return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd_runppm;
static grub_extcmd_t cmd_cpu_acpi;

GRUB_MOD_INIT(rcm)
{
    cmd_runppm = grub_register_extcmd("runppm", grub_cmd_runppm, 0,
                                      "[RCM_FILE]",
                                      "Run a PPM RCM, interface version " CURRENT_PPM_RCM_INTERFACE_SPECIFICATION_STR "." CURRENT_PPM_RCM_INTERFACE_MINOR_REVISION_STR "\n"
                                      "Please see nehalem-ppm-rcm-v" CURRENT_PPM_RCM_INTERFACE_SPECIFICATION_STR ".txt for detailed documentation.",
                                      options);

    cmd_cpu_acpi = grub_register_extcmd("cpu_acpi", grub_cmd_cpu_acpi, 0, 0,
                                        "Find ACPI tables like \"runppm\" does, for debugging purposes",
                                        0);
}

GRUB_MOD_FINI(rcm)
{
    grub_unregister_extcmd(cmd_cpu_acpi);
    grub_unregister_extcmd(cmd_runppm);
}
