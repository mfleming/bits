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

#include <grub/dl.h>
#include <grub/env.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include "bitsutil.h"

GRUB_MOD_LICENSE("GPLv3+");
GRUB_MOD_DUAL_LICENSE("3-clause BSD");

grub_err_t strtou32_h(const char *str, U32 * value_ret)
{
    U32 value;
    int base = 0;
    char *end;
    const char *last = str + grub_strlen(str) - 1;
    if (last < str)
        return grub_error(GRUB_ERR_BAD_NUMBER, "Unrecognized number");
    if (grub_tolower(*last) == 'h') {
        base = 16;
        last--;
        if (last < str)
            return grub_error(GRUB_ERR_BAD_NUMBER, "Unrecognized number");
    }
    value = grub_strtoul(str, &end, base);
    if (grub_errno != GRUB_ERR_NONE || end != (last + 1)) {
        return grub_error(GRUB_ERR_BAD_NUMBER, "Unrecognized number");
    } else {
        *value_ret = value;
        return GRUB_ERR_NONE;
    }
}

grub_err_t strtou64_h(const char *str, U64 * value_ret)
{
    U64 value;
    int base = 0;
    char *end;
    const char *last = str + grub_strlen(str) - 1;
    if (last < str)
        return grub_error(GRUB_ERR_BAD_NUMBER, "Unrecognized number");
    if (grub_tolower(*last) == 'h') {
        base = 16;
        last--;
        if (last < str)
            return grub_error(GRUB_ERR_BAD_NUMBER, "Unrecognized number");
    }
    value = grub_strtoull(str, &end, base);
    if (grub_errno != GRUB_ERR_NONE || end != (last + 1)) {
        return grub_error(GRUB_ERR_BAD_NUMBER, "Unrecognized number");
    } else {
        *value_ret = value;
        return GRUB_ERR_NONE;
    }
}

void dprintf(const char *debug_context, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    dvprintf(debug_context, fmt, args);
    va_end(args);
}

void dvprintf(const char *debug_context, const char *fmt, va_list args)
{
    const char *debug_env = grub_env_get("debug");

    if (debug_env && grub_strword(debug_env, debug_context))
        grub_vprintf(fmt, args);
}

U64 mulU64byU64(U64 a, U64 b, U64 * high)
{
    U64 b_high = 0;
    U64 r_high = 0, r_low = 0;
    U64 bit;

    for (bit = 1; bit; bit <<= 1) {
        if (a & bit) {
            if (r_low + b < r_low)
                r_high++;
            r_low += b;
            r_high += b_high;
        }
        b_high <<= 1;
        b_high |= (b & (1ULL << 63)) >> 63;
        b <<= 1;
    }

    if (high)
        *high = r_high;
    return r_low;
}

U64 divU64byU64(U64 n, U64 d, U64 * rem)
{
    U32 i;
    U64 q = n;
    U64 r = 0;

    for (i = 0; i < 64; i++) {
        r <<= 1;
        r |= (q & (1ULL << 63)) >> 63;
        q <<= 1;
        if (r >= d) {
            r -= d;
            q |= 1;
        }
    }
    if (rem)
        *rem = r;
    return q;
}
