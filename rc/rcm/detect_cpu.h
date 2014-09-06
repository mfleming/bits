/*
Copyright (c) 2010, Intel Corporation
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

#ifndef  detect_cpu_h
#define  detect_cpu_h

#include "datatype.h"
#include "smp.h"

//#define HARDCODED_CPU_FAMILY WESTMERE

enum cpu_family {
    AUBURNDALE,
    CLARKDALE,
    NEHALEM,
    NEHALEM_EX,
    JAKETOWN,
    LYNNFIELD,
    SANDY_BRIDGE,
    WESTMERE_EX,
    WESTMERE,
};

#ifdef HARDCODED_CPU_FAMILY

static enum cpu_family get_cpu_family(PPM_HOST * host)
{
    (void)host;

    return HARDCODED_CPU_FAMILY;
}

static void detect_cpu_family(PPM_HOST * host)
{
    (void)host;
}

#else

static enum cpu_family get_cpu_family(PPM_HOST * host)
{
    return host->detected_cpu_family;
}

static void detect_cpu_family(PPM_HOST * host)
{
    U32 signature, dummy;

    cpuid32(1, &signature, &dummy, &dummy, &dummy);

    if ((0x106a0 <= signature) && (signature <= 0x106af))
        host->detected_cpu_family = NEHALEM;
    else if ((0x106e0 <= signature) && (signature <= 0x106ef))
        host->detected_cpu_family = LYNNFIELD;
    else if ((0x106e0 <= signature) && (signature <= 0x106ef))
        host->detected_cpu_family = AUBURNDALE;
    else if ((0x20650 <= signature) && (signature <= 0x2065f))
        host->detected_cpu_family = CLARKDALE;
    else if ((0x206c0 <= signature) && (signature <= 0x206cf))
        host->detected_cpu_family = WESTMERE;
    else if ((0x206a0 <= signature) && (signature <= 0x206af))
        host->detected_cpu_family = SANDY_BRIDGE;
    else if ((0x206d0 <= signature) && (signature <= 0x206df))
        host->detected_cpu_family = JAKETOWN;
    else if ((0x206e0 <= signature) && (signature <= 0x206ef))
        host->detected_cpu_family = NEHALEM_EX;
    else if ((0x206f0 <= signature) && (signature <= 0x206ff))
        host->detected_cpu_family = WESTMERE_EX;
}

#endif

static bool is_sandybridge(PPM_HOST * host)
{
    return get_cpu_family(host) == SANDY_BRIDGE;
}

static bool is_jaketown(PPM_HOST * host)
{
    return get_cpu_family(host) == JAKETOWN;
}

#endif // detect_cpu_h
