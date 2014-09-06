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

#ifndef CPU_H
#define CPU_H

#define MSR_PLATFORM_INFO 0x00CE
#define MSR_PKG_CST_CONFIG_CONTROL 0x00E2
#define MSR_PMG_IO_CAPTURE_BASE 0x0E4
#define IA32_PERF_CTL 0x0199
#define IA32_MISC_ENABLES 0x01A0
#define MSR_MISC_PWR_MGMT 0x01AA
#define MSR_TURBO_POWER_CURRENT_LIMIT 0x1AC
#define IA32_ENERGY_PERF_BIAS 0x01B0
#define MSR_POWER_CTL 0x01FC
#define IA32_X2APIC_ICR 0x830
#define IA32_X2APIC_INIT_COUNT 0x838
#define IA32_X2APIC_CUR_COUNT 0x839
#define IA32_APIC_BASE 0x001B

#define MSR_RAPL_POWER_UNIT 0x606
#define MSR_PKGC3_IRTL 0x60A
#define MSR_PKGC6_IRTL 0x60B
#define MSR_PKGC7_IRTL 0x60C
#define MSR_PKG_RAPL_POWER_LIMIT 0x610

// Memory-mapped APIC Offsets
#define APIC_ICR_LO             0x300
#define APIC_ICR_HI             0x310
#define APIC_TMR_INITIAL_CNT    0x380
#define APIC_TMR_CURRENT_CNT    0x390

#endif /* CPU_H */
