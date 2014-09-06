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

#include "datatype.h"
#include "smp.h"
#include "portable.h"
#include "acpi.h"
#include "acpicode.h"
#include "cpu.h"
#include "ppm.h"
#include "ppmsetup.h"
#include "ppmstart.h"
#include "detect_cpu.h"
#include "bitsutil.h"

//-----------------------------------------------------------------------------
// forward declarations
static U32  init_host(PPM_HOST * host, PPM_SETUP_OPTIONS * options);
static U32  collect_socket_info(PPM_HOST * host);
// Called on AP, cannot be static or compiler optimization may interfere with parameter passing
void find_socket_proxy(void * param);
static U32  determine_configuration(PPM_HOST * host);
static void GetMaxRatio(PPM_HOST * host, U32 * max_non_turbo_ratio);
// Called on AP, cannot be static or compiler optimization may interfere with parameter passing
void collect_cpu_info(PPM_HOST * host, CPU_DETAILS * cpu);
// Called on AP, cannot be static or compiler optimization may interfere with parameter passing
void init_logical_cpu(void * param);
static U32  BuildPstateInfo(PPM_HOST * host);
static U32  BuildCstateInfo(PPM_HOST * host);
static U32  BuildTstateInfo(PPM_HOST * host);
static U32  FindPkgIndex(PPM_HOST * host, U32 apicId, U32 * pkgIndex);
static U32  FindNamePath(PPM_HOST * host, U32 acpiProcessorId, PROCESSOR_NUMBER_TO_NAMESEG ** aslCpuNamePath);
static U32  ProcessMadtInfo(PPM_HOST * host);
static U32  ProcessSsdt(PPM_HOST * host);
static U32  ProcessFadtTables(PPM_HOST * host);
static U32  BuildSsdt(PPM_HOST * host, void * buffer, U32 bufferSize);
static void set_error_code(PPM_HOST * host, EXIT_CODE error_code);
static void set_exit_state(PPM_HOST * host, U32 return_status);
static void shutdown(PPM_HOST * host, EXIT_CODE error_code, U32 return_status);
static U32  detect_and_enable_energy_perf_bias_msr(PPM_HOST * host);
static void run_on_all_cpu(CALLBACK function, PPM_HOST * host);

static void * buildCpuScope(void * current, U32 cpu_namespace, PROCESSOR_NUMBER_TO_NAMESEG * aslCpuNamePath);
static void * buildPDC(void * current);
static void * buildOSC(void * current);
static void * buildPSS(PPM_HOST * host, void * current, PKG_PSTATES * pkg_pstates);
static void * buildPPC(void * current);
static void * buildPCT(void * current);
static void * buildPSD(void * current, U32 domain, U32 cpusInDomain, U32 pstate_coordination);
static void * buildCST(void * current, PKG_CSTATES * mwait_pkg_cstates, PKG_CSTATES * io_pkg_cstates);
static void * buildReturnPackageCST(void * current, PKG_CSTATES * pkg_cstates);
static void * buildCstate(void * current, ACPI_GENERIC_ADDRESS * gas, CSTATE * cstate);
#ifdef BUILD_ACPI_CSD
static void * buildCSD(void * current, U32 domain, U32 cpusInDomain, PKG_CSTATES * pkg_cstates);
#endif
static void * buildTPC(void * current);
static void * buildPTC(void * current);
static void * buildTSS(void * current, PKG_TSTATES * pkg_tstates);
static void * buildTSD(void * current, U32 domain, U32 cpusInDomain);
static U32 encode_pstate(PPM_HOST * host, U32 ratio);
static U32 get_bclk(PPM_HOST * host);
static U32 compute_tdp(PPM_HOST * host, CPU_DETAILS * cpu);
static U32 compute_pstate_power(PPM_HOST * host, CPU_DETAILS * cpu, U32 ratio, U32 TDP);
static U32 acpi_access(PPM_HOST * host);

//-----------------------------------------------------------------------------
// Entrypoint for the Processor Power Management RC
asmlinkage U32 PpmStart(PPM_SETUP_OPTIONS * options)
{
   U32 status;

   // Create main data structure
   PPM_HOST host;

#if 0
   _asm
      {
      waiting:
         pause
         jmp waiting
      }
#endif

   // Quick sanity check to see if options is a NULL pointer
   if (options == 0L)
      return(0);

   {
      // Decompose the revision into a major (upper 16 bits) and minor (lower 16 bits)
      U16 major = (U16)(options->ppm_rcm_interface_specification >> 16);
      U16 minor = (U16)(options->ppm_rcm_interface_specification & 0xffff);

      // Verify the PPM RCM specification is acceptable according one of these rules:
      // (1) PPM RCM specification is equal the previous revision (special case for portability)
      // OR
      // (2) Major revision matches AND minor revision is less than or equal to RC minor revision
      if ((options->ppm_rcm_interface_specification == CURRENT_PPM_RCM_INTERFACE_SPECIFICATION - 1 ) ||
          ((major == CURRENT_PPM_RCM_INTERFACE_SPECIFICATION) && (minor <= CURRENT_PPM_RCM_INTERFACE_MINOR_REVISION)))
         ;
      else
         return(0);
   }

   // Perform minimal init of main data structure
   if ( ( status = init_host(&host, options) ) == 0 )
   {
      shutdown(&host, EXIT_CODE_FAILED_HOST_INIT, 0);
      return(0);
   }

   if (acpi_access(&host))
   {
      // Find all existing ACPI tables
      if ( ( status = FindAcpiTables(&host.acpi_tables) ) == 0 )
      {
         shutdown(&host, EXIT_CODE_FAILED_FIND_ACPI_TABLES, 0);
         return(0);
      }
   }

   if (acpi_access(&host))
   {
      // Process the MADT to find all enabled logical processors
      if ( ( status = ProcessMadt(host.acpi_tables.MadtPointer, &host.madt_info) ) == 0)
      {
         shutdown(&host, EXIT_CODE_FAILED_PROCESS_MADT, 0);
         return(0);
      }
   }

   // Collect CPU information from each socket
   if ( ( status = collect_socket_info(&host) ) == 0 )
   {
      shutdown(&host, EXIT_CODE_FAILED_COLLECT_SOCKET_INFO, 0);
      return(0);
   }

   // Compare collected data vs. options to determine consistent configuration
   if ( ( status = determine_configuration(&host) ) == 0 )
   {
      shutdown(&host, EXIT_CODE_FAILED_DETERMINE_CONFIGURATION, 0);
      return(0);
   }

   // Build P-state table info based on verified options
   if ( ( status = BuildPstateInfo(&host) ) == 0 )
   {
      shutdown(&host, EXIT_CODE_FAILED_BUILD_PSTATES, 0);
      return(0);
   }

   // Build C-state table info based on verified options
   if ( ( status = BuildCstateInfo(&host) ) == 0 )
   {
      shutdown(&host, EXIT_CODE_FAILED_BUILD_CSTATES, 0);
      return(0);
   }

   // Build T-state table info based on verified options
   if ( ( status = BuildTstateInfo(&host) ) == 0 )
   {
      shutdown(&host, EXIT_CODE_FAILED_BUILD_TSTATES, 0);
      return(0);
   }

   if (acpi_access(&host))
   {
      // Process FADT tables(s)
      if ( ( status = ProcessFadtTables(&host) ) == 0 )
      {
         shutdown(&host, EXIT_CODE_FAILED_UPDATE_FADT, 0);
         return(0);
      }
   }

   if (acpi_access(&host))
   {
      // Process the MADT Info against the existing processor sockets
      if ( ( status = ProcessMadtInfo(&host) ) == 0)
      {
         shutdown(&host, EXIT_CODE_FAILED_PROCESS_MADT_INFO, 0);
         return(0);
      }
   }

   if (acpi_access(&host))
   {
      // Build and Save SSDT within ACPI
      if ( ( status = ProcessSsdt(&host) ) == 0)
      {
         shutdown(&host, EXIT_CODE_FAILED_PROCESS_SSDT, 0);
         return(0);
      }
   }

   // Initialize all processors
   run_on_all_cpu(init_logical_cpu, &host);

   // Shutdown all return with a successful status
   shutdown(&host, EXIT_CODE_PPM_COMPLETED, 1);

   /* FIXME: Handle real-mode callback */

   return(1);
}

static U32 acpi_access(PPM_HOST * host)
{
   // Decompose the revision and determine minor revision (lower 16 bits)
   U16 minor = (U16)(host->options->ppm_rcm_interface_specification & 0xffff);

   if (minor < 2)
      return 1;
   return (host->options->acpi_access == 1) ? 1 : 0;
}

//-----------------------------------------------------------------------------
static void run_on_all_cpu(CALLBACK function, PPM_HOST * host)
{
   U32 nproc;
   const CPU_INFO *cpu;
   U32 i;

   nproc = smp_init();
   cpu = smp_read_cpu_list();

   for (i = 0; i < nproc; i++)
      smp_function(cpu[i].apicid, function, host);
}

//-----------------------------------------------------------------------------
static void set_exit_state(PPM_HOST * host, U32 return_status)
{
   EXIT_STATE * exit_state = &host->options->exit_state;

   exit_state->return_status = return_status;
   exit_state->pstates_enabled = host->pstates_enabled;
   exit_state->turbo_enabled = host->turbo_enabled;
   exit_state->cstates_enabled = host->cstates_enabled;
   exit_state->tstates_enabled = host->tstates_enabled;

   // Clear unused entries in the array of error codes
   {
      U32 index=exit_state->error_code_count;
      for(; index<(sizeof(exit_state->error_codes)/sizeof(exit_state->error_codes[0])); index++)
         exit_state->error_codes[index]=0;
   }
}

//-----------------------------------------------------------------------------
static void set_error_code(PPM_HOST * host, U32 error_code)
{
   EXIT_STATE * exit_state = &host->options->exit_state;

   if ( exit_state->error_code_count < (sizeof(exit_state->error_codes)/sizeof(exit_state->error_codes[0])) )
      exit_state->error_codes[exit_state->error_code_count++] = error_code;
}

//-----------------------------------------------------------------------------
static void set_smp_error_code(PPM_HOST * host, U32 error_code)
{
   SMP_EXIT_STATE * smp_exit_state = &host->smp_exit_state;

   // if HOST pointer is not valid then return without touching global data structures
   if (host->signature != NAMESEG("HOST"))
      return;

   if ( smp_exit_state->error_code_count < (sizeof(smp_exit_state->error_codes)/sizeof(smp_exit_state->error_codes[0])) )
      smp_exit_state->error_codes[smp_exit_state->error_code_count++] = error_code;
}

//-----------------------------------------------------------------------------
static U32 ProcessSsdt(PPM_HOST * host)
{
   U32 status;

   if ((host->pstates_enabled == 0) &&
       (host->cstates_enabled == 0) &&
       (host->tstates_enabled == 0) &&
       (host->options->ssdt_force_creation != 1))
   {
      set_error_code(host, EXIT_CODE_NO_SSDT_CREATED);
      return(1);
   }

   switch(host->options->ssdt_loc_flag)
   {
      case SSDT_LOC_FLAG_ACPI_RECLAIM:
      {
         // Build SSDT in stack buffer and move in ACPI Reclaim memory later

         // Create buffer for SSDT
         U8 memory_for_ssdt[20 * 1024];

         // Build the SSDT
         if ( ( status = BuildSsdt(host, memory_for_ssdt, sizeof(memory_for_ssdt) ) ) == 0)
         {
            set_error_code(host, EXIT_CODE_FAILED_BUILD_SSDT);
            return (0);
         }

         // Adjust RSDT downward, insert SSDT in space created, and insert SSDT pointer into new RSDT
         {
            // Create pointer to SSDT just built in the stack buffer
            ACPI_TABLE_SSDT * old_ssdt = (ACPI_TABLE_SSDT *)memory_for_ssdt;

            // Determine location for updated RSDT
            ACPI_TABLE_RSDT * new_rsdt =  (ACPI_TABLE_RSDT *)
                                          ((U8 *)host->acpi_tables.RsdtPointer
                                          - sizeof(ACPI_TABLE_SSDT *)
                                          - old_ssdt->Header.Length);

            // Determine location for new SSDT
            ACPI_TABLE_SSDT * new_ssdt =  (ACPI_TABLE_SSDT *)
                                          ((U8 *)new_rsdt +
                                          host->acpi_tables.RsdtPointer->Header.Length +
                                          sizeof(ACPI_TABLE_SSDT *));

            // Insert SSDT pointer into existing ACPI structures
            MoveRsdtInsertSsdt( host->acpi_tables.RsdPointer,
                                host->acpi_tables.RsdtPointer,
                                new_rsdt,
                                new_ssdt);

            // Copy SSDT into targeted location in ACPI Reclaim space
            // Move the SSDT from stack buffer into memory
            memcpy (new_ssdt, old_ssdt, old_ssdt->Header.Length);

            // Save final SSDT pointer
            host->acpi_tables.SsdtPointer = new_ssdt;
         }
         break;
      } // case SSDT_LOC_FLAG_ACPI_RECLAIM

      case SSDT_LOC_FLAG_ADDR_PROVIDED:
      {
         // Build SSDT in place

         // Build the SSDT
         if ( ( status = BuildSsdt(host, (void *)host->options->ssdt_mem_addr, host->options->ssdt_mem_size) ) == 0)
         {
            set_error_code(host, EXIT_CODE_FAILED_BUILD_SSDT);
            return (0);
         }

         // Adjust RSDT downward and insert SSDT pointer into new RSDT
         {
            // Determine location for updated RSDT
            ACPI_TABLE_RSDT * new_rsdt =  (ACPI_TABLE_RSDT *) ((U8 *)host->acpi_tables.RsdtPointer
                                           -sizeof(ACPI_TABLE_SSDT *));

            // Update SSDT pointer
            host->acpi_tables.SsdtPointer = (ACPI_TABLE_SSDT *)host->options->ssdt_mem_addr;

            // Insert SSDT pointer into existing ACPI structures
            MoveRsdtInsertSsdt(host->acpi_tables.RsdPointer,
                               host->acpi_tables.RsdtPointer,
                               new_rsdt,
                               host->acpi_tables.SsdtPointer);
         }
         break;
      } // case SSDT_LOC_FLAG_ADDR_PROVIDED

      case SSDT_LOC_FLAG_ADDR_PROVIDED_NO_INSERT:
      {
         // Build SSDT in place

         // Build the SSDT
         if ( ( status = BuildSsdt(host, (void *)host->options->ssdt_mem_addr, host->options->ssdt_mem_size) ) == 0)
         {
            set_error_code(host, EXIT_CODE_FAILED_BUILD_SSDT);
            return (0);
         }
         break;
      } // case SSDT_LOC_FLAG_ADDR_PROVIDED_NO_INSERT

      case SSDT_LOC_FLAG_ADDR_PROVIDED_INSERT:
      {
         // Build SSDT in place

         // Build the SSDT
         if ( ( status = BuildSsdt(host, (void *)host->options->ssdt_mem_addr, host->options->ssdt_mem_size) ) == 0)
         {
            set_error_code(host, EXIT_CODE_FAILED_BUILD_SSDT);
            return (0);
         }

         // Insert SSDT pointer into existing RSDT
         {
            // Update SSDT pointer
            host->acpi_tables.SsdtPointer = (ACPI_TABLE_SSDT *)host->options->ssdt_mem_addr;

            // Insert SSDT pointer into existing ACPI structures
            InsertSsdt( host->acpi_tables.RsdtPointer, host->acpi_tables.SsdtPointer);
            if (host->acpi_tables.XsdtPointer)
               InsertSsdt64( host->acpi_tables.XsdtPointer, host->acpi_tables.SsdtPointer);
         }
         break;
      } // case SSDT_LOC_FLAG_ADDR_PROVIDED_INSERT

      default:
      {
         set_error_code(host, EXIT_CODE_FAILED_INVALID_SSDT_LOCATION_FLAG);
         return(0);
      } //default

   } // switch

   host->options->exit_state.ssdt_mem_addr = (U32) host->acpi_tables.SsdtPointer;
   host->options->exit_state.ssdt_mem_size = host->acpi_tables.SsdtPointer->Header.Length;

   return(1);
}

//-----------------------------------------------------------------------------
U32 init_host(PPM_HOST * host, PPM_SETUP_OPTIONS * options)
{
   U32 index;

   detect_cpu_family(host);

   host->options = options;

   host->signature = NAMESEG("HOST");
   host->skt_info.signature = NAMESEG("SKTS");
   host->smp_exit_state.signature = NAMESEG("SMPE");

   host->pstates_enabled = 0;
   host->turbo_enabled = 0;
   host->cstates_enabled = 0;
   host->tstates_enabled = 0;

   options->exit_state.error_code_count = 0;
   options->exit_state.ssdt_mem_addr = 0;
   options->exit_state.ssdt_mem_size = 0;

   host->smp_exit_state.error_code_count = 0;
   for(index=0; index<(sizeof(host->smp_exit_state.error_codes)/sizeof(host->smp_exit_state.error_codes[0])); index++)
      host->smp_exit_state.error_codes[index]=0;


   host->skt_info.socket_count = 0;

   for (index=0; index < MAX_CPU_SOCKETS; index++)
   {
      CPU_DETAILS * cpu = &host->skt_info.cpu[index];

      cpu->present=0;
      cpu->logical_processor_count_from_madt=0;
      {
         U32 core_index;
         for (core_index = 0; core_index < MAX_CORES; core_index++)
            cpu->core_logical_processor_count_from_madt[core_index]=0;
      }
   }

   return(1);
}

//-----------------------------------------------------------------------------
static void shutdown(PPM_HOST * host, EXIT_CODE error_code, U32 return_status)
{
   set_error_code(host, error_code);
   set_exit_state(host, return_status);
}

//-----------------------------------------------------------------------------
U32 collect_socket_info(PPM_HOST * host)
{

   // Collect CPU info for all cpu sockets
   run_on_all_cpu(find_socket_proxy, host);

   if (host->smp_exit_state.error_code_count != 0)
      // Error from SMP operations
      return(0);
   else
      return(1);
}

//-----------------------------------------------------------------------------
U32 determine_configuration(PPM_HOST * host)
{
   // Compare collected data vs. options to determine consistent configuration

   // Assume all states as requested by input options
   host->pstates_enabled = host->options->pstates_enabled;
   if ((host->options->pstate_coordination <= ACPI_COORD_TYPE_HW_ALL) &&
       (host->options->pstate_coordination >= ACPI_COORD_TYPE_SW_ALL))
      host->pstate_coordination = host->options->pstate_coordination;
   else
      host->pstate_coordination = ACPI_COORD_TYPE_HW_ALL;
   host->turbo_enabled = host->options->turbo_enabled;
   host->cstates_enabled = host->options->cstates_enabled;
   host->tstates_enabled = host->options->tstates_enabled;
   host->performance_per_watt = host->options->performance_per_watt;

   // Verify this data
   {
      U32   socket_id = 0;
      for (socket_id = 0; socket_id < MAX_CPU_SOCKETS; socket_id ++)
      {
         CPU_DETAILS * cpu = &host->skt_info.cpu[socket_id];

         if (cpu->present)
         {
            host->pstates_enabled &= cpu->eist_cpuid_feature_flag;
            host->turbo_enabled &= cpu->turbo_available;
            host->tstates_enabled &= cpu->acpi_support_cpuid_feature_flag;

            // if any cpu does not support the energy_perf_bias msr
            // then force performance/watt to "traditional" for all cpu
            // otherwise use the originally requested performance/watt setting
            if (!cpu->energy_perf_bias_supported)
               host->performance_per_watt = 0;
         }
      }
   }

   return(1);
}

//-----------------------------------------------------------------------------
U32 ProcessMadtInfo (PPM_HOST * host)
{
   // Quick sanity check for MADT_INFO data to process
   if (host->madt_info.lapic_count == 0)
   {
      set_error_code(host, EXIT_CODE_FAILED_NO_LAPIC_FOUND_IN_MADT);
      return(0);
   }
   {
      U32 lapic_index;
      for (lapic_index = 0; lapic_index < host->madt_info.lapic_count; lapic_index++)
      {
         LAPIC_INFO * lapic = &host->madt_info.lapic[lapic_index];

         // Find the package index
         if ( FindPkgIndex(host, lapic->apicId, &lapic->pkg_index ) == 0)
         {
            set_error_code(host, EXIT_CODE_FAILED_FIND_PKG_INDEX_FROM_LAPIC);
            return (0);
         }

         {
            CPU_DETAILS * cpu = &host->skt_info.cpu[lapic->pkg_index];

            lapic->core_apic_id = lapic->apicId & (~cpu->smt_select_mask);
            lapic->core_index = (lapic->apicId & cpu->core_select_mask) >> cpu->smt_mask_width;

            if (lapic->core_index >= MAX_CORES)
            {
               set_error_code(host, EXIT_CODE_MAX_CORES_EXCEEDED);
               return (0);
            }
            cpu->logical_processor_count_from_madt++;
            cpu->core_logical_processor_count_from_madt[lapic->core_index]++;
         }

         // Find the ACPI NameSeg for the CPU Scope
         {
            // if MADT structure type = 0
            // then use processor ID
            // else MADT structure type = 9 so use UID instead
            U32 id = lapic->madt_type == 0? lapic->processorId : lapic->uid;
            if ( FindNamePath(host, id, &lapic->namepath) == 0 )
            {
               set_error_code(host, EXIT_CODE_FAILED_FIND_CPU_SCOPE_NAME_SEG);
               return (0);
            }
         }
      }
   }

   return (1);
}

//-----------------------------------------------------------------------------
static U32 computePstateRatio(const U32 max, const U32 min, const U32 turboEnabled, const U32 numStates, const U32 pstate)
{
  U32 ratiorange = max-min;
  U32 numGaps = numStates-1-turboEnabled;
  U32 adjPstate = pstate-turboEnabled;
  return (pstate == 0)     ? (max + turboEnabled) :
         (ratiorange == 0) ? max                  :
         max-(((adjPstate*ratiorange)+(numGaps/2))/numGaps);
}

//-----------------------------------------------------------------------------
static U32 computeNumPstates(const U32 max, const U32 min, const U32 turboEnabled, const U32 pssLimit)
{
  U32 ratiorange, maxStates, numStates;

  ratiorange = max - min + 1;
  maxStates = ratiorange + (turboEnabled ? 1 : 0);
  numStates = (pssLimit < maxStates) ? pssLimit : maxStates;
  return (numStates < 2) ? 0 : numStates;
}

//-----------------------------------------------------------------------------
U32 BuildPstateInfo(PPM_HOST * host)
{
   // Build P-state table info based on verified options
   U32   socket_id = 0;

   for (socket_id = 0; socket_id < MAX_CPU_SOCKETS; socket_id ++)
   {
      if (host->skt_info.cpu[socket_id].present)
      {
         CPU_DETAILS * cpu = &host->skt_info.cpu[socket_id];

         // Compute the number of p-states based on the ratio range
         cpu->pkg_pstates.num_pstates = computeNumPstates(cpu->max_ratio_as_cfg, cpu->min_ratio, host->turbo_enabled, 16);

         if (!cpu->pkg_pstates.num_pstates)
         {
            host->pstates_enabled = 0;
            return 1;
         }

         // Compute pstate data
         {
            U32 TDP = compute_tdp(host, cpu);

            U32 index;
            for (index=0; index < cpu->pkg_pstates.num_pstates; index ++)
            {
               PSTATE * pstate = &cpu->pkg_pstates.pstate[index];

               // Set ratio
               pstate->ratio = computePstateRatio(cpu->max_ratio_as_cfg, cpu->min_ratio, host->turbo_enabled, cpu->pkg_pstates.num_pstates, index);

               // Compute frequency based on ratio
               if ((index != 0) || (host->turbo_enabled == 0))
                  pstate->frequency = pstate->ratio * get_bclk(host);
               else
                  pstate->frequency = ((pstate->ratio - 1) * get_bclk(host)) + 1;

               // Compute power based on ratio and other data
               if (pstate->ratio >= cpu->max_ratio_as_mfg)
                  // Use max power in mW
                  pstate->power = TDP * 1000;
               else
               {
                  pstate->power = compute_pstate_power(host, cpu, pstate->ratio, TDP);

                  // Convert to mW
                  pstate->power*= 1000;
               }
            }
         }
      }
   }
   return (1);
}

//-----------------------------------------------------------------------------
static U32 BuildCstateInfo(PPM_HOST * host)
{
   static const ACPI_GENERIC_ADDRESS mwait_gas[] = {
      {GAS_TYPE_FFH,1,2,1,0x00},   // processor C1
      {GAS_TYPE_FFH,1,2,1,0x10},   // processor C3 as ACPI C2
      {GAS_TYPE_FFH,1,2,1,0x10},   // processor C3 as ACPI C3
      {GAS_TYPE_FFH,1,2,1,0x20},   // processor C6
      {GAS_TYPE_FFH,1,2,1,0x30},   // processor C7
   };

   static const ACPI_GENERIC_ADDRESS io_gas[] = {
      {GAS_TYPE_FFH,      0,0,0,0x00},   // processor C1
      {GAS_TYPE_SYSTEM_IO,8,0,0,0x14},   // processor C3 as ACPI C2
      {GAS_TYPE_SYSTEM_IO,8,0,0,0x14},   // processor C3 as ACPI C3
      {GAS_TYPE_SYSTEM_IO,8,0,0,0x15},   // processor C6
      {GAS_TYPE_SYSTEM_IO,8,0,0,0x16},   // processor C7
   };

   static const CSTATE mwait_cstate [] = {
      {1,0x01,0x3e8},      // processor C1
      {2,0x40,0x1f4},      // processor C3 as ACPI C2
      {3,0x40,0x1f4},      // processor C3 as ACPI C3
      {3,0x60,0x15e},      // processor C6
      {3,0x60,0x0c8},      // processor C7
   };

   static const CSTATE io_cstate [] = {
      {1,0x01,0x3e8},      // processor C1
      {2,0x40,0x1f4},      // processor C3 as ACPI C2
      {3,0x40,0x1f4},      // processor C3 as ACPI C3
      {3,0x60,0x15e},      // processor C6
      {3,0x60,0x0c8},      // processor C7
   };

   static const U32 cstate_2_index [] = {0,0,0,1,2,0,3,4};

   // Build C-state table info based on verified options
   U32   socket_id = 0;

   for (socket_id = 0; socket_id < MAX_CPU_SOCKETS; socket_id ++)
   {
      if (host->skt_info.cpu[socket_id].present)
      {
         CPU_DETAILS * cpu = &host->skt_info.cpu[socket_id];

         cpu->pkg_mwait_cstates.num_cstates = 0;
         {
            {
               cpu->pkg_mwait_cstates.cstate[cpu->pkg_mwait_cstates.num_cstates] = mwait_cstate[cstate_2_index[CPU_C1]];
               cpu->pkg_mwait_cstates.gas[cpu->pkg_mwait_cstates.num_cstates] = mwait_gas[cstate_2_index[CPU_C1]];
               cpu->pkg_mwait_cstates.num_cstates++;
            }
            if (cpu->core_c3_supported && ((host->options->c3_enabled == 2) ||
               ((host->options->c3_enabled == 4) && cpu->invariant_apic_timer_flag)))
            {
               cpu->pkg_mwait_cstates.cstate[cpu->pkg_mwait_cstates.num_cstates] = mwait_cstate[cstate_2_index[CPU_C3_ACPI_C2]];
               cpu->pkg_mwait_cstates.gas[cpu->pkg_mwait_cstates.num_cstates] = mwait_gas[cstate_2_index[CPU_C3_ACPI_C2]];
               cpu->pkg_mwait_cstates.num_cstates++;
            }
            if (cpu->core_c3_supported && ((host->options->c3_enabled == 3) ||
               ((host->options->c3_enabled == 4) && !cpu->invariant_apic_timer_flag)))
            {
               cpu->pkg_mwait_cstates.cstate[cpu->pkg_mwait_cstates.num_cstates] = mwait_cstate[cstate_2_index[CPU_C3_ACPI_C3]];
               cpu->pkg_mwait_cstates.gas[cpu->pkg_mwait_cstates.num_cstates] = mwait_gas[cstate_2_index[CPU_C3_ACPI_C3]];
               cpu->pkg_mwait_cstates.num_cstates++;
            }
            if (cpu->core_c6_supported && host->options->c6_enabled)
            {
               cpu->pkg_mwait_cstates.cstate[cpu->pkg_mwait_cstates.num_cstates] = mwait_cstate[cstate_2_index[CPU_C6]];
               cpu->pkg_mwait_cstates.gas[cpu->pkg_mwait_cstates.num_cstates] = mwait_gas[cstate_2_index[CPU_C6]];
               cpu->pkg_mwait_cstates.num_cstates++;
            }
            if (cpu->core_c7_supported && host->options->c7_enabled)
            {
               cpu->pkg_mwait_cstates.cstate[cpu->pkg_mwait_cstates.num_cstates] = mwait_cstate[cstate_2_index[CPU_C7]];
               cpu->pkg_mwait_cstates.gas[cpu->pkg_mwait_cstates.num_cstates] = mwait_gas[cstate_2_index[CPU_C7]];
               cpu->pkg_mwait_cstates.num_cstates++;
            }
         }

         cpu->pkg_io_cstates.num_cstates = 0;
         {
            {
               cpu->pkg_io_cstates.cstate[cpu->pkg_io_cstates.num_cstates] = io_cstate[cstate_2_index[CPU_C1]];
               cpu->pkg_io_cstates.gas[cpu->pkg_io_cstates.num_cstates] = io_gas[cstate_2_index[CPU_C1]];
               cpu->pkg_io_cstates.num_cstates++;
            }
            if (cpu->core_c3_supported && ((host->options->c3_enabled == 2) ||
               ((host->options->c3_enabled == 4) && cpu->invariant_apic_timer_flag)))
            {
               cpu->pkg_io_cstates.cstate[cpu->pkg_io_cstates.num_cstates] = io_cstate[cstate_2_index[CPU_C3_ACPI_C2]];
               cpu->pkg_io_cstates.gas[cpu->pkg_io_cstates.num_cstates] = io_gas[cstate_2_index[CPU_C3_ACPI_C2]];
               cpu->pkg_io_cstates.gas[cpu->pkg_io_cstates.num_cstates].Address += host->options->pmbase;
               cpu->pkg_io_cstates.num_cstates++;
            }
            if (cpu->core_c3_supported && ((host->options->c3_enabled == 3) ||
               ((host->options->c3_enabled == 4) && !cpu->invariant_apic_timer_flag)))
            {
               cpu->pkg_io_cstates.cstate[cpu->pkg_io_cstates.num_cstates] = io_cstate[cstate_2_index[CPU_C3_ACPI_C3]];
               cpu->pkg_io_cstates.gas[cpu->pkg_io_cstates.num_cstates] = io_gas[cstate_2_index[CPU_C3_ACPI_C3]];
               cpu->pkg_io_cstates.gas[cpu->pkg_io_cstates.num_cstates].Address += host->options->pmbase;
               cpu->pkg_io_cstates.num_cstates++;
            }
            if (cpu->core_c6_supported && host->options->c6_enabled)
            {
               cpu->pkg_io_cstates.cstate[cpu->pkg_io_cstates.num_cstates] = io_cstate[cstate_2_index[CPU_C6]];
               cpu->pkg_io_cstates.gas[cpu->pkg_io_cstates.num_cstates] = io_gas[cstate_2_index[CPU_C6]];
               cpu->pkg_io_cstates.gas[cpu->pkg_io_cstates.num_cstates].Address += host->options->pmbase;
               cpu->pkg_io_cstates.num_cstates++;
            }
            if (cpu->core_c7_supported && host->options->c7_enabled)
            {
               cpu->pkg_io_cstates.cstate[cpu->pkg_io_cstates.num_cstates] = io_cstate[cstate_2_index[CPU_C7]];
               cpu->pkg_io_cstates.gas[cpu->pkg_io_cstates.num_cstates] = io_gas[cstate_2_index[CPU_C7]];
               cpu->pkg_io_cstates.gas[cpu->pkg_io_cstates.num_cstates].Address += host->options->pmbase;
               cpu->pkg_io_cstates.num_cstates++;
            }
         }
      }
   }
   return (1);
}

//-----------------------------------------------------------------------------
static U32 BuildTstateInfo(PPM_HOST * host)
{
   // Coarse grained clock modulation is available if cpuid.6.eax[5] = 0
   // Max of 8 T-states using 12.5% increments
   static const TSTATE tstate_coarse_grain [] = {
      {100,0,0,0x00,0},
      { 88,0,0,0x1e,0},
      { 75,0,0,0x1c,0},
      { 63,0,0,0x1a,0},
      { 50,0,0,0x18,0},
      { 38,0,0,0x16,0},
      { 25,0,0,0x14,0},
      { 13,0,0,0x12,0},
   };

   // Fine grained clock modulation is available if cpuid.6.eax[5] = 1
   // Max of 15 T-states using 6.25% increments
   static const TSTATE tstate_fine_grain [] = {
      {100,0,0,0x00,0},
      { 94,0,0,0x1f,0},
      { 88,0,0,0x1e,0},
      { 81,0,0,0x1d,0},
      { 75,0,0,0x1c,0},
      { 69,0,0,0x1b,0},
      { 63,0,0,0x1a,0},
      { 56,0,0,0x19,0},
      { 50,0,0,0x18,0},
      { 44,0,0,0x17,0},
      { 38,0,0,0x16,0},
      { 31,0,0,0x15,0},
      { 25,0,0,0x14,0},
      { 19,0,0,0x13,0},
      { 13,0,0,0x12,0},
   };

   // Build T-state table info based on verified options
   U32 socket_id;
   const TSTATE * tstate;
   U32 num_tstates;

   for (socket_id = 0; socket_id < MAX_CPU_SOCKETS; socket_id ++)
   {
      if (host->skt_info.cpu[socket_id].present)
      {
         CPU_DETAILS * cpu = &host->skt_info.cpu[socket_id];

         // Check if fine or coarse grained clock modulation is available
         if (cpu->cpuid6._eax & (1UL << 5))
         {
            // Fine grain thermal throttling is available
            num_tstates = 15;
            tstate = tstate_fine_grain;
         }
         else
         {
            // Coarse grain thermal throttling is available
            num_tstates = 8;
            tstate = tstate_coarse_grain;
         }

         cpu->pkg_tstates.num_tstates = num_tstates;
         {
            U32 index;
            for (index = 0; index < num_tstates; index++)
            {
               cpu->pkg_tstates.tstate[index] = tstate[index];
               cpu->pkg_tstates.tstate[index].power = 1000 * (compute_tdp(host, cpu) * (num_tstates - index)) / num_tstates;
            }
         }
      }
   }
   return (1);
}

//-----------------------------------------------------------------------------
U32 BuildSsdt(PPM_HOST * host, void * buffer, U32 bufferSize)
{
   // Build SSDT
   {
      // (1) Setup pointers to SSDT memory location
      // (2) Create SSDT Definition Block
      //    (2.1) Save pointer to SSDT package length and checksum fields
      //    (2.2) Create variables in SSDT scope
      // (3) For each logical processor CPUn in the MADT
      //    (3.1) Create scope for CPUn
      //    (3.2) Create variables in CPU scope
      //    (3.3) Create _OSC and/or _PDC Methods
      //    (3.4) Create P-state related structures
      //       (3.4.1) Create _PSS Method
      //       (3.4.2) Create _PCT Object
      //       (3.4.3) Create _PPC Method
      //       (3.4.4) Create _PSD Object
      //    (3.5) Create C-state related structures
      //       (3.5.1) Create _CST Method
      //       (3.5.2) Create _CSD Method
      //    (3.6) Create T-state related structures
      //       (3.6.1) Create _TPC Method
      //       (3.6.2) Create _PTC Method
      //       (3.6.3) Create _TSS Method
      //       (3.6.4) Create _TSD Method
      //    (3.7) Update length in CPUn Scope
      // (4) Update length and checksum in SSDT Definition Block

      // (1) Setup pointers to SSDT memory location
      void * current = buffer;
      void * end = (U8 *)buffer + bufferSize;

      // Confirm a valid SSDT buffer was provided
      if (!buffer)
      {
         set_error_code(host, EXIT_CODE_INVALID_SSDT_ADDR);
         return(0);
      }

      // Confirm a valid SSDT buffer length was provided
      if (!bufferSize)
      {
         set_error_code(host, EXIT_CODE_INVALID_SSDT_LEN);
         return(0);
      }

      host->acpi_tables.SsdtPointer = (ACPI_TABLE_SSDT *)buffer;

      // (2) Create SSDT Definition Block
      // (2.1) Save pointer to SSDT package length and checksum fields
      current = buildTableHeader(current, NAMESEG("SSDT"), NAMESEG64("PPM RCM "));

      // Check to confirm no SSDT buffer overflow
      if ( (U8 *)current > (U8 *)end )
      {
         set_error_code(host, EXIT_CODE_FAILED_SSDT_SIZE_EXCEEDED);
         return(0);
      }

      // (3) For each logical processor CPUn in the MADT
      {
         U32 lapic_index;
         for (lapic_index=0; lapic_index < host->madt_info.lapic_count; lapic_index++)
         {
            // (3.1) Create scope for CPUn
            ACPI_SCOPE * scope = current;

            {
               U32 cpu_namespace = host->options->cpu_namespace_flag ? NAMESEG("_SB_") : NAMESEG("_PR_");
               PROCESSOR_NUMBER_TO_NAMESEG * namepath = host->madt_info.lapic[lapic_index].namepath;
               current = buildCpuScope (current, cpu_namespace, namepath );
            }

            // Check to confirm no SSDT buffer overflow
            if ( (U8 *)current > (U8 *)end )
            {
               set_error_code(host, EXIT_CODE_FAILED_SSDT_SIZE_EXCEEDED);
               return(0);
            }

            // (3.2) Create variables in CPU scope

            // Build Type variable used to store PDC capabilities
            current = buildNamedDword(current, NAMESEG("TYPE"), 0);

            // Build PSEN variable used to store state of P-State Enable setup option
            current = buildNamedDword(current, NAMESEG("PSEN"), host->pstates_enabled);

            // Build CSEN variable used to store state of C-State Enable setup option
            current = buildNamedDword(current, NAMESEG("CSEN"), host->cstates_enabled);

            // Build MWOS variable used to store state of MWAIT OS setup option
            current = buildNamedDword(current, NAMESEG("MWOS"), host->options->mwait_enabled);

            // Build TSEN variable used to store state of T-State Enable setup option
            current = buildNamedDword(current, NAMESEG("TSEN"), host->options->tstates_enabled);

            // (3.3) Create _OSC and/or _PDC Methods
            {
               // Build _PDC method
               current = buildPDC(current);

               // Check to confirm no SSDT buffer overflow
               if ( (U8 *)current > (U8 *)end )
               {
                  set_error_code(host, EXIT_CODE_FAILED_SSDT_SIZE_EXCEEDED);
                  return(0);
               }

               // Build _OSC method
               current = buildOSC(current);

               // Check to confirm no SSDT buffer overflow
               if ( (U8 *)current > (U8 *)end )
               {
                  set_error_code(host, EXIT_CODE_FAILED_SSDT_SIZE_EXCEEDED);
                  return(0);
               }
            }

            // (3.4) Create P-state related structures
            if (host->pstates_enabled == 1)
            {
               // (3.4.1) Create _PSS Method
               {
                  U32 pkgIndex = host->madt_info.lapic[lapic_index].pkg_index;
                  PKG_PSTATES * pkg_pstates = &host->skt_info.cpu[pkgIndex].pkg_pstates;
                  current = buildPSS(host, current, pkg_pstates);
               }

               // Check to confirm no SSDT buffer overflow
               if ( (U8 *)(current) > (U8 *)end )
               {
                  set_error_code(host, EXIT_CODE_FAILED_SSDT_SIZE_EXCEEDED);
                  return(0);
               }

               // (3.4.2) Create _PCT Object
               current = buildPCT(current);

               // Check to confirm no SSDT buffer overflow
               if ( (U8 *)(current) > (U8 *)end )
               {
                  set_error_code(host, EXIT_CODE_FAILED_SSDT_SIZE_EXCEEDED);
                  return(0);
               }

               // (3.4.3) Create _PPC Method
               current = buildPPC(current);

               // Check to confirm no SSDT buffer overflow
               if ( (U8 *)(current) > (U8 *)end )
               {
                  set_error_code(host, EXIT_CODE_FAILED_SSDT_SIZE_EXCEEDED);
                  return(0);
               }

               // (3.4.4) Create PSD with hardware coordination
               {
                  U32 domain = host->madt_info.lapic[lapic_index].pkg_index;
                  U32 cpusInDomain = host->skt_info.cpu[domain].logical_processor_count_from_madt;
                  current = buildPSD(current, domain, cpusInDomain, host->options->pstate_coordination);
               }

               // Check to confirm no SSDT buffer overflow
               if ( (U8 *)(current) > (U8 *)end )
               {
                  set_error_code(host, EXIT_CODE_FAILED_SSDT_SIZE_EXCEEDED);
                  return(0);
               }
            }

            // (3.5) Create C-state related structures
            if (host->cstates_enabled == 1)
            {
               {
                  LAPIC_INFO * lapic = &host->madt_info.lapic[lapic_index];
                  CPU_DETAILS * cpu = &host->skt_info.cpu[lapic->pkg_index];
                  PKG_CSTATES * mwait_pkg_cstates = &cpu->pkg_mwait_cstates;
                  PKG_CSTATES * io_pkg_cstates = &cpu->pkg_io_cstates;

                  // Build CST
                  current = buildCST(current, mwait_pkg_cstates, io_pkg_cstates);

#ifdef BUILD_ACPI_CSD
                  {
                     // Use core_apic_id as domain
                     U32 domain = lapic->core_apic_id;

                     // Use cpu->core_logical_processor_count_from_madt[lapic->core_index] as count for that domain
                     U32 cpusInDomain = cpu->core_logical_processor_count_from_madt[lapic->core_index];

                     // Create CSD
                     current = buildCSD(current, domain, cpusInDomain, io_pkg_cstates);
                  }
#endif
               }

               // Check to confirm no SSDT buffer overflow
               if ( (U8 *)(current) > (U8 *)end )
               {
                  set_error_code(host, EXIT_CODE_FAILED_SSDT_SIZE_EXCEEDED);
                  return(0);
               }
            }

            // (3.6) Create T-state related structures
            if (host->tstates_enabled == 1)
            {
               // (3.6.1) Create _TPC Method
               current = buildTPC(current);

               // (3.6.2) Create _PTC Method
               current = buildPTC(current);

               // (3.6.3) Create _TSS Method
               {
                  U32 pkgIndex = host->madt_info.lapic[lapic_index].pkg_index;
                  PKG_TSTATES * pkg_tstates = &host->skt_info.cpu[pkgIndex].pkg_tstates;
                  current = buildTSS(current, pkg_tstates);
               }

               // (3.6.4) Create _TSD Method
               {
                  LAPIC_INFO * lapic = &host->madt_info.lapic[lapic_index];
                  CPU_DETAILS * cpu = &host->skt_info.cpu[lapic->pkg_index];

                  // Use core_apic_id as domain
                  U32 domain = lapic->core_apic_id;
                  // Use cpu->core_logical_processor_count_from_madt[lapic->core_index] as count for that domain
                  U32 cpusInDomain = cpu->core_logical_processor_count_from_madt[lapic->core_index];

                  current = buildTSD(current, domain, cpusInDomain);
               }
            }

            // (3.7) Update length in CPUn Scope
            setPackageLength(&scope->pkgLength, (U8 *)current - (U8 *)&scope->pkgLength);

         } // End for

         // (4) Update length and checksum in SSDT Definition Block
         {
            host->acpi_tables.SsdtPointer->Header.Length = (U8 *)current - (U8 *)host->acpi_tables.SsdtPointer;
            host->acpi_tables.SsdtPointer->Header.Checksum = 0;
            host->acpi_tables.SsdtPointer->Header.Checksum = 0 -
                                                            GetChecksum(host->acpi_tables.SsdtPointer,
                                                                        host->acpi_tables.SsdtPointer->Header.Length);
         }

         // Check to confirm no SSDT buffer overflow
         if ( (U8 *)current > (U8 *)end )
         {
            set_error_code(host, EXIT_CODE_FAILED_SSDT_SIZE_EXCEEDED);
            return(0);
         }

      } // End build SSDT

   } // SSDT

   return(1);
}

//-----------------------------------------------------------------------------
static void * buildReturnPackageCST(void * current, PKG_CSTATES * pkg_cstates)
{
   // Create package returning C-states
   ACPI_RETURN_PACKAGE * returnPkg = current;
   current = buildReturnPackage(current, (U8)pkg_cstates->num_cstates + 1);

   {
      // Include number of C-states
      current = buildByteConst(current, (U8)pkg_cstates->num_cstates);

      {
         U32 cstateIndex = 0;
         for (cstateIndex=0; cstateIndex < pkg_cstates->num_cstates; cstateIndex++)
            // Build C-state
            current = buildCstate(current, &pkg_cstates->gas[cstateIndex], &pkg_cstates->cstate[cstateIndex]);
      }
   }

   // Update package length in return package
   setPackageLength(&returnPkg->package.pkgLength,
                     (U8 *)current - (U8 *)&returnPkg->package.pkgLength);

   return(current);
}

//-----------------------------------------------------------------------------
static void * buildCST(void * current, PKG_CSTATES * mwait_pkg_cstates, PKG_CSTATES * io_pkg_cstates)
{
   //
   // IF (CSEN)
   // {
   //    IF (LAnd(MWOS, And(TYPE, 0x200)))
   //    {
   //       Return package containing MWAIT C-states
   //    }
   //    Return package containing IO C-states
   // }
   // Return(Zero)
   //
   ACPI_METHOD * cst = current;
   current = buildMethod(current, NAMESEG("_CST"), 0);
   {
      // "IF" CSEN -- IF Opcode
      current = buildOpCode(current, AML_IF_OP);
      {
         ACPI_PACKAGE_LENGTH * packageLength1 = current;
         current = buildPackageLength(current, 0);

         // IF "(CSEN)" -- IF Predicate
         current = buildNameSeg(current, NAMESEG("CSEN"));

         // "IF" (LAnd(MWOS, And(TYPE, 0x200))) -- IF Opcode
         current = buildOpCode(current, AML_IF_OP);
         {
            ACPI_PACKAGE_LENGTH * packageLength2 = current;
            current = buildPackageLength(current, 0);

            // IF ("LAnd"(MWOS, And(TYPE, 0x200))) -- LAND Opcode
            current = buildOpCode(current, AML_LAND_OP);

            // IF (LAnd("MWOS", And(TYPE, 0x200))) -- MWOS Term
            current = buildNameSeg(current, NAMESEG("MWOS"));

            // IF (LAnd(MWOS, "And"(TYPE, 0x200))) -- AND Opcode
            current = buildOpCode(current, AML_AND_OP);

            // IF (LAnd(MWOS, And("TYPE", 0x200))) -- TYPE Term
            current = buildNameSeg(current, NAMESEG("TYPE"));

            // IF (LAnd(MWOS, And(TYPE, "0x200"))) -- DWORD Value Term
            current = buildWordConst(current, 0x200);

            // IF (LAnd(MWOS, "And(TYPE, 0x200)")) -- Target for And term (unused)
            current = buildOpCode(current, AML_ZERO_OP);

            // Build return package for mwait c-states
            current = buildReturnPackageCST(current, mwait_pkg_cstates);

            setPackageLength(packageLength2,
                             (U8 *)current - (U8 *)packageLength2);
         }

         // Build return package for io c-states
         current = buildReturnPackageCST(current, io_pkg_cstates);

         setPackageLength(packageLength1,
                          (U8 *)current - (U8 *)packageLength1);
      }
      // "Return (ZERO)"
      current = buildReturnZero(current);
   }
   // Update length in _CST method
   setPackageLength(&cst->pkgLength, (U8 *)current - (U8 *)&cst->pkgLength);

   return(current);
}

//-----------------------------------------------------------------------------
static void * buildPDC(void * current)
{
   ACPI_METHOD * pdc = current;
   current = buildMethod(current, NAMESEG("_PDC"), 1);

   // CreateDWordField (Arg0, 0x08, CAPA)
   current = buildOpCode(current, AML_CREATE_DWORD_FIELD_OP);
   current = buildOpCode(current, AML_ARG0_OP);
   current = buildByteConst(current, 0x08);
   current = buildNameSeg(current, NAMESEG("CAPA"));

   // Store (CAPA, TYPE)
   current = buildOpCode(current, AML_STORE_OP);
   current = buildNameSeg(current, NAMESEG("CAPA"));
   current = buildNameSeg(current, NAMESEG("TYPE"));

   // CreateDWordField (Arg0, 0x00, REVS)
   current = buildOpCode(current, AML_CREATE_DWORD_FIELD_OP);
   current = buildOpCode(current, AML_ARG0_OP);
   current = buildByteConst(current, 0x00);
   current = buildNameSeg(current, NAMESEG("REVS"));

   // CreateDWordField (Arg0, 0x04, SIZE)
   current = buildOpCode(current, AML_CREATE_DWORD_FIELD_OP);
   current = buildOpCode(current, AML_ARG0_OP);
   current = buildByteConst(current, 0x04);
   current = buildNameSeg(current, NAMESEG("SIZE"));

   // Store(SizeOf(Arg0), Local0)
   current = buildOpCode(current, AML_STORE_OP);
   current = buildOpCode(current, AML_SIZEOF_OP);
   current = buildOpCode(current, AML_ARG0_OP);
   current = buildOpCode(current, AML_LOCAL0_OP);

   // Store(Subtract(Local0, 0x08),Local1)
   current = buildOpCode(current, AML_STORE_OP);
   current = buildOpCode(current, AML_SUBTRACT_OP);
   current = buildOpCode(current, AML_LOCAL0_OP);
   current = buildByteConst(current, 0x08);
   current = buildOpCode(current, AML_ZERO_OP);
   current = buildOpCode(current, AML_LOCAL1_OP);

   // CreateField (Arg0, 0x40, Multiply (Local1, 0x08), TEMP)
   current = buildOpCode(current, AML_EXT_OP_PREFIX);
   current = buildOpCode(current, AML_CREATE_FIELD_OP);
   current = buildOpCode(current, AML_ARG0_OP);
   current = buildByteConst(current, 0x40);
   current = buildOpCode(current, AML_MULTIPLY_OP);
   current = buildOpCode(current, AML_LOCAL1_OP);
   current = buildByteConst(current, 0x08);
   current = buildOpCode(current, AML_ZERO_OP);
   current = buildNameSeg(current, NAMESEG("TEMP"));

   // Name (STS0, Buffer (0x04) {0x00, 0x00, 0x00, 0x00})
   // Create STS0 as named buffer
   current = buildNamePath(current, NAMESEG("STS0"));
   {
      ACPI_SMALL_BUFFER * buff = current;
      current = buildSmallBuffer(current);

      // count of buffer elements
      current = buildByteConst(current, 4);

      current = buildOpCode(current, AML_ZERO_OP);
      current = buildOpCode(current, AML_ZERO_OP);
      current = buildOpCode(current, AML_ZERO_OP);
      current = buildOpCode(current, AML_ZERO_OP);
      {
         U32 length = (U8 *)current - (U8 *)buff;
         buff->packageLength = (U8)length - 1;
      }
   }

   //Concatenate (STS0, TEMP, Local2)
   current = buildOpCode(current, AML_CONCAT_OP);
   current = buildNameSeg(current, NAMESEG("STS0"));
   current = buildNameSeg(current, NAMESEG("TEMP"));
   current = buildOpCode(current, AML_LOCAL2_OP);

   //_OSC (Buffer (0x10)
   //      {
   //         /* 0000 */    0x16, 0xA6, 0x77, 0x40, 0x0C, 0x29, 0xBE, 0x47,
   //         /* 0008 */    0x9E, 0xBD, 0xD8, 0x70, 0x58, 0x71, 0x39, 0x53
   //      }, REVS, SIZE, Local2)
   current = buildNameSeg(current, NAMESEG("_OSC"));
   {
      ACPI_SMALL_BUFFER * buff = current;
      current = buildSmallBuffer(current);

      // count of buffer elements
      current = buildByteConst(current, 0x10);

      current = buildOpCode(current, 0x16);
      current = buildOpCode(current, 0xa6);
      current = buildOpCode(current, 0x77);
      current = buildOpCode(current, 0x40);
      current = buildOpCode(current, 0x0c);
      current = buildOpCode(current, 0x29);
      current = buildOpCode(current, 0xbe);
      current = buildOpCode(current, 0x47);
      current = buildOpCode(current, 0x9e);
      current = buildOpCode(current, 0xbd);
      current = buildOpCode(current, 0xd8);
      current = buildOpCode(current, 0x70);
      current = buildOpCode(current, 0x58);
      current = buildOpCode(current, 0x71);
      current = buildOpCode(current, 0x39);
      current = buildOpCode(current, 0x53);
      {
         U32 length = (U8 *)current - (U8 *)buff;
         buff->packageLength = (U8)length - 1;
      }
   }
   current = buildNameSeg(current, NAMESEG("REVS"));
   current = buildNameSeg(current, NAMESEG("SIZE"));
   current = buildOpCode(current, AML_LOCAL2_OP);

   // Update package length in PDC object
   //pdc->packageLength = (U8)((U8 *)current - (U8 *)&pdc->packageLength);
   setPackageLength(&pdc->pkgLength, (U8 *)current - (U8 *)&pdc->pkgLength);

   return(current);
}

//-----------------------------------------------------------------------------
static void * buildOSC(void * current)
{
   //
   //
   ACPI_METHOD * osc = current;
   current = buildMethod(current, NAMESEG("_OSC"), 4);

   // CreateDWordField (Arg3, 0x04, CAPA)
   current = buildOpCode(current, AML_CREATE_DWORD_FIELD_OP);
   current = buildOpCode(current, AML_ARG3_OP);
   current = buildByteConst(current, 0x04);
   current = buildNameSeg(current, NAMESEG("CAPA"));

   // Store (CAPA, TYPE)
   current = buildOpCode(current, AML_STORE_OP);
   current = buildNameSeg(current, NAMESEG("CAPA"));
   current = buildNameSeg(current, NAMESEG("TYPE"));

   // CreateDWordField (Arg3, 0x00, STS0)
   current = buildOpCode(current, AML_CREATE_DWORD_FIELD_OP);
   current = buildOpCode(current, AML_ARG3_OP);
   current = buildByteConst(current, 0x00);
   current = buildNameSeg(current, NAMESEG("STS0"));

   // CreateDWordField (Arg3, 0x04, CAP0)
   current = buildOpCode(current, AML_CREATE_DWORD_FIELD_OP);
   current = buildOpCode(current, AML_ARG3_OP);
   current = buildByteConst(current, 0x04);
   current = buildNameSeg(current, NAMESEG("CAP0"));

   // CreateDWordField (Arg0, 0x00, IID0)
   current = buildOpCode(current, AML_CREATE_DWORD_FIELD_OP);
   current = buildOpCode(current, AML_ARG0_OP);
   current = buildByteConst(current, 0x00);
   current = buildNameSeg(current, NAMESEG("IID0"));

   // CreateDWordField (Arg0, 0x04, IID1)
   current = buildOpCode(current, AML_CREATE_DWORD_FIELD_OP);
   current = buildOpCode(current, AML_ARG0_OP);
   current = buildByteConst(current, 0x04);
   current = buildNameSeg(current, NAMESEG("IID1"));

   // CreateDWordField (Arg0, 0x08, IID2)
   current = buildOpCode(current, AML_CREATE_DWORD_FIELD_OP);
   current = buildOpCode(current, AML_ARG0_OP);
   current = buildByteConst(current, 0x08);
   current = buildNameSeg(current, NAMESEG("IID2"));

   // CreateDWordField (Arg0, 0x0C, IID3)
   current = buildOpCode(current, AML_CREATE_DWORD_FIELD_OP);
   current = buildOpCode(current, AML_ARG0_OP);
   current = buildByteConst(current, 0x0C);
   current = buildNameSeg(current, NAMESEG("IID3"));

   // Name (UID0, Buffer (0x10)
   // {
   //    0x16, 0xA6, 0x77, 0x40, 0x0C, 0x29, 0xBE, 0x47,
   //    0x9E, 0xBD, 0xD8, 0x70, 0x58, 0x71, 0x39, 0x53
   // })
   current = buildNamePath(current, NAMESEG("UID0"));
   {
      ACPI_SMALL_BUFFER * buff = current;
      current = buildSmallBuffer(current);

      // count of buffer elements
      current = buildByteConst(current, 0x10);

      current = buildOpCode(current, 0x16);
      current = buildOpCode(current, 0xa6);
      current = buildOpCode(current, 0x77);
      current = buildOpCode(current, 0x40);
      current = buildOpCode(current, 0x0c);
      current = buildOpCode(current, 0x29);
      current = buildOpCode(current, 0xbe);
      current = buildOpCode(current, 0x47);
      current = buildOpCode(current, 0x9e);
      current = buildOpCode(current, 0xbd);
      current = buildOpCode(current, 0xd8);
      current = buildOpCode(current, 0x70);
      current = buildOpCode(current, 0x58);
      current = buildOpCode(current, 0x71);
      current = buildOpCode(current, 0x39);
      current = buildOpCode(current, 0x53);

      {
         U32 length = (U8 *)current - (U8 *)buff;
         buff->packageLength = (U8)length - 1;
      }
   }

   // CreateDWordField (UID0, 0x00, EID0)
   current = buildOpCode(current, AML_CREATE_DWORD_FIELD_OP);
   current = buildOpCode(current, AML_ARG0_OP);
   current = buildByteConst(current, 0x00);
   current = buildNameSeg(current, NAMESEG("EID0"));

   // CreateDWordField (UID0, 0x04, EID1)
   current = buildOpCode(current, AML_CREATE_DWORD_FIELD_OP);
   current = buildOpCode(current, AML_ARG0_OP);
   current = buildByteConst(current, 0x04);
   current = buildNameSeg(current, NAMESEG("EID1"));

   // CreateDWordField (UID0, 0x08, EID2)
   current = buildOpCode(current, AML_CREATE_DWORD_FIELD_OP);
   current = buildOpCode(current, AML_ARG0_OP);
   current = buildByteConst(current, 0x08);
   current = buildNameSeg(current, NAMESEG("EID2"));

   // CreateDWordField (UID0, 0x0C, EID3)
   current = buildOpCode(current, AML_CREATE_DWORD_FIELD_OP);
   current = buildOpCode(current, AML_ARG0_OP);
   current = buildByteConst(current, 0x0C);
   current = buildNameSeg(current, NAMESEG("EID3"));

   // If (LNot (LAnd (LAnd (LEqual (IID0, EID0), LEqual (IID1, EID1)),
   //      LAnd (LEqual (IID2, EID2), LEqual (IID3, EID3)))))
   // {
   //      Store (0x06, Index (STS0, 0x00))
   //      Return (Arg3)
   // }
   {
      current = buildOpCode(current, AML_IF_OP);
      {
         ACPI_PACKAGE_LENGTH * packageLength = current;
         current = buildPackageLength(current, 0);

         current = buildOpCode(current, AML_LNOT_OP);
         current = buildOpCode(current, AML_LAND_OP);
         current = buildOpCode(current, AML_LAND_OP);
         current = buildOpCode(current, AML_LEQUAL_OP);
         current = buildNameSeg(current, NAMESEG("IID0"));
         current = buildNameSeg(current, NAMESEG("EID0"));

         current = buildOpCode(current, AML_LEQUAL_OP);
         current = buildNameSeg(current, NAMESEG("IID1"));
         current = buildNameSeg(current, NAMESEG("EID1"));

         current = buildOpCode(current, AML_LAND_OP);
         current = buildOpCode(current, AML_LEQUAL_OP);
         current = buildNameSeg(current, NAMESEG("IID2"));
         current = buildNameSeg(current, NAMESEG("EID2"));

         current = buildOpCode(current, AML_LEQUAL_OP);
         current = buildNameSeg(current, NAMESEG("IID3"));
         current = buildNameSeg(current, NAMESEG("EID3"));

         // Store (0x06, Index (STS0, 0x00))
         current = buildOpCode(current, AML_STORE_OP);
         current = buildByteConst(current, 0x06);
         current = buildOpCode(current, AML_INDEX_OP);
         current = buildNameSeg(current, NAMESEG("STS0"));
         current = buildByteConst(current, 0x00);
         current = buildOpCode(current, AML_ZERO_OP);

         // Return (Arg3)
         current = buildReturnOpcode(current, AML_ARG3_OP);

         setPackageLength(packageLength,
                           (U8 *)current - (U8 *)packageLength);
      }
   }

   // If (LNotEqual (Arg1, 0x01))
   // {
   //      Store (0x0A, Index (STS0, 0x00))
   //      Return (Arg3)
   // }
   {
      current = buildOpCode(current, AML_IF_OP);
      {
         ACPI_PACKAGE_LENGTH * packageLength = current;
         current = buildPackageLength(current, 0);

         // If ("LNotEqual (Arg1, 0x01)")
         current = buildOpCode(current, AML_LNOT_OP);
         current = buildOpCode(current, AML_LEQUAL_OP);
         current = buildOpCode(current, AML_ARG1_OP);
         current = buildByteConst(current, 0x01);

         // Store (0x0A, Index (STS0, 0x00))
         current = buildOpCode(current, AML_STORE_OP);
         current = buildByteConst(current, 0x0A);
         current = buildOpCode(current, AML_INDEX_OP);
         current = buildNameSeg(current, NAMESEG("STS0"));
         current = buildByteConst(current, 0x00);
         current = buildOpCode(current, AML_ZERO_OP);

         // Return (Arg3)
         current = buildReturnOpcode(current, AML_ARG3_OP);

         setPackageLength(packageLength,
                           (U8 *)current - (U8 *)packageLength);
      }
   }

   // If (And (STS0, 0x01))
   // {
   //    And (CAP0, 0x0BFF, CAP0)
   //    Return (Arg3)
   // }
   {
      current = buildOpCode(current, AML_IF_OP);
      {
         ACPI_PACKAGE_LENGTH * packageLength = current;
         current = buildPackageLength(current, 0);

         // If ("And (STS0, 0x01)")
         current = buildOpCode(current, AML_AND_OP);
         current = buildNameSeg(current, NAMESEG("STS0"));
         current = buildByteConst(current, 0x01);
         current = buildOpCode(current, AML_ZERO_OP);

         // And (CAP0, 0x0BFF, CAP0)
         current = buildOpCode(current, AML_AND_OP);
         current = buildNameSeg(current, NAMESEG("CAP0"));
         current = buildWordConst(current, 0x0BFF);
         current = buildNameSeg(current, NAMESEG("CAP0"));

         // Return (Arg3)
         current = buildReturnOpcode(current, AML_ARG3_OP);

         setPackageLength(packageLength,
                           (U8 *)current - (U8 *)packageLength);
      }
   }

   // And (CAP0, 0x0BFF, CAP0)
   current = buildOpCode(current, AML_AND_OP);
   current = buildNameSeg(current, NAMESEG("CAP0"));
   current = buildWordConst(current, 0x0BFF);
   current = buildNameSeg(current, NAMESEG("CAP0"));

   // Store (CAP0, TYPE)
   current = buildOpCode(current, AML_STORE_OP);
   current = buildNameSeg(current, NAMESEG("CAP0"));
   current = buildNameSeg(current, NAMESEG("TYPE"));

   // Return (Arg3)
   current = buildReturnOpcode(current, AML_ARG3_OP);

   // Set package length for the OSC object
   setPackageLength(&osc->pkgLength, (U8 *)current - (U8 *)&osc->pkgLength);

   return(current);
}

//-----------------------------------------------------------------------------
static void * buildPSS(PPM_HOST * host, void * current, PKG_PSTATES * pkg_pstates)
{
   //
   // IF (PSEN)
   // {
   //    Return (Package of Pstate Packages)
   // }
   // Return(Zero)
   //
   ACPI_METHOD * pss = current;
   current = buildMethod(current, NAMESEG("_PSS"), 0);

   {
      // "IF" (PSEN) -- IF Opcode
      current = buildOpCode(current, AML_IF_OP);
      {
         ACPI_PACKAGE_LENGTH * packageLength = current;
         current = buildPackageLength(current, 0);

         // IF "(PSEN)" -- IF Predicate
         current = buildNameSeg(current, NAMESEG("PSEN"));

         {
            ACPI_RETURN_PACKAGE * returnPkg = current;
            current = buildReturnPackage(current, (U8)pkg_pstates->num_pstates);

            // (3.3.3) For each P-state
            {
               U32 pstateIndex = 0;
               for (pstateIndex=0; pstateIndex < pkg_pstates->num_pstates; pstateIndex++)
               {
                  // (3.3.3.1) Create P-state package
                  ACPI_PSTATE_PACKAGE * pstate = current;
                  current = pstate + 1;

                  setSmallPackage(&pstate->package, 6);
                  pstate->package.packageLength = (U8)(sizeof(ACPI_PSTATE_PACKAGE) - 1);

                  setDwordConst(&pstate->CoreFreq, pkg_pstates->pstate[pstateIndex].frequency);
                  setDwordConst(&pstate->Power, pkg_pstates->pstate[pstateIndex].power);
                  setDwordConst(&pstate->TransLatency, 10);
                  setDwordConst(&pstate->BMLatency, 10);
                  setDwordConst(&pstate->Control, encode_pstate(host, pkg_pstates->pstate[pstateIndex].ratio));
                  setDwordConst(&pstate->Status, encode_pstate(host, pkg_pstates->pstate[pstateIndex].ratio));
               } // for
            } // for block

            // (3.3.4) Update package length in return package
            setPackageLength(&returnPkg->package.pkgLength, (U8 *)current - (U8 *)&returnPkg->package.pkgLength);
         }

         // "IF (PSEN) and its body" -- Set package length
         setPackageLength(packageLength,
                          (U8 *)current - (U8 *)packageLength);
      }
      // "Return (ZERO)"
      current = buildReturnZero(current);
   }
   // Set package length for the _PSS object
   setPackageLength(&pss->pkgLength, (U8 *)current - (U8 *)&pss->pkgLength);

   return(current);
}



//-----------------------------------------------------------------------------
static void * buildPSD(void * current, U32 domain, U32 cpusInDomain, U32 pstate_coordination)
{
   // If (And(TYPE, 0x0820))
   // {
   //    Return (PSD Package)
   // }
   // Return(Zero)

   ACPI_METHOD * psdMethod = current;
   current = buildMethod(current, NAMESEG("_PSD"), 0);
   {
      // "IF" (And(TYPE, 0x0820)) -- IF Opcode
      current = buildOpCode(current, AML_IF_OP);
      {
         ACPI_PACKAGE_LENGTH * packageLength = current;
         current = buildPackageLength(current, 0);

         // IF ("And"(TYPE, 0x820)) -- AND Opcode
         current = buildOpCode(current, AML_AND_OP);

         // IF (And("TYPE", 0x820)) -- TYPE Term
         current = buildNameSeg(current, NAMESEG("TYPE"));

         // IF (And(TYPE, "0x0820")) -- DWORD Value Term
         current = buildDwordConst(current, 0x820);

         // IF ("And(TYPE, 0x200)") -- Target for And term (unused)
         current = buildOpCode(current, AML_ZERO_OP);

         // Build return package containing PSD package
         {
            ACPI_RETURN_PACKAGE * returnPkg = current;
            current = buildReturnPackage(current, 1);

            {
               // Create PSD package
               ACPI_PSD_PACKAGE * psd = current;
               current = psd + 1;

               setSmallPackage(&psd->package, 5);
               psd->package.packageLength = (U8)(sizeof(ACPI_PSD_PACKAGE) - 1);

               setByteConst(&psd->NumberOfEntries, 5);
               setByteConst(&psd->Revision, 0);
               setDwordConst(&psd->Domain, domain);
               setDwordConst(&psd->CoordType, pstate_coordination);
               setDwordConst(&psd->NumProcessors, cpusInDomain);

            } // PSD package

            setPackageLength(&returnPkg->package.pkgLength,
                      (U8 *)current - (U8 *)&returnPkg->package.pkgLength);
         }
         setPackageLength(packageLength, (U8 *)current - (U8 *)packageLength);
      }
      // "Return (ZERO)"
      current = buildReturnZero(current);
   }
   // Update length in _PSD method
   setPackageLength(&psdMethod->pkgLength, (U8 *)current - (U8 *)&psdMethod->pkgLength);

   return(current);
}

//-----------------------------------------------------------------------------
static void * buildPPC(void * current)
{
   ACPI_SMALL_METHOD * ppc = current;
   current = buildSmallMethod(current, NAMESEG("_PPC"), 0);

   current = buildReturnZero(current);

   // Update package length in PPC object
   ppc->packageLength = (U8) ( (U8 *)current - (U8 *)&ppc->packageLength );

   return(current);
}

//-----------------------------------------------------------------------------
static void * buildPCT(void * current)
{
   static const ACPI_GENERIC_ADDRESS pct_gas[] = {
      {0x7f,0x40,0,0,0x199},
      {0x7f,0x10,0,0,0x198},
   };

   ACPI_SMALL_METHOD * pct = current;
   current = buildSmallMethod(current, NAMESEG("_PCT"), 0);

   {
      ACPI_RETURN_PACKAGE * returnPkg = current;
      current = buildReturnPackage(current, 2);

      {
         ACPI_SMALL_BUFFER * buff = current;
         current = buildSmallBuffer(current);

         current = buildByteConst(current, sizeof(ACPI_GENERIC_REGISTER) + sizeof(ACPI_END_TAG) );
         current = buildGenericRegister(current, &pct_gas[0]);
         current = buildEndTag(current);

         {
            U32 length = (U8 *)current - (U8 *)buff;
            buff->packageLength = (U8)length - 1;
         }
      }
      {
         ACPI_SMALL_BUFFER * buff = current;
         current = buildSmallBuffer(current);

         current = buildByteConst(current, sizeof(ACPI_GENERIC_REGISTER) + sizeof(ACPI_END_TAG) );
         current = buildGenericRegister(current, &pct_gas[1]);
         current = buildEndTag(current);

         {
            U32 length = (U8 *)current - (U8 *)buff;
            buff->packageLength = (U8)length - 1;
         }

      }

      setPackageLength(&returnPkg->package.pkgLength,
                      (U8 *)current - (U8 *)&returnPkg->package.pkgLength);
   }

   // Update package length in PCT object
   pct->packageLength = (U8)((U8 *)current - (U8 *)&pct->packageLength);

   return(current);
}

//-----------------------------------------------------------------------------
static void * buildCstate(void * current, ACPI_GENERIC_ADDRESS * gas, CSTATE * cstate)
{
   //
   // Build a C-state
   //
   ACPI_SMALL_PACKAGE * pkg1 = current;
   current = buildSmallPackage(current, 4);

   {
      {
         ACPI_SMALL_BUFFER * buffer = current;
         current = buildSmallBuffer(current);

         {
            // Buffer length
            current = buildByteConst(current, sizeof(ACPI_GENERIC_REGISTER) + sizeof(ACPI_END_TAG) );
            current = buildGenericRegister(current, gas);
            current = buildEndTag(current);
         }
         {
            U32 length = (U8 *)current - (U8 *)buffer;
            buffer->packageLength = (U8)length - 1;
         }
      }

      {
         current = buildByteConst(current, cstate->type);
         current = buildWordConst(current, cstate->latency);
         current = buildDwordConst(current, cstate->power);
      }
   }
   pkg1->packageLength = (U8)((U8 *)current - (U8 *)&pkg1->packageLength);

   return(current);
}

#ifdef BUILD_ACPI_CSD
//-----------------------------------------------------------------------------
static void * buildCSD(void * current, U32 domain, U32 cpusInDomain, PKG_CSTATES * pkg_cstates)
{
   // If (And(TYPE, 0x0040))
   // {
   //    Return (CSD Package)
   // }
   // Return(Zero)

   ACPI_METHOD * csdMethod = current;
   current = buildMethod(current, NAMESEG("_CSD"), 0);
   {
      // "IF" (And(TYPE, 0x0040)) -- IF Opcode
      current = buildOpCode(current, AML_IF_OP);
      {
         ACPI_PACKAGE_LENGTH * packageLength = current;
         current = buildPackageLength(current, 0);

         // IF ("And"(TYPE, 0x0040)) -- AND Opcode
         current = buildOpCode(current, AML_AND_OP);

         // IF (And("TYPE", 0x0040)) -- TYPE Term
         current = buildNameSeg(current, NAMESEG("TYPE"));

         // IF (And(TYPE, "0x0040")) -- DWORD Value Term
         current = buildDwordConst(current, 0x0040);

         // IF ("And(TYPE, 0x0040)") -- Target for And term (unused)
         current = buildOpCode(current, AML_ZERO_OP);

         // Build return package containing CSD package(s)
         {
            ACPI_RETURN_PACKAGE * returnPkg = current;
            current = buildReturnPackage(current, (U8)pkg_cstates->num_cstates - 1);

            {
               U32 cstateIndex;
               for (cstateIndex=1; cstateIndex < pkg_cstates->num_cstates; cstateIndex++)
               {
                  // Build CSD for this C-state

                  // Create CSD package
                  ACPI_CSD_PACKAGE * csd = current;
                  current = csd + 1;

                  setSmallPackage(&csd->package, 6);
                  csd->package.packageLength = (U8)(sizeof(ACPI_CSD_PACKAGE) - 1);

                  setByteConst(&csd->NumberOfEntries, 6);
                  setByteConst(&csd->Revision, 0);
                  setDwordConst(&csd->Domain, domain);
                  setDwordConst(&csd->CoordType, ACPI_COORD_TYPE_HW_ALL);
                  setDwordConst(&csd->NumProcessors, cpusInDomain);
                  setDwordConst(&csd->Index, cstateIndex);
               }
            }

            setPackageLength(&returnPkg->package.pkgLength,
                      (U8 *)current - (U8 *)&returnPkg->package.pkgLength);
         }

         setPackageLength(packageLength, (U8 *)current - (U8 *)packageLength);
      }
      // "Return (ZERO)"
      current = buildReturnZero(current);
   }
   // Update length in _CSD method
   setPackageLength(&csdMethod->pkgLength, (U8 *)current - (U8 *)&csdMethod->pkgLength);

   return(current);
}
#endif

//-----------------------------------------------------------------------------
U32 FindPkgIndex(PPM_HOST * host, U32 apicId, U32 * pkgIndex)
{
   SOCKET_INFO * skt = &host->skt_info;
   for (*pkgIndex = 0; *pkgIndex < skt->socket_count; (*pkgIndex)++)
   {
      CPU_DETAILS * pkg = &host->skt_info.cpu[*pkgIndex];

      if (pkg->present && (pkg->socket_id == (apicId >> (pkg->cpuidB_1._eax & 0x1f))))
         return (1ul);
   }
   return (0);
}

//-----------------------------------------------------------------------------
static U32 FindNamePath(PPM_HOST * host, U32 acpiProcessorId, PROCESSOR_NUMBER_TO_NAMESEG ** namepath)
{
   U32 i;
   for (i=0; i < host->options->cpu_map_count; i++)
   {
      if (acpiProcessorId == host->options->cpu_map[i].acpi_processor_number)
      {
         *namepath = &host->options->cpu_map[i];
         return(1ul);
      }
   }
   return (0);
}

//-----------------------------------------------------------------------------
void find_socket_proxy(void * param)
{
   PPM_HOST * host = param;
   DWORD_REGS  cpuidB_1;
   U32   x2apic_id;
   U32   socket_index;

   cpuid32_indexed(0xB, 1, &cpuidB_1._eax, &cpuidB_1._ebx, &cpuidB_1._ecx, &cpuidB_1._edx);

   // defn: Extended APIC ID -- Lower 8 bits identical to the legacy APIC ID
   x2apic_id = cpuidB_1._edx;

   // defn: socket_index = socket-specific portion of APIC ID
   socket_index = x2apic_id >> (cpuidB_1._eax & 0x1f);

   // if HOST pointer is not valid then return without touching global data structures
   if (host->signature != NAMESEG("HOST"))
      return;

   {
      U32 i;
      for (i=0; i < host->skt_info.socket_count; i++)
         if (socket_index == host->skt_info.cpu[i].socket_id)
            // data has already been collected for this processor package
            return;

      if (host->skt_info.socket_count < MAX_CPU_SOCKETS)
      {
         collect_cpu_info(host, &host->skt_info.cpu[host->skt_info.socket_count]);
         host->skt_info.socket_count++;
      }
      else
      {
         // Report error from SMP operation
         set_smp_error_code(host, EXIT_CODE_FAILED_SOCKET_PROXY_SAVE);
      }
   }
}

//-----------------------------------------------------------------------------
void collect_cpu_info(PPM_HOST * host, CPU_DETAILS * cpu)
{
   U32 temp32;
   U64 misc_enables, platform_info, pkg_cst_config_control;
   U32 status;
   U64 temp64;

   cpu->present = 1;

   cpuid32(0x1, &cpu->cpuid1._eax, &cpu->cpuid1._ebx, &cpu->cpuid1._ecx, &cpu->cpuid1._edx);
   cpuid32(0x5, &cpu->cpuid5._eax, &cpu->cpuid5._ebx, &cpu->cpuid5._ecx, &cpu->cpuid5._edx);
   cpuid32(0x6, &cpu->cpuid6._eax, &cpu->cpuid6._ebx, &cpu->cpuid6._ecx, &cpu->cpuid6._edx);
   cpuid32_indexed(0xB, 0, &cpu->cpuidB_0._eax, &cpu->cpuidB_0._ebx, &cpu->cpuidB_0._ecx, &cpu->cpuidB_0._edx);
   cpuid32_indexed(0xB, 1, &cpu->cpuidB_1._eax, &cpu->cpuidB_1._ebx, &cpu->cpuidB_1._ecx, &cpu->cpuidB_1._edx);

   // defn: Extended APIC ID -- Lower 8 bits identical to the legacy APIC ID
   cpu->x2apic_id = cpu->cpuidB_1._edx;

   cpu->eist_cpuid_feature_flag = (cpu->cpuid1._ecx & (1UL << 7)) ? 1 : 0;
   cpu->turbo_cpuid_feature_flag = (cpu->cpuid6._eax & (1UL << 1)) ? 1 : 0;
   rdmsr64(IA32_MISC_ENABLES, &misc_enables, &status); /* status ignored */
   cpu->turbo_misc_enables_feature_flag = (misc_enables & (1ULL << 38)) ? 1 : 0;
   cpu->turbo_available = ((cpu->turbo_cpuid_feature_flag == 0) && \
                           (cpu->turbo_misc_enables_feature_flag == 0)) ? 0 : 1;

   GetMaxRatio(host, &cpu->max_ratio_as_mfg);

   rdmsr64(MSR_PLATFORM_INFO, &platform_info, &status); /* status ignored */
   cpu->max_ratio_as_cfg = (U32) ((U32)platform_info >> 8) & 0xff;
   cpu->min_ratio        = (U32) ((platform_info >> 40) & 0xff);

   cpu->tdc_tdp_limits_for_turbo_flag = (platform_info & (1ULL << 29)) ? 1 : 0;
   cpu->ratio_limits_for_turbo_flag   = (platform_info & (1ULL << 28)) ? 1 : 0;
   cpu->xe_available = cpu->tdc_tdp_limits_for_turbo_flag | cpu->ratio_limits_for_turbo_flag;

   if (!is_sandybridge(host) && !is_jaketown(host))
   {
      rdmsr64(MSR_TURBO_POWER_CURRENT_LIMIT, &temp64, &status);
      temp32 = (status == 0) ? (U32)temp64 : (U32)0x02a802f8;
      cpu->tdp_limit = ( temp32 & 0x7fff );
      cpu->tdc_limit = ( (temp32 >> 16) & 0x7fff );
   }

   // defn: intra_pkg_mask_width = number of APIC ID bits used within processor package
   cpu->intra_package_mask_width = cpu->cpuidB_1._eax & 0x1f;

   // defn: socket_id = socket-specific portion of APIC ID
   cpu->socket_id = cpu->x2apic_id >> (cpu->cpuidB_1._eax & 0x1f);

   cpu->smt_mask_width = cpu->cpuidB_0._eax & 0x1f;
   cpu->smt_select_mask = ~((-1) << cpu->smt_mask_width);
   cpu->core_select_mask = (~((-1) << cpu->intra_package_mask_width) ) ^ cpu->smt_select_mask;

   rdmsr64(MSR_PKG_CST_CONFIG_CONTROL, &pkg_cst_config_control, &status); /* status ignored */
   cpu->package_cstate_limit =  (U32)pkg_cst_config_control & 7;
   cpu->core_c1_supported = ((cpu->cpuid5._edx >> 4) & 0xf) ? 1 : 0;
   cpu->core_c3_supported = ((cpu->cpuid5._edx >> 8) & 0xf) ? 1 : 0;
   cpu->core_c6_supported = ((cpu->cpuid5._edx >> 12) & 0xf) ? 1 : 0;
   cpu->core_c7_supported = ((cpu->cpuid5._edx >> 16) & 0xf) ? 1 : 0;
   cpu->mwait_supported = (cpu->cpuid5._ecx & (1UL << 0)) ? 1 : 0;

   cpu->acpi_support_cpuid_feature_flag = (cpu->cpuid1._edx & (1UL << 22)) ? 1 : 0;
   cpu->invariant_apic_timer_flag = (cpu->cpuid6._eax & (1UL << 2)) ? 1 : 0;

   cpu->energy_perf_bias_supported = detect_and_enable_energy_perf_bias_msr(host);

   // if the energy_perf_bias is supported, then CPUID leaf 6 will be updated with a new feature flag
   if (cpu->energy_perf_bias_supported)
      cpuid32(0x6, &cpu->cpuid6._eax, &cpu->cpuid6._ebx, &cpu->cpuid6._ecx, &cpu->cpuid6._edx);

   if (is_sandybridge(host) || is_jaketown(host))
   {
      rdmsr64(MSR_PKG_RAPL_POWER_LIMIT, &cpu->package_power_limit, &status);
      rdmsr64(MSR_RAPL_POWER_UNIT, &cpu->package_power_sku_unit, &status);
   }
}

//-----------------------------------------------------------------------------
// returns 0=unsupported and 1=supported
static U32 detect_and_enable_energy_perf_bias_msr(PPM_HOST * host)
{
   U64 temp64;
   U32 status;

   if (is_sandybridge(host) || is_jaketown(host))
   {
     // If MSR_POWER_CTL is not readable
     // then Energy Performance Bias MSR is not supported
     rdmsr64(MSR_POWER_CTL, &temp64, &status);
     if (status == ~0U)
        return 0;

     // If setting MSR_POWER_CTL bit 18 causes a GPF
     // then Energy Performance Bias MSR cannot be made visible and is not supported
     temp64 |= 1UL << 18;
     wrmsr64(MSR_POWER_CTL, temp64, &status);
     if (status == ~0U)
        return 0;
   }
   else
   {
      // If MSR_MISC_PWR_MGMT is not readable
      // then Energy Performance Bias MSR is not supported
      rdmsr64(MSR_MISC_PWR_MGMT, &temp64, &status);
      if (status == ~0U)
         return 0;

      // If setting MSR_MISC_PWR_MGMT bit 1 causes a GPF
      // then Energy Performance Bias MSR cannot be made visible and is not supported
      temp64 |= 1UL << 1;
      wrmsr64(MSR_MISC_PWR_MGMT, temp64, &status);
      if (status == ~0U)
         return 0;
   }

   // IA32_ENERGY_PERF_BIAS is now software visible
   // if CPUID.(EAX=06h):ECX[3] == 1
   {
      DWORD_REGS cpuid6;
      cpuid32(0x6, &cpuid6._eax, &cpuid6._ebx, &cpuid6._ecx, &cpuid6._edx);
      temp64 = cpuid6._ecx & (1UL << 3);
      if (temp64 == 0)
         return 0;
   }

   // If reading IA32_ENERGY_PERF_BIAS MSR causes a GPF
   // then Energy Performance Bias MSR is not supported
   rdmsr64(IA32_ENERGY_PERF_BIAS, &temp64, &status);
   if (status == ~0U)
      return 0;

   // IA32_ENERGY_PERF_BIAS MSR is supported
   return 1;
}

//-----------------------------------------------------------------------------
void init_logical_cpu(void * param)
{
   // Perform final logical processor initialization
   // (1) Gather processor info
   //    (1.1)Find the APIC ID
   //    (1.2) Find package index for the APIC ID
   //    (1.3) Create pointer to this CPU
   // (2) Set EIST and Turbo state
   //    (2.1) Read IA32_MISC_ENABLE MSR 1A0h
   //    (2.2) Set EIST state (enabled or disabled) using IA32_MISC_ENABLE MSR 1A0h bit [16]
   //    (2.3) Set Turbo state (enabled or disabled) using IA32_MISC_ENABLE MSR 1A0h bit [38]
   //    (2.4) Write IA32_MISC_ENABLE MSR 1A0h
   // (3) Set EIST Hardware Coordination to enabled state
   // (4) Force P0 pstate
   //    (4.1) Find package index for the APIC ID
   //    (4.2) Write package-specific P0 pstate to the MSR_IA32_PERF_CTL MSR 199h
   // (5) Force IO Redirection and set max package C-state
   //    (5.1) Force IO Redirection as enabled
   //    (5.2) Set max package C-state as min of user setup option and capability of CPU
   //    (5.3) Write PMG_CST_CONFIG_CONTROL MSR with IO Redirection and set max package C-state
   // (6) Force IO redirection related paramters
   //    (6.1) Force IO redirection for all C-states
   //    (6.3) Set LVL2 Base Address based on user input
   //    (6.4) Write PMG_IO_CAPTURE_BASE MSR with IO redirection paramters
   // (7) Set state of C1E feature based on user input
   // (8) Set state of Energy Performance Bias based on user input


   PPM_HOST * host = param;
   DWORD_REGS  cpuidB_1;
   U32 x2apic_id;
   U32 pkg_index;
   CPU_DETAILS * cpu;
   U32 core_count;
   U64 temp64;
   U32 status;


   // (1) Gather processor info
   {
      U32 core_index;
      U32 threads_per_core;

      cpuid32_indexed(0xB, 1, &cpuidB_1._eax, &cpuidB_1._ebx, &cpuidB_1._ecx, &cpuidB_1._edx);

      // defn: Extended APIC ID -- Lower 8 bits identical to the legacy APIC ID
      x2apic_id = cpuidB_1._edx;

      // Find package index for the APIC ID
      FindPkgIndex(host, x2apic_id, &pkg_index);

      // Create pointer to this CPU
      cpu = &host->skt_info.cpu[pkg_index];

      core_index = (x2apic_id & cpu->core_select_mask) >> cpu->smt_mask_width;
      threads_per_core = cpu->core_logical_processor_count_from_madt[core_index];
      core_count = cpu->logical_processor_count_from_madt / threads_per_core;
   }

   // (2) Set EIST and Turbo state
   {
      // (2.1) Read IA32_MISC_ENABLES MSR 1A0h
      rdmsr64(IA32_MISC_ENABLES, &temp64, &status); /* status ignored */

      // (2.2) Set EIST state (enabled or disabled) using IA32_MISC_ENABLES MSR 1A0h bit [16]
      temp64 = temp64 | (1ULL << 16); /* Leave enabled in case frequency transitions incomplete */

      // (2.3) Set Turbo state (enabled or disabled) using IA32_MISC_ENABLES MSR 1A0h bit [38]
      // Note: If Turbo is factory-configured as disabled, do not attempt to touch this bit.
      if (cpu->turbo_available)
      {
         temp64 = temp64 & ~(1ULL << 38);
         temp64 = temp64 | ((U64)(host->turbo_enabled ^ 1) << 38);
      }

      // (2.4) Write IA32_MISC_ENABLES MSR 1A0h
      wrmsr64(IA32_MISC_ENABLES, temp64, &status); /* status ignored */
   }

   // (3) Set EIST Hardware Coordination to enabled state
   {
      rdmsr64(MSR_MISC_PWR_MGMT, &temp64, &status); /* status ignored */
      temp64 = temp64 & ~(1ULL << 0);
      wrmsr64(MSR_MISC_PWR_MGMT, temp64, &status); /* status ignored */
   }

   // (4) Force P0 pstate
   {
      // Write package-specific P0 pstate to the IA32_PERF_CTL MSR 199h
      wrmsr64(IA32_PERF_CTL, encode_pstate(host, cpu->pkg_pstates.pstate[0].ratio), &status); /* status ignored */
   }

   // (5) Force IO Redirection and set max package C-state
   {
      // (5.1) Force IO Redirection as enabled
      U32 io_redirection = (1 << 10);
      U32 c1_auto_demotion = (1 << 26);
      U32 c3_auto_demotion = (1 << 25);
      U32 cfg_lock = (1 << 15);

      // (5.2) Set max package C-state as min of user setup option and capability of CPU
      U32 pkg_limit = (host->options->package_cstate_limit <= 7) ?  host->options->package_cstate_limit : 7;
      pkg_limit = (cpu->package_cstate_limit < pkg_limit) ? cpu->package_cstate_limit : pkg_limit;

      // (5.3) Write PKG_CST_CONFIG_CONTROL MSR with IO Redirection and set max package C-state
      rdmsr64(MSR_PKG_CST_CONFIG_CONTROL, &temp64, &status); /* status ignored */
      temp64 |= c1_auto_demotion | c3_auto_demotion;
      if ( !((U32)temp64 & cfg_lock) )
         temp64 |= pkg_limit | io_redirection;
      wrmsr64(MSR_PKG_CST_CONFIG_CONTROL, temp64, &status); /* status ignored */
   }

   // (6) Force IO redirection related paramters
   {
      // (6.1) Force IO redirection for all C-states
      U32 cst_range = (2 << 16);

      // (6.3) Set LVL2 Base Address based on user input
      U32 lvl2_base_addr = host->options->pmbase + 0x014;

      // (6.4) Write PMG_IO_CAPTURE_BASE MSR with IO redirection paramters
      wrmsr64(MSR_PMG_IO_CAPTURE_BASE, cst_range | lvl2_base_addr, &status); /* status ignored */
   }

   // (7) Set state of C1E feature based on user input
   {
      rdmsr64(MSR_POWER_CTL, &temp64, &status); /* status ignored */
      if (host->options->c1e_enabled)
         temp64 |= (1ULL << 1);
      else
         temp64 &= ~(1ULL << 1);
      wrmsr64(MSR_POWER_CTL, temp64, &status); /* status ignored */
   }

   // (8) Set state of Energy Performance Bias based on user input
   {
      if ( detect_and_enable_energy_perf_bias_msr(host) )
      {
         // Configure "Performance/Watt" setting via IA32_ENERGY_PERF_BIAS MSR
         // For Nehalem family processors
         // 1= "Power Optimized" or 0="Traditional"
         // For Sandy Bridge family processors
         //    2="Low Power" or 1="Balanced or 0="Max Performance"
         if (!is_sandybridge(host) && !is_jaketown(host))
            temp64 = (host->performance_per_watt != 0) ? 4 : 0;
         else
            temp64 = (host->performance_per_watt == 2) ? 7 :
                     (host->performance_per_watt == 1) ? 4 : 0;
         wrmsr64(IA32_ENERGY_PERF_BIAS, temp64, &status);
      }
   }
   if (is_sandybridge(host) || is_jaketown(host))
   {
     // Setup programmable c-state interrupt latency response times
     rdmsr64(MSR_PKGC3_IRTL, &temp64, &status);
     // Clear bits [12:10]
     temp64 &= ~(((1ULL << 3) - 1) << 10);
     // Set time unit as 32768ns
     temp64 |= 3 << 10;
     // Clear bits [9:0]
     temp64 &= ~((1ULL << 10) - 1);
     // Set time limit
     temp64 |= 2;
     temp64 |= 1 << 15;
     wrmsr64(MSR_PKGC3_IRTL, temp64, &status);

     rdmsr64(MSR_PKGC6_IRTL, &temp64, &status);
     // Clear bits [12:10]
     temp64 &= ~(((1ULL << 3) - 1) << 10);
     // Set time unit as 1024ns
     temp64 |= 2 << 10;
     // Clear bits [9:0]
     temp64 &= ~((1ULL << 10) - 1);
     // Set time limit
     temp64 |= core_count == 4 ? 0x5B : 0x54;
     temp64 |= 1 << 15;
     wrmsr64(MSR_PKGC6_IRTL, temp64, &status);

     rdmsr64(MSR_PKGC7_IRTL, &temp64, &status);
     // Clear bits [12:10]
     temp64 &= ~(((1ULL << 3) - 1) << 10);
     // Set time unit as 1024ns
     temp64 |= 2 << 10;
     // Clear bits [9:0]
     temp64 &= ~((1ULL << 10) - 1);
     // Set time limit
     temp64 |= core_count == 4 ? 0x5B : 0x54;
     temp64 |= 1 << 15;
     wrmsr64(MSR_PKGC7_IRTL, temp64, &status);

     {
       U32 c1_auto_undemotion_enable = 1 << 28;
       U32 c3_auto_undemotion_enable = 1 << 27;

       rdmsr64(MSR_PKG_CST_CONFIG_CONTROL, &temp64, &status); /* status ignored */
       temp64 |= c1_auto_undemotion_enable | c3_auto_undemotion_enable;
       wrmsr64(MSR_PKG_CST_CONFIG_CONTROL, temp64, &status); /* status ignored */
     }
   }
}

//-----------------------------------------------------------------------------
void GetMaxRatio(PPM_HOST * host, U32 * max_non_turbo_ratio)
{
   U32 _eax, dummy;
   U32 index;
   U32 * brand_ptr;
   U32 leaf_num;
   U8 brand_str[48];
   U32 max_ratio=0;

   // Verify CPUID brand string function is supported
   cpuid32(0x80000000, &_eax, &dummy, &dummy, &dummy);
   if (_eax < 80000004)
   {
      *max_non_turbo_ratio = max_ratio;
      return;
   }

   // Build CPUID brand string
   brand_ptr = (U32 *)(brand_str);
   for (leaf_num=0x80000002; leaf_num<=0x80000004; leaf_num++)
   {
      cpuid32(leaf_num, &brand_ptr[0], &brand_ptr[1], &brand_ptr[2], &brand_ptr[3]);
      brand_ptr += 4;
   }

   // Search brand string for "x.xxGHz" where x is a digit
   for (index=0; index<=40; index++)
   {
      if ( (brand_str[index]   >= 0x30) && \
           (brand_str[index]   <= 0x39) && \
           (brand_str[index+2] >= 0x30) && \
           (brand_str[index+2] <= 0x39) && \
           (brand_str[index+3] >= 0x30) && \
           (brand_str[index+3] <= 0x39) && \
           (brand_str[index+4] == 'G')  && \
           (brand_str[index+5] == 'H')  && \
           (brand_str[index+6] == 'z'))
      {
         // Compute frequency (in MHz) from brand string
         max_ratio  = (U32)(brand_str[index]  -0x30) * 1000;
         max_ratio += (U32)(brand_str[index+2]-0x30) * 100;
         max_ratio += (U32)(brand_str[index+2]-0x30) * 10;

         if (get_bclk(host) == 133)
         {
           // Find nearest full/half multiple of 66/133 MHz
           max_ratio *= 3;
           max_ratio += 100;
           max_ratio /= 200;
           max_ratio *= 200;
           max_ratio /= 3;
         }

         // Divide adjusted frequency by base clock
         max_ratio /= get_bclk(host);
         break;
      }
   }

   // Return non-zero Max Non-Turbo Ratio obtained from CPUID brand string
   // or return 0 indicating Max Non-Turbo Ratio not available
   *max_non_turbo_ratio = max_ratio;
}

//-----------------------------------------------------------------------------
static void * buildTPC(void * current)
{
   ACPI_SMALL_METHOD * tpc = current;
   current = buildSmallMethod(current, NAMESEG("_TPC"), 0);

   current = buildReturnZero(current);

   // Update package length in PPC object
   tpc->packageLength = (U8) ( (U8 *)current - (U8 *)&tpc->packageLength );

   return(current);
}

//-----------------------------------------------------------------------------
static void * buildPTC(void * current)
{
   static const ACPI_GENERIC_ADDRESS ptc_gas[] = {
      {0x7f,0x00,0,0,0},
      {0x7f,0x00,0,0,0},
   };

   ACPI_SMALL_METHOD * ptc = current;
   current = buildSmallMethod(current, NAMESEG("_PTC"), 0);

   {
      ACPI_RETURN_PACKAGE * returnPkg = current;
      current = buildReturnPackage(current, 2);

      {
         ACPI_SMALL_BUFFER * buff = current;
         current = buildSmallBuffer(current);

         current = buildByteConst(current, sizeof(ACPI_GENERIC_REGISTER) + sizeof(ACPI_END_TAG) );
         current = buildGenericRegister(current, &ptc_gas[0]);
         current = buildEndTag(current);

         {
            U32 length = (U8 *)current - (U8 *)buff;
            buff->packageLength = (U8)length - 1;
         }
      }
      {
         ACPI_SMALL_BUFFER * buff = current;
         current = buildSmallBuffer(current);

         current = buildByteConst(current, sizeof(ACPI_GENERIC_REGISTER) + sizeof(ACPI_END_TAG) );
         current = buildGenericRegister(current, &ptc_gas[1]);
         current = buildEndTag(current);

         {
            U32 length = (U8 *)current - (U8 *)buff;
            buff->packageLength = (U8)length - 1;
         }
      }

      setPackageLength(&returnPkg->package.pkgLength,
                      (U8 *)current - (U8 *)&returnPkg->package.pkgLength);
   }

   // Update package length in PTC object
   ptc->packageLength = (U8)((U8 *)current - (U8 *)&ptc->packageLength);

   return(current);
}

//-----------------------------------------------------------------------------
static void * buildTSS(void * current, PKG_TSTATES * pkg_tstates)
{
   //
   // IF (LAnd(TSEN, And(TYPE,4)))
   // {
   //    Return (Package of Tstate Packages)
   // }
   // Return(Zero)
   //
   ACPI_METHOD * tss = current;
   current = buildMethod(current, NAMESEG("_TSS"), 0);

   {
      // "IF" (LAnd(TSEN, And(TYPE,4))) -- IF Opcode
      current = buildOpCode(current, AML_IF_OP);
      {
         ACPI_PACKAGE_LENGTH * packageLength = current;
         current = buildPackageLength(current, 0);

         // IF ("LAnd"(TSEN, And(TYPE, 4))) -- LAND Opcode
         current = buildOpCode(current, AML_LAND_OP);

         // IF (LAnd("TSEN", And(TYPE, 4))) -- TSEN Term
         current = buildNameSeg(current, NAMESEG("TSEN"));

         // IF (LAnd(TSEN, "And"(TYPE, 4))) -- AND Opcode
         current = buildOpCode(current, AML_AND_OP);

         // IF (LAnd(TSEN, And("TYPE", 4))) -- TYPE Term
         current = buildNameSeg(current, NAMESEG("TYPE"));

         // IF (LAnd(TSEN, And(TYPE, "4"))) -- DWORD Value Term
         current = buildWordConst(current, 4);

         // IF (LAnd(MWOS, "And(TYPE, 4)")) -- Target for And term (unused)
         current = buildOpCode(current, AML_ZERO_OP);

         // Return (Package of Tstate Packages)
         {
            ACPI_RETURN_PACKAGE * returnPkg = current;
            current = buildReturnPackage(current, (U8)pkg_tstates->num_tstates);

            // (3.3.3) For each T-state
            {
               U32 tstateIndex = 0;
               for (tstateIndex=0; tstateIndex < pkg_tstates->num_tstates; tstateIndex++)
               {
                  // (3.3.3.1) Create T-state package
                  ACPI_TSTATE_PACKAGE * tstate = current;
                  current = tstate + 1;

                  setSmallPackage(&tstate->package, 5);
                  tstate->package.packageLength = (U8)(sizeof(ACPI_TSTATE_PACKAGE) - 1);

                  setDwordConst(&tstate->FreqPercent, pkg_tstates->tstate[tstateIndex].freqpercent);
                  setDwordConst(&tstate->Power, pkg_tstates->tstate[tstateIndex].power);
                  setDwordConst(&tstate->TransLatency, pkg_tstates->tstate[tstateIndex].latency);
                  setDwordConst(&tstate->Control, pkg_tstates->tstate[tstateIndex].control);
                  setDwordConst(&tstate->Status, pkg_tstates->tstate[tstateIndex].status);
               } // for
            } // for block

            // (3.3.4) Update package length in return package
            setPackageLength(&returnPkg->package.pkgLength, (U8 *)current - (U8 *)&returnPkg->package.pkgLength);
         }

         // "IF (LAnd(TSEN, And(TYPE,4))) and its body" -- Set package length
         setPackageLength(packageLength, (U8 *)current - (U8 *)packageLength);
      }
      // "Return (ZERO)"
      current = buildReturnZero(current);
   }
   // Set package length for the _TSS object
   setPackageLength(&tss->pkgLength, (U8 *)current - (U8 *)&tss->pkgLength);

   return(current);
}

//-----------------------------------------------------------------------------
static void * buildTSD(void * current, U32 domain, U32 cpusInDomain)
{
   // If (And(TYPE, 0x0080))
   // {
   //    Return (Package containing TSD package)
   // }
   // Return(Zero)

   ACPI_METHOD * tsdMethod = current;
   current = buildMethod(current, NAMESEG("_TSD"), 0);
   {
      // "IF" (And(TYPE, 0x0080)) -- IF Opcode
      current = buildOpCode(current, AML_IF_OP);
      {
         ACPI_PACKAGE_LENGTH * packageLength = current;
         current = buildPackageLength(current, 0);

         // IF ("And"(TYPE, 0x0080)) -- AND Opcode
         current = buildOpCode(current, AML_AND_OP);

         // IF (And("TYPE", 0x0080)) -- TYPE Term
         current = buildNameSeg(current, NAMESEG("TYPE"));

         // IF (And(TYPE, "0x0080")) -- DWORD Value Term
         current = buildDwordConst(current, 0x0080);

         // IF ("And(TYPE, 0x0080)") -- Target for And term (unused)
         current = buildOpCode(current, AML_ZERO_OP);

         // Build package containing TSD package
         {
            ACPI_RETURN_PACKAGE * returnPkg = current;
            current = buildReturnPackage(current, 1);

            {
               // Create PSD package
               ACPI_TSD_PACKAGE * tsd = current;
               current = tsd + 1;

               setSmallPackage(&tsd->package, 5);
               tsd->package.packageLength = (U8)(sizeof(ACPI_TSD_PACKAGE) - 1);

               setByteConst(&tsd->NumberOfEntries, 5);
               setByteConst(&tsd->Revision, 0);
               setDwordConst(&tsd->Domain, domain);
               setDwordConst(&tsd->CoordType, ACPI_COORD_TYPE_SW_ANY);
               setDwordConst(&tsd->NumProcessors, cpusInDomain);

            } // TSD package

            setPackageLength(&returnPkg->package.pkgLength,
                      (U8 *)current - (U8 *)&returnPkg->package.pkgLength);
         }

         setPackageLength(packageLength, (U8 *)current - (U8 *)packageLength);
      }
      // "Return (ZERO)"
      current = buildReturnZero(current);
   }
   // Update length in _TSD method
   setPackageLength(&tsdMethod->pkgLength, (U8 *)current - (U8 *)&tsdMethod->pkgLength);

   return(current);
}

//-----------------------------------------------------------------------------
static void * buildCpuScope (void * current, U32 cpu_namespace, PROCESSOR_NUMBER_TO_NAMESEG * aslCpuNamePath)
{
   ACPI_SCOPE * scope = current;
   current = scope + 1;

   scope->scopeOpcode = AML_SCOPE_OP;
   scope->rootChar = AML_ROOT_PREFIX;

   if (aslCpuNamePath->seg_count == 1)
   {
      DUAL_NAME_PATH * dualNamePath = current;
      current = dualNamePath + 1;
      dualNamePath->prefix = AML_DUAL_NAME_PREFIX;
      dualNamePath->nameseg[0] = cpu_namespace;
      dualNamePath->nameseg[1] = aslCpuNamePath->nameseg[0];
   }
   else
   {
      MULTI_NAME_PATH * multiNamePath = current;
      current = multiNamePath + 1;
      multiNamePath->prefix = AML_MULTI_NAME_PREFIX;
      // the nameseg count includes the root prefix and all other namesegs
      multiNamePath->segCount = (U8) aslCpuNamePath->seg_count+1;
      multiNamePath->nameseg[0] = cpu_namespace;
      {
         U32 i;
         for (i=0; i<aslCpuNamePath->seg_count; i++)
            multiNamePath->nameseg[i+1] = aslCpuNamePath->nameseg[i];
      }
   }
   return (current);
}

static U32 ProcessFadtTables(PPM_HOST * host)
{
   if (host->options->modify_fadt_flag)
   {
      if ( ProcessFadt(host->acpi_tables.FacpPointer, host->options->pmbase) == 0 )
         return(0);

      if ((host->acpi_tables.FacpPointer64 != (void *)0ul) &&
          (host->acpi_tables.FacpPointer64 != host->acpi_tables.FacpPointer))
      {
         if ( ProcessFadt(host->acpi_tables.FacpPointer64, host->options->pmbase) == 0 )
            return(0);
      }
   }
   return 1;
}

static U32 encode_pstate(PPM_HOST * host, U32 ratio)
{
  if (is_jaketown(host) || is_sandybridge(host))
    return ratio << 8;
  return ratio;
}

static U32 get_bclk(PPM_HOST * host)
{
  return (is_jaketown(host) || is_sandybridge(host)) ? 100 : 133;
}

static U32 compute_tdp(PPM_HOST * host, CPU_DETAILS * cpu)
{
  if (is_jaketown(host) || is_sandybridge(host))
  {
    U64 power_limit_1 = cpu->package_power_limit & ((1ULL << 15) - 1);
    U64 power_unit = cpu->package_power_sku_unit & ((1ULL << 4) - 1);
    U64 tdp = divU64byU64(power_limit_1, 1 << power_unit, NULL);
    return (U32)tdp;
  }
  else
  {
    // tdp = (TURBO_POWER_CURRENT_LIMIT MSR 1ACh bit [14:0] / 8) Watts
    return cpu->tdp_limit / 8;
  }
}

static U32 compute_pstate_power(PPM_HOST * host, CPU_DETAILS * cpu, U32 ratio, U32 TDP)
{
  if (is_jaketown(host) || is_sandybridge(host))
  {
    U32 P1_Ratio = cpu->max_ratio_as_mfg;
    U64 M, pstate_power;

    // M = ((1.1 - ((P1_ratio - ratio) * 0.00625)) / 1.1) ^2
    // To prevent loss of precision compute M * 10^5 (preserves 5 decimal places)
    M = (P1_Ratio - ratio) * 625;
    M = (110000 - M);
    M = divU64byU64(M, 11, NULL);
    M = divU64byU64(mulU64byU64(M, M, NULL), 1000, NULL);

    // pstate_power = ((ratio/p1_ratio) * M * TDP)
    // Divide the final answer by 10^5 to remove the precision factor
    pstate_power = mulU64byU64(ratio, M, NULL);
    pstate_power = mulU64byU64(pstate_power, TDP, NULL);
    pstate_power = divU64byU64(pstate_power, P1_Ratio, NULL);
    pstate_power = divU64byU64(pstate_power, 100000, NULL);
    return (U32)pstate_power; // in Watts
  }
  else
  {
    // pstate_power[ratio] = (ratio/P1_ratio)^3 * Core_TDP + Uncore_TDP

    // Core_TDP = (TURBO_POWER_CURRENT_LIMIT MSR 1ACh bit [30:16] / 8) Watts
    U32 Core_TDP = cpu->tdc_limit / 8;

    // Uncore_TDP = TDP - Core_TDP
    U32 Uncore_TDP = TDP - Core_TDP;

    // max_ratio_as_mfg = P1_Ratio derived from Brand String returned by CPUID instruction
    U32 P1_Ratio = cpu->max_ratio_as_mfg;

    #define PRECISION_FACTOR         (U32) 30
    #define PRECISION_FACTOR_CUBED   (U32) (PRECISION_FACTOR * PRECISION_FACTOR * PRECISION_FACTOR)

    U32 ratio_factor = (ratio * PRECISION_FACTOR)/P1_Ratio;
    return ((ratio_factor * ratio_factor * ratio_factor * Core_TDP) / PRECISION_FACTOR_CUBED) + Uncore_TDP;
  }
}
